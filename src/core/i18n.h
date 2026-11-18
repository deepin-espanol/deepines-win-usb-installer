// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QTranslator>

namespace WinUsb {

/*
 * Instala la traducción embebida (:/translations/deepines-win-usb-installer_<idioma>.qm)
 * para el idioma indicado, por ejemplo "es_ES". Si no existe ni para el idioma
 * completo ni para su código de dos letras, se usa el inglés.
 * Los textos originales del código están en español.
 */
inline void installAppTranslator(const QString &locale = QLocale::system().name())
{
    // El helper (root) recibe el idioma como argumento: solo se aceptan
    // códigos como "es" o "es_ES".
    static const QRegularExpression valid(QStringLiteral("^[a-z]{2,3}(_[A-Z]{2})?$"));
    const QString lang = valid.match(locale).hasMatch() ? locale : QStringLiteral("en");

    static QTranslator translator;
    const QString base = QStringLiteral("deepines-win-usb-installer_");
    const QString dir = QStringLiteral(":/translations/");
    if (translator.load(base + lang, dir)
        || translator.load(base + lang.left(lang.indexOf(QLatin1Char('_'))), dir)
        || translator.load(base + QStringLiteral("en"), dir))
        QCoreApplication::installTranslator(&translator);
}

} // namespace WinUsb
