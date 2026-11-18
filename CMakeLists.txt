cmake_minimum_required(VERSION 3.16)

project(deepines-win-usb-installer VERSION 0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(Dtk6 REQUIRED COMPONENTS Core Gui Widget)

# Traducciones: los textos originales están en español; cada src/translations/*.ts
# se compila con lrelease y se embebe en el ejecutable bajo :/translations/.
# Para actualizar los .ts tras cambiar textos:
#   lupdate src -ts src/translations/*.ts
find_program(LRELEASE NAMES lrelease HINTS /usr/lib/qt6/bin REQUIRED)
file(GLOB TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/translations/*.ts)
set(QM_FILES "")
set(QRC_ENTRIES "")
foreach(TS_FILE ${TS_FILES})
    get_filename_component(TS_NAME ${TS_FILE} NAME_WE)
    set(QM_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TS_NAME}.qm")
    add_custom_command(
        OUTPUT ${QM_FILE}
        COMMAND ${LRELEASE} -silent ${TS_FILE} -qm ${QM_FILE}
        DEPENDS ${TS_FILE}
        COMMENT "Compilando ${TS_NAME}.ts"
    )
    list(APPEND QM_FILES ${QM_FILE})
    string(APPEND QRC_ENTRIES "        <file>${TS_NAME}.qm</file>\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
    "<RCC>\n    <qresource prefix=\"/translations\">\n${QRC_ENTRIES}    </qresource>\n</RCC>\n")

add_executable(${PROJECT_NAME}
    src/main.cpp

    # Núcleo compartido: constantes y detección de unidades USB
    src/core/common.h
    src/core/i18n.h
    src/core/usbdevices.h
    src/core/usbdevices.cpp

    # Modo privilegiado (pkexec): particionado, formateo y copia
    src/helper/helper.h
    src/helper/helper.cpp

    # Interfaz gráfica DTK
    src/gui/mainwindow.h
    src/gui/mainwindow.cpp

    data/resources.qrc
    ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc
    ${QM_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus
    Dtk6::Core Dtk6::Gui Dtk6::Widget
)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/applications/deepines-win-usb-installer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/icons/deepines-win-usb-installer.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/polkit/org.deepines.win-usb-installer.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
