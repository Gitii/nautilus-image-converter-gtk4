# Nautilus Image Converter GTK4

Hard fork of the legacy Nautilus Image Converter codebase for modern Nautilus/GTK.

This targets Nautilus 46 / `libnautilus-extension-4` and GTK4. The old GTK3 extension path is not supported.

## Features

- Single right-click Transform Images action in Nautilus.
- GTK4 dialogs compatible with Nautilus 46.
- Resize, rotate, re-encode, or combine them in one operation.
- Preview runs the transform into temporary files and shows estimated output sizes.
- Output sizes include the delta, e.g. `521 KB (-22%)`.
- JPEG output uses ImageMagick decode/transform followed by mozjpeg `cjpeg` encode.
- JPEG target-size mode binary-searches quality from one transformed lossless intermediate.

## Build And Install

Host requirements on Ubuntu 24.04-style systems:

```bash
sudo apt install build-essential libnautilus-extension-dev libgtk-4-dev
```

Devbox must also be installed. It provides autotools, ImageMagick, and mozjpeg for this project.

Build and install:

```bash
./build-and-install.sh
```

Build without installing:

```bash
./build-and-install.sh --build-only
```

The script uses host `/usr/bin/gcc` and `/usr/bin/pkg-config` for Nautilus/GTK ABI compatibility, while Devbox provides autotools, ImageMagick, and mozjpeg.

## Install As A Debian Package

`packaging/build-debs.sh` builds the `nautilus-image-converter-gtk4` package for Ubuntu 24.04 and 26.04 in clean Docker containers, from the committed tree:

```bash
packaging/build-debs.sh            # both; or: noble | resolute
sudo apt install ./dist/debs/resolute/nautilus-image-converter-gtk4_*.deb
nautilus -q
```

Install the package built for your release. Each build links against that release's Nautilus and uses the system ImageMagick and `cjpeg` (from `libjpeg-turbo-progs`), whereas `build-and-install.sh` compiles in the paths to Devbox's copies. The package replaces Ubuntu's own `nautilus-image-converter`, and installs the same plugin file as `build-and-install.sh`, so use one or the other.

## Uninstall

```bash
sudo make uninstall                              # after build-and-install.sh
sudo apt remove nautilus-image-converter-gtk4   # after installing the package
nautilus -q
```

## Lineage

This is a hard fork of `Ameen-Sha-Cheerangan/nautilus-image-converter-legacy`, itself a legacy fork of the original GNOME Nautilus Image Converter.
