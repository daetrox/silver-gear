#!/usr/bin/env bash
# Empaqueta build/dist como AppImage (x86_64). Requiere haber ejecutado ./build.sh antes.
# Uso: packaging/make_appimage.sh [versión]
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-dev}"
APPDIR="$ROOT/build/AppDir"
TOOL="$ROOT/build/appimagetool-x86_64.AppImage"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/lib/silvergear"
cp -r "$ROOT/build/dist/." "$APPDIR/usr/lib/silvergear/"
# Nunca empaquetar el juego ni las BIOS del usuario.
rm -rf "$APPDIR/usr/lib/silvergear/game" "$APPDIR/usr/lib/silvergear/bios" "$APPDIR/usr/lib/silvergear/silvergear.desktop"

cp "$ROOT/packaging/silvergear.png" "$APPDIR/silvergear.png"
cat > "$APPDIR/silvergear.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Silver Gear
Comment=Lanzador para Metal Gear Solid (PlayStation)
Exec=silvergear
Icon=silvergear
Categories=Game;
DESKTOP
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/silvergear/silvergear" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

if [ ! -x "$TOOL" ]; then
  curl -sSfL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
# Sin FUSE (p. ej. en GitHub Actions) la herramienta se extrae y se ejecuta sola.
OUT="$ROOT/SilverGear-$VERSION-x86_64.AppImage"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$OUT"
echo ">> AppImage: $OUT"
