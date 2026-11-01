#!/bin/sh
# Run inside the Debian ARM64 builder; no compilation on the N1.
set -eu
out=${1:?Usage: build-redsea.sh /writable/dependency-build-directory}
mkdir -p "$out"
if [ ! -d "$out/redsea-src/.git" ]; then
    git clone --depth 1 --branch v1.3.1 https://github.com/windytan/redsea.git "$out/redsea-src"
fi
revision=$(git -C "$out/redsea-src" rev-parse HEAD)
[ "$revision" = 4cc27df9939798e800c4ff7cb484be6d9bf68b4d ] || {
    echo 'Unexpected redsea source revision' >&2; exit 1;
}
if [ ! -f "$out/redsea-build/build.ninja" ]; then
    meson setup "$out/redsea-build" "$out/redsea-src" --buildtype=release -Dbuild_tests=false
fi
meson compile -C "$out/redsea-build" -j3
cp "$out/redsea-build/redsea" "$out/redsea"
cp "$out/redsea-src/LICENSE" "$out/REDSEA-LICENSE.txt"
