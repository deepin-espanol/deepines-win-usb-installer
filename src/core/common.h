// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <QString>

namespace WinUsb {

inline constexpr qint64 MiB = 1024 * 1024;

// Tamaño de la partición EFI (FAT32) que contiene los archivos de arranque y boot.wim
inline constexpr qint64 EfiSizeMiB = 860;
inline constexpr qint64 ExtraBytes = 10240;

inline const QString EfiLabel = QStringLiteral("EFI");
inline const QString InstallLabel = QStringLiteral("INSTAL-WIN");

inline const QString HelperArg = QStringLiteral("--helper");

// Bytes necesarios en la unidad USB para una ISO de un tamaño dado
inline qint64 requiredBytes(qint64 isoSize)
{
    return isoSize + EfiSizeMiB * MiB + ExtraBytes;
}

/*
 * Protocolo de salida del proceso auxiliar (stdout, una línea por mensaje):
 *   STEP <código>
 *   PROGRESS <bytes copiados> <bytes totales>
 *   FILE <ruta relativa>
 *   LOG <texto>
 *   ERROR <texto>
 *   DONE
 * Entrada (stdin): la línea "cancel" o el cierre de stdin cancelan el proceso.
 */
namespace Step {
inline const QString CheckIso = QStringLiteral("check-iso");
inline const QString Unmount = QStringLiteral("unmount");
inline const QString Partition = QStringLiteral("partition");
inline const QString Format = QStringLiteral("format");
inline const QString Mount = QStringLiteral("mount");
inline const QString Copy = QStringLiteral("copy");
inline const QString Sync = QStringLiteral("sync");
inline const QString Cleanup = QStringLiteral("cleanup");
}

} // namespace WinUsb
