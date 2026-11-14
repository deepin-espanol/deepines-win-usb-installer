# deepines-win-usb-installer

Creador de memorias USB de instalación de Windows 10/11 para equipos **UEFI**
(compatible con Secure Boot), escrito en C++ con **Qt6** y **DTK6**.

## Cómo funciona

1. Seleccione (o arrastre a la ventana) el archivo `.iso` oficial de Windows.
2. Seleccione la unidad USB (la lista se actualiza sola al conectar/desconectar).
3. Confirme el borrado y autentíquese (polkit/pkexec).

La unidad queda con una tabla GPT y dos particiones:

| Partición    | Formato | Contenido                                        |
|--------------|---------|--------------------------------------------------|
| `EFI`        | FAT32   | Archivos de arranque + `sources/boot.wim`        |
| `INSTAL-WIN` | exFAT   | Contenido completo de la ISO (install.wim > 4 GiB) |

La interfaz se ejecuta sin privilegios. Las operaciones de disco las realiza el
mismo ejecutable en modo `--helper`, lanzado con `pkexec`, que valida que el
destino sea un disco USB y que la ISO sea un instalador de Windows **antes** de
borrar nada.

## Estructura

```
src/
  main.cpp            Punto de entrada (interfaz o modo --helper)
  core/               Constantes y detección de unidades USB (lsblk)
  helper/             Modo privilegiado: particionado, formateo y copia
  gui/                Ventana principal DTK
data/
  applications/       Lanzador .desktop
  icons/              Icono de la aplicación
  polkit/             Política de pkexec
  resources.qrc       Recursos embebidos
debian/               Empaquetado .deb
```

## Compilar

Dependencias (Deepin 23/25):

```sh
sudo apt install cmake g++ qt6-base-dev libdtk6core-dev libdtk6gui-dev libdtk6widget-dev
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Paquete `.deb`:

```sh
dpkg-buildpackage -us -uc -b
```

Dependencias en tiempo de ejecución: `util-linux` (sfdisk, wipefs, lsblk,
mount), `dosfstools`, `exfatprogs`, `pkexec`, `udev`.
