// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/common.h"
#include "helper/helper.h"
#include "gui/mainwindow.h"

#include <DApplication>
#include <DGuiApplicationHelper>
#include <DWidgetUtil>

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
    app.setApplicationDisplayName(QObject::tr("Instalador USB de Windows"));
    app.setProductName(QObject::tr("Instalador USB de Windows"));
    const QIcon icon = QIcon::fromTheme(QStringLiteral("deepines-win-usb-installer"),
                                        QIcon(QStringLiteral(":/icons/deepines-win-usb-installer.svg")));
    app.setProductIcon(icon);
    app.setWindowIcon(icon);
    app.setApplicationDescription(QObject::tr("Crea una memoria USB de instalación de Windows 10/11 "
                                              "para equipos UEFI a partir del archivo .iso oficial."));
    app.setApplicationHomePage(QStringLiteral("https://github.com/igatjens/deepines-win-usb-installer"));
    app.setApplicationLicense(QStringLiteral("GPL-3.0-or-later"));
    app.loadTranslator();

    if (!DGuiApplicationHelper::setSingleInstance(app.applicationName()))
        return 0;

    MainWindow window;
    window.show();
    Dtk::Widget::moveToCenter(&window);
    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance,
                     &window, [&window] { window.activateWindow(); window.raise(); });

    return app.exec();
}
