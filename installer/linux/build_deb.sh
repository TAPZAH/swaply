#!/bin/sh
# Build a Debian package for the X11/Linux build.
# Usage: build_deb.sh [build-dir] [source-dir] [output-dir] [version] [arch]
set -eu

BUILD_DIR="${1:-/tmp/build}"
SOURCE_DIR="${2:-/src}"
OUT_DIR="${3:-${SOURCE_DIR}/dist}"
VERSION="${4:-0.991~beta}"
ARCH="${5:-amd64}"

PKG="${BUILD_DIR}/deb-root"
rm -rf "${PKG}"
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
install -m 0644 "${SOURCE_DIR}/LICENSE" "${PKG}/usr/share/doc/swaply/copyright"
install -m 0644 "${SOURCE_DIR}/THIRD_PARTY.md" "${PKG}/usr/share/doc/swaply/THIRD_PARTY.md"

cat > "${PKG}/usr/share/applications/swaply.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Swaply
Comment=EN/RU keyboard layout switcher
Exec=swaply-tray
Icon=input-keyboard
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
Depends: libx11-6, libxtst6, libgtk-3-0 | libgtk-3-0t64, libstdc++6
Description: EN/RU keyboard layout switcher for X11
 Swaply watches typing, detects words typed in the wrong EN/RU layout
 and retypes them, switching the XKB group. It runs as a tray daemon.
EOF

mkdir -p "${OUT_DIR}"
dpkg-deb --build --root-owner-group "${PKG}" "${OUT_DIR}/swaply_${VERSION}_${ARCH}.deb"
