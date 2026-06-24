#!/usr/bin/env bash
# Build, test, and run microtext. Every artifact lands in build/output.
# Usage: build/build.sh [all|test|sanitize|leaks|golden|fuzz|demo1|run-demo1|demo2|run-demo2|clean]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="build/output"
CC="${CC:-clang}"
CFLAGS="-std=c99 -Wall -Wextra -pedantic -O2"
SANFLAGS="-std=c99 -Wall -Wextra -pedantic -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
MAC_FW="-framework CoreText -framework CoreGraphics -framework CoreFoundation"

mkdir -p "$OUT"

# stb single-file headers are fetched on demand, not vendored.
STB_REV="31c1ad37456438565541f4919958214b6e762fb4"
fetch_3rd() {
    mkdir -p tests/3rd
    for h in stb_image.h stb_image_write.h; do
        [ -f "tests/3rd/$h" ] || curl -fsSL \
            "https://raw.githubusercontent.com/nothings/stb/$STB_REV/$h" -o "tests/3rd/$h"
    done
}

build_test()   { fetch_3rd; $CC $CFLAGS tests/test_dump.c -o "$OUT/test_dump" $MAC_FW; }
build_golden() { fetch_3rd; $CC $CFLAGS tests/golden.c -o "$OUT/golden_test" $MAC_FW; }
build_fuzz()   { $CC $SANFLAGS tests/fuzz.c -o "$OUT/fuzz" $MAC_FW; }
build_demo1()  { $CC $CFLAGS examples/demo_1_showcase.c -o "$OUT/demo_1_showcase" $MAC_FW; }
build_demo2()  {
    local rl
    rl="$(brew --prefix raylib)"
    $CC $CFLAGS examples/demo_2_raylib.c -o "$OUT/demo_2_raylib" \
        -I"$rl/include" -L"$rl/lib" -lraylib \
        $MAC_FW -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
}

case "${1:-all}" in
all)
    build_test && "$OUT/test_dump"
    build_demo1
    build_demo2
    ;;
test)
    build_test && "$OUT/test_dump"
    ;;
sanitize)
    fetch_3rd
    $CC $SANFLAGS tests/test_dump.c -o "$OUT/test_dump_san" $MAC_FW
    "$OUT/test_dump_san"
    ;;
# Ad-hoc codesign with get-task-allow so `leaks` can scan the data segment for
# roots; without it the rooted sRGB color-space singleton is a false positive.
leaks)
    build_test
    codesign -s - -f --entitlements tests/leaks.entitlements "$OUT/test_dump"
    leaks --atExit -- "$OUT/test_dump"
    ;;
# MICROTEXT_UPDATE_GOLDEN=1 build/build.sh golden regenerates the references.
golden)
    build_golden && "$OUT/golden_test"
    ;;
# Extra arguments are corpus files replayed after the built-in seeds.
fuzz)
    build_fuzz && "$OUT/fuzz" "${@:2}"
    ;;
demo1)
    build_demo1
    ;;
run-demo1)
    build_demo1 && "$OUT/demo_1_showcase"
    ;;
demo2)
    build_demo2
    ;;
# Opens a blocking window.
run-demo2)
    build_demo2 && "$OUT/demo_2_raylib"
    ;;
clean)
    rm -rf "$OUT"
    ;;
*)
    echo "usage: build/build.sh [all|test|sanitize|leaks|golden|fuzz|demo1|run-demo1|demo2|run-demo2|clean]" >&2
    exit 2
    ;;
esac
