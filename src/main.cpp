// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/common.h"
#include "core/i18n.h"
#include "helper/helper.h"
#include "gui/mainwindow.h"

#include <DAboutDialog>
#include <DApplication>
#include <DGuiApplicationHelper>
#include <DWidgetUtil>

#include <QLabel>
#include <QPixmap>

#include <cstring>

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

int main(int argc, char *argv[])
{
    // Modo privilegiado, lanzado por la propia interfaz mediante pkexec
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--helper") == 0)
            return runHelper(argc, argv);

    DApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("deepines"));
    app.setApplicationName(QStringLiteral("deepines-win-usb-installer"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    // Traducciones de DTK y de la aplicación, antes de cualquier tr()
    app.loadTranslator();
    WinUsb::installAppTranslator();
    app.setApplicationDisplayName(QObject::tr("Instalador USB de Windows"));
    app.setProductName(QObject::tr("Instalador USB de Windows"));
    const QIcon icon = QIcon::fromTheme(QStringLiteral("deepines-win-usb-installer"),
                                        QIcon(QStringLiteral(":/icons/deepines-win-usb-installer.svg")));
    app.setProductIcon(icon);
    app.setWindowIcon(icon);
    app.setApplicationDescription(QObject::tr("Crea una memoria USB de instalación de Windows 10/11 "
                                              "para equipos UEFI a partir del archivo .iso oficial."));
    app.setApplicationHomePage(QStringLiteral("https://github.com/deepin-espanol/deepines-win-usb-installer"));
    app.setApplicationLicense(QStringLiteral("GPL-3.0-or-later"));

    if (!DGuiApplicationHelper::setSingleInstance(app.applicationName()))
        return 0;

    MainWindow window;

    // Diálogo «Acerca de» con el logo y la web de la comunidad Deepin en Español
    auto *about = new DAboutDialog(&window);
    about->setWindowTitle(app.productName());
    about->setProductIcon(icon);
    about->setProductName(app.productName());
    about->setVersion(app.applicationVersion());
    about->setDescription(
        app.applicationDescription() +
        QStringLiteral("<br/><br/>"
                       "<span style='font-size:8pt; font-weight:600;'>Deepin en Español</span><br/>"
                       "<span style='font-size:8pt; font-weight:600;'>Deepin Latin Code - developers</span>"));
    about->setLicense(QObject::tr("Licencia: %1").arg(app.applicationLicense()));
    about->setWebsiteName(QStringLiteral("deepinenespañol.org"));
    about->setWebsiteLink(QStringLiteral("https://deepinenespañol.org"));

    // Logo de la comunidad bajo el nombre de la app. DTK oculta siempre la etiqueta
    // del logo de empresa y fija el diálogo en 540x290, así que se muestra a mano y
    // se agranda el diálogo para que quepa.
    const qreal dpr = window.devicePixelRatioF();
    QPixmap logo = QPixmap(QStringLiteral(":/images/Logo-Racoon.png"))
                       .scaledToWidth(qRound(150 * dpr), Qt::SmoothTransformation);
    logo.setDevicePixelRatio(dpr);
    about->setCompanyLogo(logo);
    for (QLabel *label : about->findChildren<QLabel *>()) {
        if (label->pixmap().cacheKey() == logo.cacheKey()) {
            label->setAlignment(Qt::AlignCenter);
            label->show();
        }
    }
    about->setFixedSize(540, 400);
    app.setAboutDialog(about);

    window.show();
    Dtk::Widget::moveToCenter(&window);
    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance,
                     &window, [&window] { window.activateWindow(); window.raise(); });

    return app.exec();
}
