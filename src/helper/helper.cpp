// SPDX-License-Identifier: GPL-3.0-or-later
#include "helper/helper.h"

#include "core/common.h"
#include "core/i18n.h"
#include "core/usbdevices.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

using namespace WinUsb;

namespace {

std::atomic_bool g_cancel{false};

void onSignal(int)
{
    g_cancel = true;
}

void emitLine(const QString &line)
{
    const QByteArray data = line.toUtf8() + '\n';
    fwrite(data.constData(), 1, size_t(data.size()), stdout);
    fflush(stdout);
}

void step(const QString &code) { emitLine(QStringLiteral("STEP ") + code); }
void log(const QString &text) { emitLine(QStringLiteral("LOG ") + text); }

struct Failure
{
    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw Failure{message};
}

void checkCancel()
{
    if (g_cancel)
        fail(QStringLiteral("cancelled"));
}

struct RunResult
{
    int code = -1;
    QString output;
};

RunResult run(const QString &program, const QStringList &args, const QByteArray &input = {}, int timeoutMs = 300000)
{
    log(program + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted())
        return {-1, QCoreApplication::translate("Helper", "%1: no se pudo ejecutar").arg(program)};
    if (!input.isEmpty())
        p.write(input);
    p.closeWriteChannel();
    p.waitForFinished(timeoutMs);
    RunResult r{p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1,
                QString::fromUtf8(p.readAll()).trimmed()};
    if (!r.output.isEmpty())
        for (const QString &l : r.output.split(QLatin1Char('\n')))
            log(QStringLiteral("  ") + l);
    return r;
}

void runOrFail(const QString &program, const QStringList &args, const QString &error, const QByteArray &input = {})
{
    const RunResult r = run(program, args, input);
    if (r.code != 0)
        fail(error + (r.output.isEmpty() ? QString() : QStringLiteral("\n") + r.output));
}

void settle()
{
    run(QStringLiteral("udevadm"), {QStringLiteral("settle"), QStringLiteral("--timeout=10")});
}

// Desmonta todo lo que esté montado del disco (p. ej. montajes automáticos del escritorio)
void unmountAll(const QString &disk)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QStringList mps = UsbDevices::mountPoints(disk);
        if (mps.isEmpty())
            return;
        for (const QString &mp : mps)
            run(QStringLiteral("umount"), {mp});
        QThread::msleep(500);
    }
    if (!UsbDevices::mountPoints(disk).isEmpty())
        fail(QCoreApplication::translate("Helper", "No se pudo desmontar %1. Cierre los programas que estén usando la unidad.").arg(disk));
}

struct Mounts
{
    QStringList points;

    void mount(const QString &device, const QString &dir, const QStringList &options = {})
    {
        QStringList args = options;
        args << device << dir;
        // El escritorio puede tardar en liberar los nodos recién creados: reintentar
        RunResult r;
        for (int attempt = 0; attempt < 5; ++attempt) {
            r = run(QStringLiteral("mount"), args);
            if (r.code == 0) {
                points.prepend(dir);
                return;
            }
            QThread::msleep(1000);
            settle();
        }
        fail(QCoreApplication::translate("Helper", "No se pudo montar %1\n%2").arg(device, r.output));
    }

    void unmountAll()
    {
        for (const QString &p : std::as_const(points))
            if (run(QStringLiteral("umount"), {p}).code != 0)
                run(QStringLiteral("umount"), {QStringLiteral("-l"), p});
        points.clear();
    }

    ~Mounts() { unmountAll(); }
};

struct CopyJob
{
    QString source;
    QString dest;
    qint64 size;
};

class Copier
{
public:
    void add(const QString &src, const QString &dst)
    {
        const qint64 size = QFileInfo(src).size();
        m_jobs.append({src, dst, size});
        m_total += size;
    }

    qint64 total() const { return m_total; }

    void run()
    {
        QElapsedTimer timer;
        timer.start();
        report();
        for (const CopyJob &job : std::as_const(m_jobs)) {
            emitLine(QStringLiteral("FILE ") + job.dest);
            copyFile(job, timer);
        }
        report();
    }

private:
    void report()
    {
        emitLine(QStringLiteral("PROGRESS %1 %2").arg(m_done).arg(m_total));
    }

    void copyFile(const CopyJob &job, QElapsedTimer &timer)
    {
        QDir().mkpath(QFileInfo(job.dest).absolutePath());
        QFile in(job.source);
        QFile out(job.dest);
        if (!in.open(QIODevice::ReadOnly))
            fail(QCoreApplication::translate("Helper", "No se pudo leer %1: %2").arg(job.source, in.errorString()));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(QCoreApplication::translate("Helper", "No se pudo escribir %1: %2").arg(job.dest, out.errorString()));

        constexpr qint64 chunk = 4 * MiB;
        constexpr qint64 syncEvery = 64 * MiB;
        QByteArray buffer(chunk, Qt::Uninitialized);
        qint64 unsynced = 0;
        while (true) {
            checkCancel();
            const qint64 n = in.read(buffer.data(), chunk);
            if (n < 0)
                fail(QCoreApplication::translate("Helper", "Error al leer %1: %2").arg(job.source, in.errorString()));
            if (n == 0)
                break;
            if (out.write(buffer.constData(), n) != n)
                fail(QCoreApplication::translate("Helper", "Error al escribir %1: %2").arg(job.dest, out.errorString()));
            m_done += n;
            unsynced += n;
            // Forzar la escritura periódicamente para que el progreso refleje
            // lo que realmente se ha grabado en la memoria USB
            if (unsynced >= syncEvery) {
                out.flush();
                ::fdatasync(out.handle());
                unsynced = 0;
            }
            if (timer.elapsed() > 250) {
                report();
                timer.restart();
            }
        }
        out.flush();
        if (::fdatasync(out.handle()) != 0)
            fail(QCoreApplication::translate("Helper", "Error al escribir %1").arg(job.dest));
    }

    QList<CopyJob> m_jobs;
    qint64 m_total = 0;
    qint64 m_done = 0;
};

// Busca una ruta sin distinguir mayúsculas/minúsculas (las ISO UDF distinguen)
QString findCi(const QString &root, const QString &relPath)
{
    QString current = root;
    for (const QString &part : relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QStringList entries = QDir(current).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        QString match;
        for (const QString &e : entries)
            if (e.compare(part, Qt::CaseInsensitive) == 0) {
                match = e;
                break;
            }
        if (match.isEmpty())
            return {};
        current += QLatin1Char('/') + match;
    }
    return current;
}

QString mkfsExfatLabelFlag()
{
    // exfatprogs usa -L, el antiguo exfat-utils usa -n
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("mkfs.exfat"), {QStringLiteral("-V")});
    p.waitForFinished(5000);
    return p.readAll().contains("exfatprogs") ? QStringLiteral("-L") : QStringLiteral("-n");
}

void work(const QString &isoPath, const QString &disk)
{
    // ---- Validaciones (antes de tocar nada) ----
    step(Step::CheckIso);
    const QFileInfo isoInfo(isoPath);
    if (!isoInfo.isFile())
        fail(QCoreApplication::translate("Helper", "El archivo «%1» no existe").arg(isoPath));

    const UsbDevice dev = UsbDevices::find(disk);
    if (dev.path.isEmpty())
        fail(QCoreApplication::translate("Helper", "%1 no es una unidad de almacenamiento USB").arg(disk));
    if (dev.size <= requiredBytes(isoInfo.size()))
        fail(QCoreApplication::translate("Helper", "La unidad es de %1 y se necesitan al menos %2")
                 .arg(UsbDevices::formatSize(dev.size), UsbDevices::formatSize(requiredBytes(isoInfo.size()))));

    QTemporaryDir work(QStringLiteral("/var/tmp/deepines-win-usb-installer-XXXXXX"));
    if (!work.isValid())
        fail(QCoreApplication::translate("Helper", "No se pudo crear la carpeta de trabajo"));
    // Nunca borrar recursivamente: si algo siguiera montado se borrarían sus archivos.
    work.setAutoRemove(false);
    struct DirCleanup
    {
        QStringList dirs;
        ~DirCleanup()
        {
            for (const QString &d : std::as_const(dirs))
                QDir().rmdir(d); // solo elimina carpetas vacías
        }
    } dirCleanup;
    const QString isoDir = work.filePath(QStringLiteral("iso"));
    const QString efiDir = work.filePath(QStringLiteral("efi"));
    const QString instDir = work.filePath(QStringLiteral("install"));
    for (const QString &d : {isoDir, efiDir, instDir})
        QDir().mkpath(d);
    dirCleanup.dirs = {isoDir, efiDir, instDir, work.path()};

    Mounts mounts;
    mounts.mount(isoInfo.absoluteFilePath(), isoDir, {QStringLiteral("-o"), QStringLiteral("loop,ro")});

    const QString bootWim = findCi(isoDir, QStringLiteral("sources/boot.wim"));
    const QString sourcesDir = findCi(isoDir, QStringLiteral("sources"));
    const bool hasInstall = !findCi(isoDir, QStringLiteral("sources/install.wim")).isEmpty()
        || !findCi(isoDir, QStringLiteral("sources/install.esd")).isEmpty()
        || !findCi(isoDir, QStringLiteral("sources/install.swm")).isEmpty();
    if (bootWim.isEmpty() || !hasInstall || findCi(isoDir, QStringLiteral("efi")).isEmpty())
        fail(QCoreApplication::translate("Helper", "«%1» no parece ser una ISO del instalador de Windows compatible con UEFI").arg(isoInfo.fileName()));
    checkCancel();

    // ---- Particionado ----
    step(Step::Unmount);
    unmountAll(disk);
    checkCancel();

    step(Step::Partition);
    run(QStringLiteral("wipefs"), {QStringLiteral("-a"), disk});
    const QByteArray layout = QStringLiteral(
                                  "label: gpt\n"
                                  "start=1MiB, size=%1MiB, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name=\"%2\"\n"
                                  "type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name=\"%3\"\n")
                                  .arg(EfiSizeMiB - 1)
                                  .arg(EfiLabel, InstallLabel)
                                  .toUtf8();
    runOrFail(QStringLiteral("sfdisk"),
              {QStringLiteral("--wipe"), QStringLiteral("always"), QStringLiteral("--wipe-partitions"),
               QStringLiteral("always"), disk},
              QCoreApplication::translate("Helper", "No se pudo crear la tabla de particiones en %1").arg(disk), layout);
    settle();

    QStringList parts;
    for (int attempt = 0; attempt < 10 && parts.size() != 2; ++attempt) {
        if (attempt > 0) {
            run(QStringLiteral("partprobe"), {disk});
            QThread::msleep(1000);
            settle();
        }
        parts = UsbDevices::partitions(disk);
    }
    if (parts.size() != 2)
        fail(QCoreApplication::translate("Helper", "El sistema no detectó las nuevas particiones de %1").arg(disk));
    checkCancel();

    // ---- Formateo ----
    step(Step::Format);
    unmountAll(disk);
    runOrFail(QStringLiteral("mkfs.vfat"), {QStringLiteral("-F"), QStringLiteral("32"), QStringLiteral("-n"), EfiLabel, parts[0]},
              QCoreApplication::translate("Helper", "No se pudo formatear %1").arg(parts[0]));
    unmountAll(disk);
    runOrFail(QStringLiteral("mkfs.exfat"), {mkfsExfatLabelFlag(), InstallLabel, parts[1]},
              QCoreApplication::translate("Helper", "No se pudo formatear %1").arg(parts[1]));
    settle();
    checkCancel();

    // ---- Montaje ----
    step(Step::Mount);
    unmountAll(disk);
    mounts.mount(parts[0], efiDir);
    mounts.mount(parts[1], instDir);

    // ---- Copia ----
    // EFI (FAT32): todo excepto sources/, más sources/boot.wim
    // INSTAL-WIN (exFAT): el contenido completo de la ISO (install.wim puede pasar de 4 GiB)
    step(Step::Copy);
    Copier copier;
    const QDir iso(isoDir);
    QDirIterator it(isoDir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = iso.relativeFilePath(src);
        if (!src.startsWith(sourcesDir + QLatin1Char('/')))
            copier.add(src, efiDir + QLatin1Char('/') + rel);
        copier.add(src, instDir + QLatin1Char('/') + rel);
    }
    copier.add(bootWim, efiDir + QLatin1Char('/') + iso.relativeFilePath(bootWim));
    copier.run();

    // ---- Final ----
    step(Step::Sync);
    ::sync();

    step(Step::Cleanup);
    mounts.unmountAll();
}

} // namespace

int runHelper(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::signal(SIGPIPE, SIG_IGN); // si la interfaz se cierra, terminar limpiando
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGHUP, onSignal);

    const QStringList args = app.arguments();
    const int idx = int(args.indexOf(HelperArg));
    // Idioma de la interfaz (argumento opcional tras el dispositivo): pkexec no
    // conserva LANG, así que lo envía la propia interfaz.
    installAppTranslator(idx >= 0 && args.size() > idx + 3 ? args.at(idx + 3)
                                                             : QLocale::system().name());
    if (idx < 0 || args.size() < idx + 3) {
        emitLine(QStringLiteral("ERROR ")
                 + QCoreApplication::translate("Helper", "Uso: %1 --helper <archivo.iso> <dispositivo> [idioma]")
                       .arg(args.value(0)));
        return 2;
    }
    if (::geteuid() != 0) {
        emitLine(QStringLiteral("ERROR ")
                 + QCoreApplication::translate("Helper", "Se requieren permisos de administrador"));
        return 2;
    }

    // Cancelación desde la interfaz: línea "cancel" o cierre de stdin
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line))
            if (line == "cancel")
                break;
        g_cancel = true;
    }).detach();

    try {
        work(args.at(idx + 1), args.at(idx + 2));
    } catch (const Failure &f) {
        settle();
        if (g_cancel && f.message == QLatin1String("cancelled")) {
            emitLine(QStringLiteral("ERROR cancelled"));
            return 3;
        }
        emitLine(QStringLiteral("ERROR ") + QString(f.message).replace(QLatin1Char('\n'), QStringLiteral(" | ")));
        return 1;
    }

    emitLine(QStringLiteral("DONE"));
    return 0;
}
