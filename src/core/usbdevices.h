// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct UsbDevice
{
    QString path;      // /dev/sdX
    QString model;
    QString vendor;
    qint64 size = 0;
    QStringList labels;      // etiquetas de las particiones
    QStringList mountPoints; // puntos de montaje activos

    QString displayName() const;
};

namespace UsbDevices {
// Lista los discos completos conectados por USB
QList<UsbDevice> list();
// Devuelve el dispositivo si `path` es un disco USB, o un UsbDevice vacío
UsbDevice find(const QString &path);
// Rutas de las particiones de un disco, en orden
QStringList partitions(const QString &diskPath);
// Puntos de montaje de un disco y sus particiones
QStringList mountPoints(const QString &diskPath);

QString formatSize(qint64 bytes);
}
