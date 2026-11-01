# Era-SGF native viewer implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this approved design task by task. Steps use checkbox tracking.

**Goal:** Build an experimental standalone PocketBook `.app` that opens SGF records, comments and variations, supports legacy Cyrillic encodings, and registers `.sgf` without KOReader.

**Architecture:** A portable C++ core parses raw SGF trees, decodes their text and replays any selected node. A shared presentation model feeds InkView and a desktop SVG preview. Installation and application-owned state use tested file operations.

**Tech stack:** C++17, Make, optional CMake/CTest, PocketBook InkView SDK 6.8 B288, Python 3 for fixture generation and integration tests. No runtime interpreter or online service is required on the reader.

**Spec:** [docs/design.md](../../design.md), approved by the owner on 2026-11-01.

## Global constraints

- Standalone native `.app`; no KOReader dependency or reused GPL/AGPL implementation.
- First target PocketBook Era 700; adaptive layout and explicit build profiles for other PocketBook OS / Linux devices.
- Preserve source SGF bytes and unrelated file associations.
- Initial codecs: UTF-8, Windows-1251, KOI8-R, CP866, ISO-8859-5 and ISO-8859-1; manual override, SGF CA, then practical auto detection.
- Desktop tests and ARM compilation do not establish physical-device compatibility.
- Public source and synthetic fixtures only; SDK and personal records stay ignored.
- Version 0.1.0 is experimental. No claim of hardware testing until the device is actually checked.

## Review focus

- A sibling variation must not inherit captures or setup changes from the previously viewed branch.
- Escaped brackets/backslashes and comment-only nodes must not change the shape of an SGF tree.
- A wrong or missing CA must remain recoverable through the manual encoding menu without rewriting the file.
- Association installation must retain unrelated handlers, be idempotent and support paths containing spaces/Cyrillic.
- Replacing a file at the same path must not restore a stale node index or codec from a different record.

## File map

- `include/erasgf/sgf.hpp`, `src/sgf.cpp`: bounded parser and raw game tree.
- `include/erasgf/encoding.hpp`, `src/encoding.cpp`, `src/codec_tables.inc`: strict UTF-8, legacy conversion and detection.
- `include/erasgf/viewer.hpp`, `src/viewer.cpp`: board replay, annotations and tree navigation.
- `include/erasgf/storage.hpp`, `src/storage.cpp`: atomic state, association merging/backups and file helpers.
- `include/erasgf/render.hpp`, `src/render.cpp`: responsive drawing operations and controls.
- `src/pocketbook.cpp`: InkView adapter, menus, file selection, event handling and font lifecycle.
- `src/inspect.cpp`, `src/preview.cpp`: host inspection and SVG preview of the shared layout.
- `tests/core_tests.cpp`, `tests/integration_test.py`: portable behavior and file-operation tests.
- `Makefile`, `CMakeLists.txt`, `cmake/pocketbook.cmake`, `scripts/*`, `.github/workflows/*`: native tests, cross-builds and packaging.

### Task 1: Parse and replay SGF records

**Interfaces:** `Collection parse_sgf(std::string_view, const ParseLimits& = {})`; `Game` owns indexed `Node` objects and their raw `Property` values. `Position replay(const Game&, size_t)` returns a bounded board, captured-stone counts, last move and selected-node annotations. `Viewer` owns `Collection` and exposes `select_game`, `select_node`, `next`, `previous`, `begin`, `end` and `previous_branch`.

- [ ] Write real C++ behavior tests for escaped text, collections, nested branches, empty values, passes, setup rectangles/removals, captures, sibling branch isolation, malformed syntax, unsupported games/sizes and parser limits.
- [ ] Compile interfaces with unimplemented stubs and watch the behavior tests fail because functionality is missing.
- [ ] Implement the bounded parser, board replay and navigation. Use iterative ancestor replay; do not share mutable board state between branches.
- [ ] Run `make test-core`; expect all parser/replay/navigation tests to pass.
- [ ] Commit the self-contained core and its tests.

### Task 2: Decode legacy text

**Interfaces:** `std::string normalize_encoding(std::string_view)`; `std::string decode_text(std::string_view, std::string_view)`; `EncodingChoice choose_encoding(std::string_view sample, std::string_view declared, std::string_view manual = "auto")`. Choices expose name, uncertainty and explanatory status. UI obtains per-game raw text and CA from Task 1.

- [ ] Add failing tests with hand-checked byte literals for all codecs, CA aliases, invalid UTF-8, missing/wrong declarations, ambiguous short text and explicit manual override/reset.
- [ ] Implement strict decoding and conservative detection. Generate single-byte mapping tables from Python's codec database; no iconv dependency is needed on the device.
- [ ] Test two game trees with different CA values and preserve their independent decoded comments.
- [ ] Run `make test-core`; expect the complete core suite to pass.
- [ ] Commit decoding, generated tables and tests.

### Task 3: Preserve files and register associations

**Interfaces:** `AssociationResult update_association(config_path, install)` merges only `era-sgf.app`. `load_state`/`save_state` store game index, node index, codec and fingerprint for a specific path. `read_file` enforces the parser's size limit; `fingerprint` identifies source contents.

- [ ] Add failing integration cases for absent/existing config, other SGF handlers, repeated install/uninstall, backups, symlink refusal, filenames with spaces/Cyrillic and stale source fingerprints.
- [ ] Implement atomic same-directory writes and checked backups before changing associations. Keep state under an application-owned directory.
- [ ] Add the `era-sgf-inspect` host executable so integration tests exercise the same production operations as the reader.
- [ ] Run `make test`; expect core and integration suites to pass, and assert original SGF bytes are unchanged.
- [ ] Commit storage and association support.

### Task 4: Render and integrate InkView

**Interfaces:** `Scene make_scene(const ViewModel&, int width, int height)` returns drawing operations and control rectangles; the SVG and InkView backends consume identical geometry. `pocketbook.cpp` adapts these models to the SDK and dispatches user actions through `Viewer`.

- [ ] Add failing bounds tests for 600×800, Era 1264×1680 and landscape layouts, and interaction tests for branch selection and comment paging.
- [ ] Implement high-contrast board rendering, marks, metadata/comments, page-turn navigation, variation/encoding menus, game selection, recent files and directory browsing.
- [ ] Add installation/removal menu actions and explicit installer/uninstaller launch modes.
- [ ] Render and inspect SVG/PNG previews containing Russian text and a branch; run `make test`.
- [ ] Cross-compile using the official B288 SDK, inspect the ELF architecture, interpreter and runtime dependencies, and resolve build errors against the actual headers.
- [ ] Commit the UI and build tooling. Record hardware verification as pending.

### Task 5: Package and publish the experimental implementation

**Interfaces:** `make package` builds a ZIP with app, association launchers, synthetic sample and installation/license notices; scripts verify expected files and generate SHA-256 checksums.

- [ ] Add a packaging integration test which opens the archive, validates executable names and excludes SDK/private data.
- [ ] Implement packaging, host CI, reproducible SDK setup instructions and bilingual install/encoding documentation.
- [ ] Run the complete tests, sanitizers, host preview checks and ARM/package verification. Obtain one independent code review and fix material findings with regression tests.
- [ ] Commit the completed implementation, publish the feature branch and create a reviewable pull request. Publishing a stable release or claiming hardware compatibility is not part of this step.

## Execution decisions

The owner has authorised implementation after reviewing the design. Execute continuously without another approval round for this task breakdown. Work in the requested new checkout on `codex/native-viewer`; preserve `main` for review. SDK download is necessary for the actual binary, but no SDK contents are committed. Make supports the current host, which has a compiler but no CMake; optional CMake remains available for other contributors.
