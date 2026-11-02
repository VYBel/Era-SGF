# Building Era-SGF

## Desktop checks

Requirements: Linux, GCC with libstdc++ and C++14 Library Fundamentals headers (GCC 6.3+),
GNU Make, Python 3.9+ and zlib development headers (`zlib1g-dev` on Debian/Ubuntu). No downloaded C++ test framework is required.

```sh
make test all
```

This builds `era-sgf-inspect`, `era-sgf-preview` and the test binaries under `build/`.
The tests cover the portable core, actual file operations, the production device adapter
linked against a small InkView test implementation, and packaging/launcher behavior.

Optional host CMake build (CMake 3.20+):

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cmake --parallel 2
ctest --test-dir build-cmake --output-on-failure
```

## Native PocketBook build

Additional requirements: `curl`, `7z`, and about 8 GB of free disk space for the SDK archive
and extracted files. No root access, system package installation or firmware modification
is performed by the setup script.

```sh
bash scripts/setup-sdk.sh
make package
```

The setup downloads the publisher's [B288 SDK 6.8](https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8)
(1,301,230,510 bytes), checks SHA-256 and extracts it under `.sdk/`. The pinned archive digest is:

```text
b924fbeba90e9258854e910c2c29a3105e6fb9c6e0e6de4cb13a015be3fc23e2
```

Some recent 7-Zip versions refuse a subset of the archive's relative symlinks.
`prepare-sdk.py` restores only explicit linker-library symlinks after checking that their
resolved targets stay inside this SDK. It does not disable the extractor's link protections.

The canonical build uses the SDK's GCC 6.3, ARMv7, the soft-float calling convention and its
glibc sysroot. A command-local library path loads the SDK compiler's host libraries; it does
not change system libraries or your shell configuration. GCC C++ runtime libraries are
linked statically, as is the SDK zlib used to compress PNG screenshots; InkView and libc remain supplied by the reader firmware.

Outputs:

- `build/pocketbook/era-sgf.app`: stripped ARM ELF executable.
- `dist/Era-SGF-1.0.1-b288.zip`: install archive, containing one `.app` and two BMP icons.
- The adjacent `.zip.sha256`: archive checksum.

For an already extracted compatible SDK, set `POCKETBOOK_SDK` to its `SDK-B288` directory
and run `make package`. `POCKETBOOK_BUILD_DIR` can redirect the standalone build output.
The Make package target uses the default output directory.

An optional `cmake/pocketbook.cmake` toolchain is provided for contributors. The first local
ARM artifact was built with the Make/Bash path above; cross-CMake is not yet separately validated.

## Official SDK desktop emulator

```sh
make emulator-setup
make emulator-build
make emulator
```

This links the production adapter against the SDK's x86-64 InkView implementation.
Pinned dependencies are extracted privately; no system installation is performed.
See [emulator.md](emulator.md) for requirements, keyboard mapping and hardware limitations.

## Inspect and preview

```sh
build/era-sgf-inspect examples/first-steps.sgf --node 1
build/era-sgf-inspect tests/fixtures/windows-1251-auto.sgf --encoding cp1251
build/era-sgf-preview examples/first-steps.sgf 1264 1680 1 > build/era-preview.svg
build/era-sgf-inspect --catalogue /path/to/mounted/reader
build/era-sgf-logos 1 > build/logo-1.svg
```

The inspection output is JSON. Preview arguments are file, width, height, node, encoding,
comment page and full-comment mode (`0` or `1`), in that order. Open the SVG in a browser.
The preview uses shared scene geometry; physical fonts and display refresh still need a reader test.
`--catalogue` explicitly refreshes the persistent reader catalogue and prints counts by year.
It writes only `system/config/era-sgf/library.db`, with paths relative to the supplied root.
The optional logo contact sheet generator is documented in [logos/README.md](logos/README.md).

## Sanitizers and generated fixtures

```sh
make BUILD=build-sanitized CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
```

LeakSanitizer cannot run under some ptrace-based development harnesses. In that specific
environment, prefix the command with `ASAN_OPTIONS=detect_leaks=0`; address/undefined checks
remain enabled. CI runs the default sanitizer configuration.

`tools/generate_codecs.py` reproduces single-byte mapping data. `tools/generate_fixtures.py`
reproduces the synthetic encoding examples. Generated tables and public fixtures are committed.
Keep personal SGF records in the ignored `records/` or `local/` directories.
