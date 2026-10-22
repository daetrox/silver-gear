#!/usr/bin/env bash
# Compila Silver Gear (Metal Gear Solid) desde cero: SDL2, el núcleo SwanStation y el launcher.
# Uso: ./build.sh            -> compila en build/dist
#      ./build.sh --package  -> además crea silvergear-linux-x86_64.tar.gz
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
TP="$ROOT/third_party"
JOBS="$(nproc)"

SDL_TAG="release-2.32.8"
SWAN_COMMIT="dd48056"   # libretro/swanstation (GPL-3.0)

mkdir -p "$TP"

if [ ! -d "$TP/SDL2" ]; then
  git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$TP/SDL2"
fi
if [ ! -d "$TP/swanstation" ]; then
  git clone https://github.com/libretro/swanstation.git "$TP/swanstation"
  git -C "$TP/swanstation" checkout "$SWAN_COMMIT"
fi
if [ ! -d "$TP/rcheevos" ]; then
  git clone --depth 1 https://github.com/RetroAchievements/rcheevos.git "$TP/rcheevos"
fi
if [ ! -d "$TP/libretro-common" ]; then
  git clone --depth 1 https://github.com/libretro/libretro-common.git "$TP/libretro-common"
fi

# --- SDL2 (local, sin tocar el sistema) ---
if [ ! -f "$TP/sdl2-install/lib/libSDL2-2.0.so.0" ]; then
  X11=ON
  if [ ! -f /usr/include/X11/extensions/Xext.h ]; then
    echo ">> Aviso: faltan las cabeceras de X11 (libxext-dev). SDL2 se compilará sólo con Wayland."
    X11=OFF
  fi
  cmake -S "$TP/SDL2" -B "$TP/build-sdl2" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$TP/sdl2-install" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_X11=$X11
  ninja -C "$TP/build-sdl2" -j"$JOBS" install
fi

# --- Núcleo SwanStation ---
if [ ! -f "$TP/build-swan/build.ninja" ]; then
  cmake -S "$TP/swanstation" -B "$TP/build-swan" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi
ninja -C "$TP/build-swan" -j"$JOBS" # incremental: recompila si cambia el parche

# --- Launcher ---
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$ROOT/build" -j"$JOBS"
cp "$ROOT/packaging/silvergear.desktop" "$ROOT/LICENSE" "$ROOT/README.md" "$ROOT/build/dist/"

echo ">> Listo: $ROOT/build/dist/silvergear"

if [ "${1:-}" = "--package" ]; then
  rm -rf "$ROOT/build/SilverGear"
  cp -r "$ROOT/build/dist" "$ROOT/build/SilverGear"
  # Nunca distribuir el juego ni las BIOS de Sony: cada usuario aporta los suyos.
  rm -rf "$ROOT/build/SilverGear/game" "$ROOT/build/SilverGear/bios"
  mkdir -p "$ROOT/build/SilverGear/game" "$ROOT/build/SilverGear/bios"
  echo "Copia aquí las imágenes de los dos discos de Metal Gear Solid (.cue + .bin, .chd...)." > "$ROOT/build/SilverGear/game/LEEME.txt"
  echo "Copia aquí la BIOS de tu consola (scph5502.bin PAL, scph5501.bin USA, scph5500.bin JAP)." > "$ROOT/build/SilverGear/bios/LEEME.txt"
  tar -czf "$ROOT/silvergear-linux-x86_64.tar.gz" -C "$ROOT/build" SilverGear
  rm -rf "$ROOT/build/SilverGear"
  echo ">> Paquete: $ROOT/silvergear-linux-x86_64.tar.gz"
fi
