# Nautilus Image Converter GTK4

Hard fork of the legacy Nautilus Image Converter codebase for modern Nautilus/GTK.

This targets Nautilus 46 / `libnautilus-extension-4` and GTK4. The old GTK3 extension path is not supported.

## Features

- Right-click image resize and rotate actions in Nautilus.
- GTK4 dialogs compatible with Nautilus 46.
- Resize, re-encode, or resize plus re-encode in one operation.
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

## Uninstall

```bash
sudo make uninstall
nautilus -q
```

## Lineage

This is a hard fork of `Ameen-Sha-Cheerangan/nautilus-image-converter-legacy`, itself a legacy fork of the original GNOME Nautilus Image Converter.
