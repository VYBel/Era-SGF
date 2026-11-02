#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="${POCKETBOOK_SDK:-$project_dir/.sdk/SDK-B288}"
sdk_dir="$(realpath -- "$sdk_dir")"
work_dir="$project_dir/local/emulator"
runtime_lib="$project_dir/.sdk/emulator-runtime/root/usr/lib/x86_64-linux-gnu"
build_only=false
no_build=false
setup_args=()
sgf_file=""
for argument in "$@"; do
    case "$argument" in
        --build-only) build_only=true ;;
        --no-build) no_build=true ;;
        --offline) setup_args+=(--offline) ;;
        --help|-h)
            echo "Usage: $0 [--offline] [--build-only|--no-build] [SGF file]"
            echo "Uses official x86-64 InkView; all reader data stays under local/emulator/."
            exit 0 ;;
        --*) echo "Unknown option: $argument" >&2; exit 2 ;;
        *)
            if [[ -n "$sgf_file" ]]; then echo "Only one SGF file is accepted" >&2; exit 2; fi
            sgf_file="$(realpath -e -- "$argument")" ;;
    esac
done
if "$build_only" && "$no_build"; then
    echo "--build-only and --no-build cannot be combined" >&2
    exit 2
fi
python3 "$project_dir/scripts/setup-emulator.py" --sdk-root "$sdk_dir" "${setup_args[@]}"
binary="$work_dir/era-sgf-emulator"
if ! "$no_build"; then
    version="$(cat "$project_dir/VERSION")"
    cd "$project_dir"
    "${CXX:-c++}" -std=c++14 -O1 -g -DEMULATOR -Wall -Wextra -Wpedantic \
        "-DERA_SGF_VERSION=\"$version\"" -Iinclude -isystem "$sdk_dir/usr/local/include" \
        src/pocketbook.cpp src/sgf.cpp src/viewer.cpp src/encoding.cpp \
        src/storage.cpp src/render.cpp src/library.cpp src/logos.cpp \
        -L"$sdk_dir/usr/local/lib" \
        -Wl,-rpath-link,"$sdk_dir/usr/local/lib" -Wl,-rpath-link,"$runtime_lib" \
        -linkview -lpthread -ldl -lm -lz -o "$binary.new"
    mv -- "$binary.new" "$binary"
fi
if "$build_only"; then
    echo "Built official SDK desktop adapter: $binary"
    exit 0
fi
if [[ ! -x "$binary" ]]; then echo "No emulator binary; omit --no-build" >&2; exit 1; fi
if [[ -z "${DISPLAY:-}" ]]; then
    echo "DISPLAY is unset; the GTK2 SDK needs an X11 or XWayland session" >&2
    exit 1
fi
# An explicitly supplied SGF is copied into synthetic storage before it is opened.
# Menu operations (move/delete/mark) can therefore never change the original file.
if [[ -n "$sgf_file" ]]; then
    sgf_file="$(python3 - "$sgf_file" "$work_dir/reader" <<'PY'
import hashlib
from pathlib import Path
import shutil
import sys
source, root = (Path(value).resolve() for value in sys.argv[1:])
if not source.is_file():
    raise SystemExit("SGF argument must name a file")
try:
    source.relative_to(root)
except ValueError:
    checksum = hashlib.sha256(source.read_bytes()).hexdigest()[:12]
    target = root / "imports" / (checksum + "-" + source.name)
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        shutil.copyfile(source, target)
    source = target
print(source)
PY
)"
else
    sgf_file="$work_dir/reader/Go/first-steps.sgf"
fi
cd "$work_dir"
echo "Starting official InkView SDK (Era 700, 1264x1680)"
echo "Reader: $work_dir/reader"
echo "Log: $work_dir/emulator.log"
set +e
env LD_LIBRARY_PATH="$runtime_lib:$sdk_dir/usr/local/lib" \
    ERA_SGF_ROOT="$work_dir/reader" "$binary" "$sgf_file" 2>&1 | tee "$work_dir/emulator.log"
exit "${PIPESTATUS[0]}"
