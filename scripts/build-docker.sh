#!/bin/sh
# On an ARM Mac, pass a copy of the installed N1 LVGL SDK as argument 1.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${1:?Usage: build-docker.sh /absolute/path/to/lvgl/install}
sdk=$(CDPATH= cd -- "$sdk" && pwd)
mkdir -p "$root/build"
docker build --platform linux/arm64 -t n1-radio-builder -f "$root/scripts/Dockerfile" "$root"
docker run --platform linux/arm64 --name "n1-radio-build-$(date +%s)" \
    -v "$root:/src:ro" -v "$sdk:/opt/lvgl/install:ro" -v "$root/build:/out" \
    n1-radio-builder sh -eu -c '
        cmake -S /src -B /out/cmake -DCMAKE_PREFIX_PATH=/opt/lvgl/install -DCMAKE_BUILD_TYPE=Release
        cmake --build /out/cmake -j3
        ctest --test-dir /out/cmake --output-on-failure
        cp /out/cmake/n1-radio /out/n1-radio
    '
