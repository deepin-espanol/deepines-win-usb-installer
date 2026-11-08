// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Modo privilegiado (ejecutado con pkexec): formatea la unidad y copia la ISO.
// Uso: deepines-win-usb-installer --helper <archivo.iso> <dispositivo>
int runHelper(int argc, char *argv[]);
