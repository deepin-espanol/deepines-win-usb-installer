// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/usbdevices.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>

namespace {

QJsonArray runLsblk(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("lsblk"), QStringList{QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("--tree")} + args);
    if (!p.waitForFinished(10000) || p.exitCode() != 0)
        return {};
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(QStringLiteral("blockdevices")).toArray();
}

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString().trimmed();
}

qint64 num(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    // Según la versión de lsblk el tamaño puede venir como número o como texto
    return v.isString() ? v.toString().toLongLong() : static_cast<qint64>(v.toDouble());
}

void collect(const QJsonObject &node, UsbDevice &dev)
{
    for (const QJsonValue &mp : node.value(QStringLiteral("mountpoints")).toArray()) {
        if (!mp.toString().isEmpty())
            dev.mountPoints << mp.toString();
    }
    if (str(node, "type") == QLatin1String("part") && !str(node, "label").isEmpty())
        dev.labels << str(node, "label");
    for (const QJsonValue &child : node.value(QStringLiteral("children")).toArray())
        collect(child.toObject(), dev);
}

QList<UsbDevice> query(const QStringList &extra)
{
    const QJsonArray devices = runLsblk(QStringList{QStringLiteral("-o"),
                                                    QStringLiteral("PATH,SIZE,MODEL,VENDOR,TRAN,TYPE,LABEL,MOUNTPOINTS")}
                                        + extra);
    QList<UsbDevice> result;
    for (const QJsonValue &v : devices) {
        const QJsonObject o = v.toObject();
        if (str(o, "type") != QLatin1String("disk") || str(o, "tran") != QLatin1String("usb"))
            continue;
        UsbDevice dev;
        dev.path = str(o, "path");
        dev.model = str(o, "model");
        dev.vendor = str(o, "vendor");
        dev.size = num(o, "size");
        if (dev.size <= 0) // lector de tarjetas vacío, etc.
            continue;
        collect(o, dev);
        result << dev;
    }
    return result;
}

} // namespace

QString UsbDevice::displayName() const
{
    QString name = QStringList{vendor, model}.join(QLatin1Char(' ')).simplified();
    if (name.isEmpty())
        name = QStringLiteral("USB");
    return name;
}

QList<UsbDevice> UsbDevices::list()
{
    return query({});
}

UsbDevice UsbDevices::find(const QString &path)
{
    const QList<UsbDevice> devs = query({path});
    return devs.isEmpty() ? UsbDevice{} : devs.first();
}

QStringList UsbDevices::partitions(const QString &diskPath)
{
    QStringList parts;
    const QJsonArray devices = runLsblk({QStringLiteral("-o"), QStringLiteral("PATH,TYPE"), diskPath});
    for (const QJsonValue &v : devices) {
        for (const QJsonValue &c : v.toObject().value(QStringLiteral("children")).toArray()) {
            const QJsonObject o = c.toObject();
            if (str(o, "type") == QLatin1String("part"))
                parts << str(o, "path");
        }
    }
    return parts;
}

QStringList UsbDevices::mountPoints(const QString &diskPath)
{
    UsbDevice dev;
    for (const QJsonValue &v : runLsblk({QStringLiteral("-o"), QStringLiteral("PATH,TYPE,LABEL,MOUNTPOINTS"), diskPath}))
        collect(v.toObject(), dev);
    return dev.mountPoints;
}

QString UsbDevices::formatSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
}
