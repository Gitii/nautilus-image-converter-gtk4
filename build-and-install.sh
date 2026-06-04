#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVBOX_BIN="$ROOT_DIR/.devbox/nix/profile/default/bin"
INSTALL=1
RESTART_NAUTILUS=1

cd "$ROOT_DIR"

for arg in "$@"; do
	case "$arg" in
		--build-only)
			INSTALL=0
			RESTART_NAUTILUS=0
			;;
		--no-restart)
			RESTART_NAUTILUS=0
			;;
		-h|--help)
			echo "usage: $0 [--build-only] [--no-restart]"
			exit 0
			;;
		*)
			echo "error: unknown option: $arg" >&2
			echo "usage: $0 [--build-only] [--no-restart]" >&2
			exit 1
			;;
	esac
done

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "error: required command not found: $1" >&2
		exit 1
	fi
}

require_file() {
	if [ ! -x "$1" ]; then
		echo "error: required executable not found: $1" >&2
		exit 1
	fi
}

echo "==> Checking host build tools"
require_file /usr/bin/gcc
require_file /usr/bin/pkg-config
require_command make
if [ "$INSTALL" -eq 1 ]; then
	require_command sudo
fi

echo "==> Checking host Nautilus/GTK development packages"
/usr/bin/pkg-config --exists libnautilus-extension-4 gtk4 glib-2.0 gio-2.0 || {
	echo "error: missing host pkg-config packages for Nautilus 46/GTK4" >&2
	echo "hint: install libnautilus-extension-dev and libgtk-4-dev" >&2
	exit 1
}

echo "==> Preparing Devbox tools"
require_command devbox
devbox install >/dev/null
require_file "$DEVBOX_BIN/magick"
require_file "$DEVBOX_BIN/cjpeg"
require_file "$DEVBOX_BIN/autoreconf"

echo "==> Regenerating autotools files"
devbox run autoreconf -fi

NAUTILUS_DIR="$(/usr/bin/pkg-config --variable=extensiondir libnautilus-extension-4)"

echo "==> Configuring against host Nautilus/GTK ABI"
env \
	PATH="/usr/bin:/bin" \
	CC=/usr/bin/gcc \
	PKG_CONFIG=/usr/bin/pkg-config \
	LD=/usr/bin/ld \
	AR=/usr/bin/ar \
	NM=/usr/bin/nm \
	RANLIB=/usr/bin/ranlib \
	STRIP=/usr/bin/strip \
	MAGICK="$DEVBOX_BIN/magick" \
	CJPEG="$DEVBOX_BIN/cjpeg" \
	./configure --with-nautilusdir="$NAUTILUS_DIR"

echo "==> Building extension"
env \
	PATH="/usr/bin:/bin" \
	CC=/usr/bin/gcc \
	PKG_CONFIG=/usr/bin/pkg-config \
	LD=/usr/bin/ld \
	AR=/usr/bin/ar \
	NM=/usr/bin/nm \
	RANLIB=/usr/bin/ranlib \
	STRIP=/usr/bin/strip \
	make

echo "==> Verifying extension links to host GTK/Nautilus"
ldd "$ROOT_DIR/src/.libs/libnautilus-image-converter.so" | grep -E 'libnautilus-extension|libgtk-4'

if [ "$INSTALL" -eq 1 ]; then
	echo "==> Installing extension to $NAUTILUS_DIR"
	sudo env \
		PATH="/usr/bin:/bin" \
		make install
else
	echo "==> Skipping install (--build-only)"
fi

if [ "$RESTART_NAUTILUS" -eq 1 ]; then
	echo "==> Restarting Nautilus"
	nautilus -q || true
else
	echo "==> Skipping Nautilus restart"
fi

echo "==> Done"
