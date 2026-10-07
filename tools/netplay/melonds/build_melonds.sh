#!/bin/bash
# Builds melonDS 1.1 natively for arm64 with melonds-netplay.patch, which adds GDB
# "monitor" commands to the ARM9 stub so the netplay tooling can drive it without focus:
#
#   monitor savestate <abs path>   -> OK | E.savestate failed
#   monitor loadstate <abs path>   -> OK | E.state file not found | E.loadstate failed
#   monitor fps <n>                -> OK | E01   (n > 0: limit to n frames/s, 0: unlimited)
#
# State operations run at the end of the current frame and reply once done; a halted
# target halts again afterwards (no stop reply). See the patch header for details.
#
#   tools/netplay/melonds/build_melonds.sh     # -> build/melonds-netplay/melonDS.app
#
# MELONDS_SRC / MELONDS_OUT override the source checkout (default build/melonds-src, reset
# to the 1.1 tag on each run; build dirs are kept) and the output folder.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SRC="${MELONDS_SRC:-$ROOT/build/melonds-src}"
OUT="${MELONDS_OUT:-$ROOT/build/melonds-netplay}"
PATCH="$HERE/melonds-netplay.patch"
TAG=1.1
BUILD="$SRC/build-netplay"

# Build deps (BUILD.md), installing only what's missing: nothing gets upgraded.
missing=()
for formula in pkgconf cmake sdl2 qt libarchive enet zstd faad2; do
    brew list --versions "$formula" >/dev/null || missing+=("$formula")
done
if [ ${#missing[@]} -gt 0 ]; then
    HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_UPGRADE=1 brew install "${missing[@]}"
fi

if [ ! -d "$SRC/.git" ]; then
    git clone https://github.com/melonDS-emu/melonDS "$SRC"
fi
git -C "$SRC" rev-parse -q --verify "refs/tags/$TAG" >/dev/null || git -C "$SRC" fetch --tags origin
git -C "$SRC" checkout -q -f --detach "$TAG"
git -C "$SRC" clean -q -fd   # build*/ is gitignored, so build dirs survive
git -C "$SRC" apply "$PATCH"

# The app links Qt/SDL/etc. from Homebrew (melonDS's mac-libs.rb bundler and macdeployqt
# both choke on Homebrew's split Qt 6.11), so rerun this script if a brew upgrade breaks it.
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
    -DMACOS_BUNDLE_LIBS=OFF
cmake --build "$BUILD" -j "$(sysctl -n hw.ncpu)"

mkdir -p "$OUT"
rm -rf "$OUT/melonDS.app"
cp -R "$BUILD/melonDS.app" "$OUT/"
codesign --force -s - "$OUT/melonDS.app"   # seal the whole bundle (ad hoc), not just the binary
echo "Built $OUT/melonDS.app ($(git -C "$SRC" describe --tags) + melonds-netplay.patch)"
