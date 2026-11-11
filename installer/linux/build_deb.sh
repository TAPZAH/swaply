#!/bin/sh
# Build a Debian package for the X11/Linux build.
# Usage: build_deb.sh [build-dir] [source-dir] [output-dir] [version] [arch]
set -eu

BUILD_DIR="${1:-/tmp/build}"
SOURCE_DIR="${2:-/src}"
OUT_DIR="${3:-${SOURCE_DIR}/dist}"
VERSION="${4:-0.992.2~beta}"
ARCH="${5:-amd64}"

# Build the package tree under a POSIX temp dir: dpkg-deb inspects directory
# modes and workspaces on FAT/exFAT-mounted drives report 0777 regardless of
# chmod.  The tree is disposable, so its location does not matter.
PKG="$(mktemp -d "${TMPDIR:-/tmp}/swaply-deb-XXXXXX")"
trap 'rm -rf "${PKG}"' EXIT
mkdir -p "${PKG}/DEBIAN" \
         "${PKG}/usr/bin" \
         "${PKG}/usr/share/swaply/dict" \
         "${PKG}/usr/share/applications" \
         "${PKG}/usr/share/doc/swaply"

install -m 0755 "${BUILD_DIR}/swaply-x11" "${PKG}/usr/bin/swaply-x11"
install -m 0755 "${BUILD_DIR}/swaply-tray" "${PKG}/usr/bin/swaply-tray"
strip --strip-unneeded "${PKG}/usr/bin/swaply-x11" 2>/dev/null || true
strip --strip-unneeded "${PKG}/usr/bin/swaply-tray" 2>/dev/null || true

install -m 0644 "${SOURCE_DIR}/assets/dict/en.txt" "${PKG}/usr/share/swaply/dict/en.txt"
install -m 0644 "${SOURCE_DIR}/assets/dict/ru.txt" "${PKG}/usr/share/swaply/dict/ru.txt"
install -m 0644 "${SOURCE_DIR}/assets/dict/ATTRIBUTION.txt" "${PKG}/usr/share/swaply/dict/ATTRIBUTION.txt"
install -m 0644 "${SOURCE_DIR}/assets/default_config.json" "${PKG}/usr/share/swaply/default_config.json"
install -m 0644 "${SOURCE_DIR}/assets/icon.png" "${PKG}/usr/share/swaply/icon.png"
for SZ in 16 24 32 48 64 128 256; do
    install -d "${PKG}/usr/share/icons/hicolor/${SZ}x${SZ}/apps"
    install -m 0644 "${SOURCE_DIR}/assets/linux/icons/${SZ}x${SZ}/swaply.png" \
        "${PKG}/usr/share/icons/hicolor/${SZ}x${SZ}/apps/swaply.png"
done
install -m 0644 "${SOURCE_DIR}/LICENSE" "${PKG}/usr/share/doc/swaply/copyright"
install -m 0644 "${SOURCE_DIR}/THIRD_PARTY.md" "${PKG}/usr/share/doc/swaply/THIRD_PARTY.md"

cat > "${PKG}/usr/share/applications/swaply.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Swaply
Comment=EN/RU keyboard layout switcher
Exec=swaply-tray
Icon=swaply
Terminal=false
Categories=Utility;
Keywords=keyboard;layout;switcher;
EOF

SIZE="$(du -sk "${PKG}" | cut -f1)"
cat > "${PKG}/DEBIAN/control" <<EOF
Package: swaply
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${SIZE}
Maintainer: Tap3ah
Depends: libx11-6, libxtst6, libgtk-3-0 | libgtk-3-0t64, libglib2.0-0 | libglib2.0-0t64, libstdc++6
Description: EN/RU keyboard layout switcher for X11
 Swaply watches typing, detects words typed in the wrong EN/RU layout
 and retypes them, switching the XKB group. It runs as a tray daemon.
EOF

# dpkg-deb refuses (or mispackages) directories that are group/world writable;
# it checks the source tree before packaging, so normalize every mode here.
# Some umasks (FAT-backed workspaces) create 0777 dirs and files.
chmod -R a-w "${PKG}" 2>/dev/null || true
find "${PKG}" -type d -exec chmod 0755 {} +
find "${PKG}" -type f -exec chmod 0644 {} +
find "${PKG}/usr/bin" -type f -exec chmod 0755 {} +

mkdir -p "${OUT_DIR}"
dpkg-deb --build --root-owner-group "${PKG}" "${OUT_DIR}/swaply_${VERSION}_${ARCH}.deb"
