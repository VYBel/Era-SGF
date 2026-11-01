#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="${POCKETBOOK_SDK:-$project_dir/.sdk/SDK-B288}"
sdk_dir="$(python3 "$project_dir/scripts/prepare-sdk.py" --sdk-root "$sdk_dir")"
compiler="$sdk_dir/usr/bin/arm-obreey-linux-gnueabi-g++"
sysroot="$sdk_dir/usr/arm-obreey-linux-gnueabi/sysroot"
output_dir="${POCKETBOOK_BUILD_DIR:-$project_dir/build/pocketbook}"
mkdir -p "$output_dir"
version="$(cat "$project_dir/VERSION")"
cd "$project_dir"
LD_LIBRARY_PATH="$sdk_dir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$compiler" --sysroot="$sysroot" -std=c++14 -Os -DNDEBUG -DPLATFORM_FC \
    "-DERA_SGF_VERSION=\"$version\"" -fsigned-char -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=softfp \
    -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections \
    -Iinclude -I"$sysroot/usr/local/include" \
    src/pocketbook.cpp src/sgf.cpp src/viewer.cpp src/encoding.cpp src/storage.cpp src/render.cpp \
    -L"$sysroot/usr/local/lib" -L"$sysroot/usr/lib" \
    -Wl,-rpath-link,"$sysroot/usr/local/lib" -Wl,-rpath-link,"$sysroot/usr/lib" -Wl,-rpath-link,"$sysroot/ebrmain/lib" \
    -Wl,--gc-sections -Wl,-z,noexecstack -static-libstdc++ -static-libgcc \
    -linkview -lpthread -ldl -lm -o "$output_dir/era-sgf.app"
"$sdk_dir/usr/bin/arm-obreey-linux-gnueabi-strip" "$output_dir/era-sgf.app"
file "$output_dir/era-sgf.app"
