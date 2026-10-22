#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
mkdir -p ../build

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -Werror -o ../build/reopt_tests \
    tests.cpp ../src/delta.cpp ../src/overrides.cpp ../src/gpu.cpp ../src/enforce.cpp
../build/reopt_tests sample_settings.xml ../../../../re/db/globals.tsv

if ! command -v wine >/dev/null; then
    echo "wine not found; the DLL hook test is skipped"
    exit 0
fi
(cd .. && ./build.sh >/dev/null)
"${ZIG[@]}" c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Werror -I../../../../packages/sdk/include \
    -o ../build/wine_hooks.exe wine_hooks.cpp ../src/enforce.cpp
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$PWD/../build/wineprefix}
RUN=(wine)
command -v xvfb-run >/dev/null && RUN=(xvfb-run -a wine)
"${RUN[@]}" ../build/wine_hooks.exe ../re_optimization.dll
"${RUN[@]}" ../build/wine_hooks.exe ../re_optimization.dll off
