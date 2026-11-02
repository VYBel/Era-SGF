# Reader feedback implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Work is authorized by the owner's twelve requested corrections and instruction to reinstall on the connected reader.

**Goal:** Deliver Era-SGF 0.2.0 with correct stones, a usable game library, autoplay, screenshots and seven logo choices, installed on the Era 700.

**Architecture:** Keep portable C++14 scene, parser and storage code. Add a focused catalogue component and logo drawing component; the InkView adapter owns events, menus, timers and framebuffer capture. Retain the current checkout and branch; do not commit, push or merge without a separate explicit instruction.

**Tech Stack:** C++14, InkView B288 SDK 6.8, GNU Make, Python 3 host tests and installation tooling.

**Spec:** `docs/superpowers/specs/2026-11-01-reader-feedback-design.md`

## Global Constraints

- Target PocketBook Era 700, B288 ARMv7 soft-float, existing SDK 6.8.
- Version 0.2.0; no new reader runtime dependencies or network use.
- Preserve original SGFs and existing unrelated device files/settings.
- Catalogue paths are relative to the reader root; resume paths retain the existing absolute-path format.
- Default autoplay pause is 3 seconds; zero active playback timers while stopped or hidden.
- File operations never overwrite a destination or follow symlinks.
- Keep source data and local installation backups out of public Git.

## Review Focus

- Native circle semantics differ from SVG: test white interiors and black borders in actual adapter framebuffer output (Task 1).
- A file changes after indexing: block destructive actions and reset replaced-file status (Task 2).
- A move collides or the catalogue is malformed: preserve source bytes and recover a usable library (Task 2).
- Hide/menu/orientation occurs during autoplay: no subsequent node advance and no timer remains (Task 3).
- USB sorting changes an already viewed path: migrate the existing resume state and verify every game's hash (Task 6).

### Task 1: Correct and simplify the reader display

**Files:** `src/pocketbook.cpp`, `src/render.cpp`, `include/erasgf/render.hpp`, `tests/inkview_stub/*`, `tests/ui_test.py`, `tests/core_tests.cpp`.
**Interfaces:** Extend `ViewModel` with a centered move caption and display-only status. Keep `Scene`, `DrawOp` and existing actions compatible.

- [x] Add framebuffer support to the test adapter, reproducing SDK filled-circle semantics; add failing white-stone, player-colour, empty-comment and inactive-branch regressions.
- [x] Run `make test` and observe the new assertions fail against the old implementation.
- [x] Draw circles using explicit scanlines, add colour-labelled players, enlarge the board, place move number below it, and hide unnecessary comment/codec labels and inactive hit targets.
- [x] Run `make test all`; require all tests passing.

### Task 2: Persistent game catalogue and file management

**Files:** create `include/erasgf/library.hpp`, `src/library.cpp`, `tests/library_tests.cpp`; modify build lists, storage helpers, adapter menus and UI tests.
**Interfaces:** `LibraryRecord`, `LibraryFilter`, `Library(root,state_dir)`, `refresh`, `records`, `find`, `query`, `years`, `set_viewed`, `move_to_viewed`, `erase`. `LibraryRecord::path` is relative to root. Export safe atomic text writes for catalogue/settings.

- [x] Add failing catalogue tests: recursive indexing, UTF-8/CP1251 metadata, all/new/viewed and year/text queries, persistence, replacement reset, malformed catalogue recovery, collision-safe move, deletion and symlink/outside-root rejection.
- [x] Run the new test target and verify failures are missing behavior rather than fixture mistakes.
- [x] Implement the catalogue and file operations; keep cached browsing free of scans and preserve moved-file state.
- [x] Add library, year/status filters, keyboard search, explicit refresh, mark new/viewed, and confirmed move/delete UI actions. Add integration tests for state and bytes after confirmation/cancellation.
- [x] Run `make test all`; require all tests passing.

### Task 3: Autoplay with branch stops and idle behavior

**Files:** adapter, test InkView timer implementation, UI tests.
**Interfaces:** one playback callback, `start_autoplay`, `stop_autoplay`, `autoplay_tick`; pause setting persisted under the existing state directory.

- [x] Add failing tests for default/custom pauses, sequence progression, arriving/starting at a branch, end, cancellation by menu/manual/hide/orientation/error, and no timer or refresh when idle.
- [x] Run the tests and confirm the missing autoplay behavior.
- [x] Implement selectable pauses and a single active timer, visible playback status/control, and every stop path. Persist on hide/exit without polling.
- [x] Run `make test all`; require all tests passing.

### Task 4: Save a position screenshot

**Files:** adapter, storage helpers as needed, InkView test adapter, UI tests.
**Interfaces:** capture board view using `BitmapFromScreen`, save BMP using `SaveBitmap`, retain unique filenames under `screenshots/Era-SGF/`.

- [x] Add failing tests that request a screenshot from the menu, validate BMP dimensions/pixels, preserve the current node, and save distinct files on repeated requests.
- [x] Run the tests, implement capture and error handling, then run `make test all`.

### Task 5: Seven logo options and launch version

**Files:** create logo component, SVG choices/contact sheet and generator; adapter startup/logo selection; tests and build lists; `VERSION`.
**Interfaces:** `logo_scene(int choice,int width,int height)` or equivalent scene operation builder. Seven numbered choices, saved setting, version 0.2.0. One non-repeating startup transition.

- [x] Add failing tests for launch version, seven selectable/persistent marks, a single dismissible startup transition, and zero startup timers left after dismissal/hide.
- [x] Draw seven original monochrome marks, generate vector previews, implement launch screen and selection, and set version 0.2.0.
- [x] Run `make test all` and inspect the contact sheet and actual adapter framebuffer images.

### Task 6: Package, independent review, ARM build and USB update

**Files:** package tests/script, install/build/validation docs, a safe year-organization installation script.
**Interfaces:** ZIP with one `.app`; migration preserves the mapping of every old relative SGF path to its new `sgf/<year>/` path and old resume state.

- [x] Replace launcher expectations with a failing single-app package test, then remove installer/uninstaller launchers and update instructions.
- [x] Run the full host suite and native ARM build; inspect white/black framebuffer samples, archive contents and ELF metadata.
- [x] Request one independent whole-change review; resolve actionable correctness findings and rerun the affected checks.
- [x] Back up this reader's existing application/config/state locally, organize 1714 games by SGF year, migrate resume paths, install the new app, remove only the two obsolete launchers, and prebuild the catalogue.
- [x] Verify source and destination hashes, catalogue count, saved node, app checksum, single app entry, association and flushed writes. Report installed behavior separately from hardware confirmation.
