// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mainwindow.h"

#include "core/common.h"
#include "core/usbdevices.h"

#include <DDialog>
#include <DFileChooserEdit>
#include <DFontSizeManager>
#include <DFrame>
#include <DIconButton>
#include <DLabel>
#include <DListView>
#include <DMessageManager>
#include <DPushButton>
#include <DStyledItemDelegate>
#include <DSuggestButton>
#include <DTipLabel>
#include <DTitlebar>
#include <DWaterProgress>

#include <QCloseEvent>
#include <QDBusConnection>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDir>
#include <QHBoxLayout>
#include <QLocale>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace WinUsb;

namespace {

enum Page { WelcomePage, SelectPage, ProgressPage, ResultPage };
enum Roles { PathRole = Qt::UserRole + 1, SizeRole };

// Comprueba la firma ISO 9660 / UDF en el sector 16
bool looksLikeIso(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(16 * 2048 + 1))
        return false;
    const QByteArray id = f.read(5);
    return id == "CD001" || id == "BEA01" || id == "NSR02" || id == "NSR03";
}

DLabel *titleLabel(const QString &text, DFontSizeManager::SizeType size = DFontSizeManager::T4)
{
    auto *label = new DLabel(text);
    DFontSizeManager::instance()->bind(label, size, QFont::DemiBold);
    label->setWordWrap(true);
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(parent)
{
    setWindowTitle(tr("Instalador USB de Windows"));
    setMinimumSize(620, 520);
    resize(680, 580);
    setAcceptDrops(true);

    titlebar()->setIcon(QIcon::fromTheme(QStringLiteral("deepines-win-usb-installer"),
                                         QIcon(QStringLiteral(":/icons/deepines-win-usb-installer.svg"))));
    titlebar()->setTitle(QString());

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(createWelcomePage());
    m_stack->addWidget(createSelectPage());
    m_stack->addWidget(createProgressPage());
    m_stack->addWidget(createResultPage());
    setCentralWidget(m_stack);

    // Actualizar la lista cuando se conecta o desconecta una unidad
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(700);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshDevices);

    QDBusConnection bus = QDBusConnection::systemBus();
    const QString service = QStringLiteral("org.freedesktop.UDisks2");
    const QString objPath = QStringLiteral("/org/freedesktop/UDisks2");
    const QString iface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
    bus.connect(service, objPath, iface, QStringLiteral("InterfacesAdded"), this, SLOT(scheduleRefresh()));
    bus.connect(service, objPath, iface, QStringLiteral("InterfacesRemoved"), this, SLOT(scheduleRefresh()));
}

// ---------------------------------------------------------------------------
// Páginas

QWidget *MainWindow::createWelcomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 10, 40, 30);

    auto *icon = new DLabel;
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("deepines-win-usb-installer"),
                                     QIcon(QStringLiteral(":/icons/deepines-win-usb-installer.svg")))
                        .pixmap(96, 96));
    icon->setAlignment(Qt::AlignCenter);

    auto *title = titleLabel(tr("Instalador USB de Windows"), DFontSizeManager::T3);
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new DTipLabel(tr("Graba el instalador de Windows 10 u 11 en una memoria USB "
                                      "a partir del archivo .iso oficial."));
    subtitle->setWordWrap(true);
    subtitle->setAlignment(Qt::AlignCenter);

    auto *frame = new DFrame;
    auto *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(20, 14, 20, 14);
    auto *info = new DLabel;
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    info->setOpenExternalLinks(true);
    info->setText(tr("<b>Aclaraciones</b>"
                     "<ul style='margin-left:-20px'>"
                     "<li>Compatible únicamente con UEFI.</li>"
                     "<li>Compatible con Secure Boot (configuración de fábrica).</li>"
                     "<li>No es compatible con modo Legacy, CSM o BIOS.</li>"
                     "<li>Si su computadora tiene Secure Boot, entonces tiene UEFI.</li>"
                     "</ul>"
                     "<p>Más información sobre la diferencia entre UEFI y BIOS: "
                     "<a href='https://es.wikipedia.org/wiki/Unified_Extensible_Firmware_Interface'>Wikipedia</a></p>"
                     "<p>Puede descargar Windows desde el "
                     "<a href='https://www.microsoft.com/es-es/software-download/'>sitio oficial de Microsoft</a>.</p>"));
    frameLayout->addWidget(info);

    auto *start = new DSuggestButton(tr("Comenzar"));
    start->setMinimumWidth(200);
    connect(start, &QPushButton::clicked, this, [this] {
        refreshDevices();
        m_stack->setCurrentIndex(SelectPage);
    });

    layout->addStretch();
    layout->addWidget(icon);
    layout->addSpacing(8);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(16);
    layout->addWidget(frame);
    layout->addStretch();
    layout->addWidget(start, 0, Qt::AlignHCenter);
    return page;
}

QWidget *MainWindow::createSelectPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 10, 30, 30);
    layout->setSpacing(8);

    // ISO
    layout->addWidget(titleLabel(tr("1. Archivo ISO del instalador de Windows"), DFontSizeManager::T5));
    m_isoEdit = new DFileChooserEdit;
    m_isoEdit->setPlaceholderText(tr("Seleccione o arrastre aquí un archivo .iso"));
    m_isoEdit->setNameFilters({tr("Imágenes ISO (*.iso *.ISO)")});
    m_isoEdit->setDirectoryUrl(QUrl::fromLocalFile(QDir::homePath() + QStringLiteral("/Descargas")));
    m_isoEdit->lineEdit()->setReadOnly(true);
    connect(m_isoEdit, &DFileChooserEdit::fileChoosed, this, &MainWindow::setIsoFile);
    layout->addWidget(m_isoEdit);
    m_isoInfo = new DTipLabel;
    m_isoInfo->setAlignment(Qt::AlignLeft);
    layout->addWidget(m_isoInfo);

    layout->addSpacing(10);

    // Unidad USB
    auto *usbHeader = new QHBoxLayout;
    usbHeader->addWidget(titleLabel(tr("2. Unidad de almacenamiento USB"), DFontSizeManager::T5), 1);
    auto *refresh = new DIconButton(this);
    refresh->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    refresh->setFlat(true);
    refresh->setToolTip(tr("Actualizar"));
    connect(refresh, &DIconButton::clicked, this, &MainWindow::refreshDevices);
    usbHeader->addWidget(refresh);
    layout->addLayout(usbHeader);

    m_deviceModel = new QStandardItemModel(this);
    m_deviceView = new DListView;
    m_deviceView->setModel(m_deviceModel);
    m_deviceView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceView->setIconSize(QSize(40, 40));
    m_deviceView->setItemSpacing(6);
    m_deviceView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_deviceView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::validateSelection);

    m_noDevices = new DLabel(tr("No hay unidades de almacenamiento USB disponibles.\n"
                                "Conecte una memoria USB."));
    m_noDevices->setAlignment(Qt::AlignCenter);
    m_noDevices->setForegroundRole(QPalette::PlaceholderText);

    auto *listStack = new QStackedWidget;
    listStack->addWidget(m_deviceView);
    listStack->addWidget(m_noDevices);
    connect(m_deviceModel, &QStandardItemModel::rowsInserted, listStack, [=] { listStack->setCurrentIndex(0); });
    connect(m_deviceModel, &QStandardItemModel::modelReset, listStack, [=] {
        listStack->setCurrentIndex(m_deviceModel->rowCount() ? 0 : 1);
    });
    connect(m_deviceModel, &QStandardItemModel::rowsRemoved, listStack, [=] {
        listStack->setCurrentIndex(m_deviceModel->rowCount() ? 0 : 1);
    });
    listStack->setCurrentIndex(1);
    layout->addWidget(listStack, 1);

    m_selectHint = new DTipLabel;
    m_selectHint->setAlignment(Qt::AlignCenter);
    m_selectHint->setWordWrap(true);
    layout->addWidget(m_selectHint);

    auto *buttons = new QHBoxLayout;
    auto *back = new DPushButton(tr("Atrás"));
    connect(back, &QPushButton::clicked, this, [this] { m_stack->setCurrentIndex(WelcomePage); });
    m_startButton = new DSuggestButton(tr("Crear instalador"));
    m_startButton->setEnabled(false);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startWriting);
    back->setMinimumWidth(140);
    m_startButton->setMinimumWidth(180);
    buttons->addStretch();
    buttons->addWidget(back);
    buttons->addSpacing(10);
    buttons->addWidget(m_startButton);
    buttons->addStretch();
    layout->addSpacing(6);
    layout->addLayout(buttons);
    return page;
}

QWidget *MainWindow::createProgressPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 10, 30, 30);

    m_water = new DWaterProgress;
    m_water->setFixedSize(120, 120);
    m_water->setValue(0);
    m_water->setTextVisible(true);

    m_stepLabel = titleLabel(QString(), DFontSizeManager::T5);
    m_stepLabel->setAlignment(Qt::AlignCenter);
    m_detailLabel = new DTipLabel;
    m_detailLabel->setAlignment(Qt::AlignCenter);
    m_detailLabel->setWordWrap(true);

    auto *warning = new DTipLabel(tr("No desconecte la unidad USB hasta que el proceso termine."));
    warning->setAlignment(Qt::AlignCenter);
    warning->setWordWrap(true);

    m_logToggle = new QToolButton;
    m_logToggle->setText(tr("Mostrar detalles"));
    m_logToggle->setCheckable(true);
    m_logToggle->setAutoRaise(true);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setVisible(false);
    connect(m_logToggle, &QToolButton::toggled, this, [this](bool on) {
        m_log->setVisible(on);
        m_logToggle->setText(on ? tr("Ocultar detalles") : tr("Mostrar detalles"));
    });

    m_cancelButton = new DPushButton(tr("Cancelar"));
    m_cancelButton->setMinimumWidth(160);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        if (!isWorking())
            return;
        m_cancelRequested = true;
        m_cancelButton->setEnabled(false);
        m_stepLabel->setText(tr("Cancelando…"));
        m_helper->write("cancel\n");
    });

    layout->addStretch();
    layout->addWidget(m_water, 0, Qt::AlignHCenter);
    layout->addSpacing(16);
    layout->addWidget(m_stepLabel);
    layout->addWidget(m_detailLabel);
    layout->addSpacing(6);
    layout->addWidget(warning);
    layout->addStretch();
    layout->addWidget(m_logToggle, 0, Qt::AlignHCenter);
    layout->addWidget(m_log, 2);
    layout->addSpacing(8);
    layout->addWidget(m_cancelButton, 0, Qt::AlignHCenter);
    return page;
}

QWidget *MainWindow::createResultPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 10, 40, 30);

    m_resultIcon = new DLabel;
    m_resultIcon->setAlignment(Qt::AlignCenter);
    m_resultTitle = titleLabel(QString(), DFontSizeManager::T3);
    m_resultTitle->setAlignment(Qt::AlignCenter);
    m_resultText = new DTipLabel;
    m_resultText->setAlignment(Qt::AlignCenter);
    m_resultText->setWordWrap(true);
    m_resultText->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *again = new DPushButton(tr("Volver al inicio"));
    again->setMinimumWidth(160);
    connect(again, &QPushButton::clicked, this, [this] {
        refreshDevices();
        m_stack->setCurrentIndex(SelectPage);
    });
    auto *quit = new DSuggestButton(tr("Salir"));
    quit->setMinimumWidth(160);
    connect(quit, &QPushButton::clicked, this, &QWidget::close);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(again);
    buttons->addSpacing(10);
    buttons->addWidget(quit);
    buttons->addStretch();

    layout->addStretch();
    layout->addWidget(m_resultIcon);
    layout->addSpacing(12);
    layout->addWidget(m_resultTitle);
    layout->addWidget(m_resultText);
    layout->addStretch();
    layout->addLayout(buttons);
    return page;
}

// ---------------------------------------------------------------------------
// Selección de ISO y unidad

void MainWindow::setIsoFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        DMessageManager::instance()->sendMessage(this, QIcon::fromTheme(QStringLiteral("dialog-warning")),
                                                 tr("El archivo «%1» no existe").arg(path));
        return;
    }
    if (!looksLikeIso(path)) {
        DMessageManager::instance()->sendMessage(this, QIcon::fromTheme(QStringLiteral("dialog-warning")),
                                                 tr("«%1» no es un archivo ISO válido").arg(fi.fileName()));
        m_isoEdit->setText(QString());
        m_isoInfo->clear();
        validateSelection();
        return;
    }
    m_isoEdit->setText(fi.absoluteFilePath());
    m_isoInfo->setText(tr("%1 · %2").arg(fi.fileName(), UsbDevices::formatSize(fi.size())));
    refreshDevices();
}

qint64 MainWindow::isoSize() const
{
    return m_isoEdit->text().isEmpty() ? 0 : QFileInfo(m_isoEdit->text()).size();
}

QString MainWindow::selectedDevice() const
{
    const QModelIndexList sel = m_deviceView->selectionModel()->selectedIndexes();
    return sel.isEmpty() ? QString() : sel.first().data(PathRole).toString();
}

void MainWindow::scheduleRefresh()
{
    if (!isWorking())
        m_refreshTimer->start();
}

void MainWindow::refreshDevices()
{
    const QString previous = selectedDevice();
    const qint64 needed = isoSize() ? requiredBytes(isoSize()) : 0;

    m_deviceModel->clear();
    const QList<UsbDevice> devices = UsbDevices::list();
    for (const UsbDevice &d : devices) {
        auto *item = new DStandardItem(QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                                                        QIcon::fromTheme(QStringLiteral("drive-removable-media"))),
                                       d.displayName());
        QString detail = QStringLiteral("%1 · %2").arg(d.path, UsbDevices::formatSize(d.size));
        if (!d.labels.isEmpty())
            detail += QStringLiteral(" · ") + d.labels.join(QStringLiteral(", "));
        const bool tooSmall = needed && d.size <= needed;
        if (tooSmall)
            detail += QStringLiteral(" · ") + tr("insuficiente");

        auto *action = new DViewItemAction(Qt::AlignLeft | Qt::AlignVCenter, QSize(), QSize(), false);
        action->setText(detail);
        action->setFontSize(DFontSizeManager::T8);
        action->setTextColorRole(DPalette::TextTips);
        item->setTextActionList({action});

        item->setData(d.path, PathRole);
        item->setData(d.size, SizeRole);
        item->setSizeHint(QSize(0, 60));
        item->setToolTip(detail);
        m_deviceModel->appendRow(item);
        if (d.path == previous)
            m_deviceView->setCurrentIndex(item->index());
    }
    if (devices.size() == 1 && !m_deviceView->currentIndex().isValid())
        m_deviceView->setCurrentIndex(m_deviceModel->index(0, 0));
    validateSelection();
}

void MainWindow::validateSelection()
{
    const QModelIndexList sel = m_deviceView->selectionModel()->selectedIndexes();
    const qint64 iso = isoSize();
    QString hint;
    bool ok = false;

    if (!iso) {
        hint = tr("Seleccione el archivo ISO del instalador de Windows.");
    } else if (sel.isEmpty()) {
        hint = m_deviceModel->rowCount() ? tr("Seleccione una de las unidades USB.") : QString();
    } else {
        const qint64 size = sel.first().data(SizeRole).toLongLong();
        if (size <= requiredBytes(iso)) {
            hint = tr("La unidad seleccionada es de %1 y se necesitan al menos %2. "
                      "Utilice una unidad de almacenamiento USB más grande.")
                       .arg(UsbDevices::formatSize(size), UsbDevices::formatSize(requiredBytes(iso)));
        } else {
            hint = tr("Se borrarán todos los archivos de %1.").arg(sel.first().data(PathRole).toString());
            ok = true;
        }
    }
    m_selectHint->setText(hint);
    m_startButton->setEnabled(ok);
}

// ---------------------------------------------------------------------------
// Proceso de escritura

bool MainWindow::confirmErase(const QString &device)
{
    const UsbDevice dev = UsbDevices::find(device);
    if (dev.path.isEmpty()) {
        DMessageManager::instance()->sendMessage(this, QIcon::fromTheme(QStringLiteral("dialog-warning")),
                                                 tr("La unidad %1 ya no está disponible").arg(device));
        refreshDevices();
        return false;
    }

    DDialog dialog(this);
    dialog.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    dialog.setTitle(tr("Se borrarán todos los archivos de %1").arg(dev.path));
    QString text = tr("%1 (%2)").arg(dev.displayName(), UsbDevices::formatSize(dev.size));
    if (!dev.labels.isEmpty())
        text += QStringLiteral("\n") + tr("Particiones: %1").arg(dev.labels.join(QStringLiteral(", ")));
    text += QStringLiteral("\n\n") + tr("Este procedimiento de borrado no se puede deshacer.");
    dialog.setMessage(text);
    dialog.addButton(tr("Cancelar"), false, DDialog::ButtonNormal);
    dialog.addButton(tr("Borrar y continuar"), true, DDialog::ButtonWarning);
    return dialog.exec() == 1;
}

void MainWindow::startWriting()
{
    const QString iso = m_isoEdit->text();
    const QString device = selectedDevice();
    if (iso.isEmpty() || device.isEmpty() || !confirmErase(device))
        return;

    m_device = device;
    m_buffer.clear();
    m_lastError.clear();
    m_cancelRequested = false;
    m_done = false;
    m_log->clear();
    m_water->setValue(0);
    m_water->start();
    m_stepLabel->setText(tr("Esperando autorización…"));
    m_detailLabel->setText(QString());
    m_cancelButton->setEnabled(true);
    m_stack->setCurrentIndex(ProgressPage);

    m_helper = new QProcess(this);
    m_helper->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_helper, &QProcess::readyReadStandardOutput, this, &MainWindow::readHelperOutput);
    connect(m_helper, &QProcess::readyReadStandardError, this, [this] {
        appendLog(QString::fromUtf8(m_helper->readAllStandardError()).trimmed());
    });
    connect(m_helper, &QProcess::finished, this, &MainWindow::helperFinished);
    connect(m_helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            helperFinished(-1, QProcess::CrashExit);
    });

    // pkexec limpia el entorno (LANG, LANGUAGE…), así que el idioma se pasa
    // como argumento para que los mensajes del helper salgan traducidos.
    const QStringList args{QCoreApplication::applicationFilePath(), HelperArg, iso, device,
                           QLocale::system().name()};
    appendLog(QStringLiteral("pkexec ") + args.join(QLatin1Char(' ')));
    m_helper->start(QStringLiteral("pkexec"), args);
}

bool MainWindow::isWorking() const
{
    return m_helper && m_helper->state() != QProcess::NotRunning;
}

void MainWindow::readHelperOutput()
{
    m_buffer += QString::fromUtf8(m_helper->readAllStandardOutput());
    qsizetype nl;
    while ((nl = m_buffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        handleLine(line);
    }
}

void MainWindow::handleLine(const QString &line)
{
    const qsizetype sp = line.indexOf(QLatin1Char(' '));
    const QString kind = sp < 0 ? line : line.left(sp);
    const QString value = sp < 0 ? QString() : line.mid(sp + 1);

    if (kind == QLatin1String("STEP")) {
        const QHash<QString, QString> steps{
            {Step::CheckIso, tr("Comprobando la imagen ISO…")},
            {Step::Unmount, tr("Desmontando %1…").arg(m_device)},
            {Step::Partition, tr("Creando tabla de particiones GPT…")},
            {Step::Format, tr("Formateando particiones %1 y %2…").arg(EfiLabel, InstallLabel)},
            {Step::Mount, tr("Montando particiones…")},
            {Step::Copy, tr("Copiando archivos…")},
            {Step::Sync, tr("Escribiendo datos en la unidad USB…")},
            {Step::Cleanup, tr("Desmontando unidades…")},
        };
        if (!m_cancelRequested)
            m_stepLabel->setText(steps.value(value, value));
        if (value == Step::Copy)
            m_copyTimer.start();
        appendLog(QStringLiteral("==> ") + steps.value(value, value));
    } else if (kind == QLatin1String("PROGRESS")) {
        const QStringList parts = value.split(QLatin1Char(' '));
        const qint64 done = parts.value(0).toLongLong();
        const qint64 total = parts.value(1).toLongLong();
        if (total > 0) {
            m_water->setValue(int(done * 100 / total));
            QString detail = tr("%1 de %2").arg(UsbDevices::formatSize(done), UsbDevices::formatSize(total));
            const qint64 ms = m_copyTimer.isValid() ? m_copyTimer.elapsed() : 0;
            if (ms > 2000 && done > 0) {
                const qint64 speed = done * 1000 / ms;
                const qint64 remaining = speed ? (total - done) / speed : 0;
                detail += QStringLiteral(" · %1/s").arg(UsbDevices::formatSize(speed));
                if (remaining > 0)
                    detail += QStringLiteral(" · ")
                        + tr("%1 min restantes").arg(qMax<qint64>(1, (remaining + 59) / 60));
            }
            m_detailLabel->setText(detail);
        }
    } else if (kind == QLatin1String("FILE")) {
        appendLog(value);
    } else if (kind == QLatin1String("LOG")) {
        appendLog(value);
    } else if (kind == QLatin1String("ERROR")) {
        m_lastError = QString(value).replace(QStringLiteral(" | "), QStringLiteral("\n"));
        appendLog(QStringLiteral("** Error: ") + m_lastError);
    } else if (kind == QLatin1String("DONE")) {
        m_done = true;
    } else if (!line.isEmpty()) {
        appendLog(line);
    }
}

void MainWindow::appendLog(const QString &text)
{
    if (!text.isEmpty())
        m_log->appendPlainText(text);
}

void MainWindow::helperFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_helper)
        return;
    readHelperOutput();
    m_water->stop();
    m_helper->deleteLater();
    m_helper = nullptr;

    if (m_done && exitCode == 0 && status == QProcess::NormalExit) {
        m_water->setValue(100);
        showResult(true, tr("El instalador de Windows se grabó correctamente en %1.\n"
                            "Ya puede retirar la unidad USB y arrancar la computadora desde ella en modo UEFI.")
                             .arg(m_device));
    } else if (m_cancelRequested || m_lastError == QLatin1String("cancelled")) {
        showResult(false, tr("Procedimiento cancelado. La unidad %1 puede haber quedado incompleta; "
                             "vuelva a crear el instalador antes de usarla.")
                              .arg(m_device));
    } else if (exitCode == 126 || exitCode == 127) {
        // Códigos de pkexec: autorización denegada o cancelada
        showResult(false, tr("No se obtuvieron los permisos de administrador necesarios."));
    } else {
        showResult(false, m_lastError.isEmpty()
                              ? tr("El proceso terminó inesperadamente (código %1).").arg(exitCode)
                              : m_lastError);
    }
}

void MainWindow::showResult(bool success, const QString &message)
{
    m_resultIcon->setPixmap(QIcon::fromTheme(success ? QStringLiteral("dialog-ok") : QStringLiteral("dialog-error"))
                                .pixmap(96, 96));
    m_resultTitle->setText(success ? tr("Proceso finalizado") : tr("No se pudo crear el instalador"));
    m_resultText->setText(message);
    m_stack->setCurrentIndex(ResultPage);
}

// ---------------------------------------------------------------------------
// Eventos

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!isWorking()) {
        event->accept();
        return;
    }
    DDialog dialog(this);
    dialog.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    dialog.setTitle(tr("El instalador se está grabando"));
    dialog.setMessage(tr("Si cancela ahora, la unidad USB quedará incompleta."));
    dialog.addButton(tr("Continuar grabando"), true, DDialog::ButtonRecommend);
    dialog.addButton(tr("Cancelar y salir"), false, DDialog::ButtonWarning);
    if (dialog.exec() != 1 || !isWorking()) {
        event->ignore();
        return;
    }
    // Pedir al proceso auxiliar que se detenga y desmonte todo antes de salir
    m_cancelRequested = true;
    m_helper->write("cancel\n");
    m_helper->closeWriteChannel();
    m_stepLabel->setText(tr("Cancelando…"));
    m_helper->waitForFinished(60000);
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_stack->currentIndex() > SelectPage || !event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".iso"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile()) {
            m_stack->setCurrentIndex(SelectPage);
            setIsoFile(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
}
