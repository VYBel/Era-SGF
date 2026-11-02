# Navigation and desktop implementation plan

> **For agentic workers:** Use superpowers:executing-plans for the coupled reader changes and superpowers:dispatching-parallel-agents for the two isolated environment/installer tasks. The owner requested the correction and testing cycle; continue without another approval gate.

**Goal:** Install Era-SGF 0.3.0 with stronger graphics, direct move navigation, compact menus, working exit, a Games launcher and a usable desktop emulation route.

**Architecture:** Extend portable Viewer and DrawOp, keep InkView as the production UI adapter, and add host-only installation/emulation helpers. Catalogue, SGFs, encoding and the Book logo are retained.

**Tech Stack:** C++14, existing B288 SDK 6.8, Make, Python 3; optional SDK emulator runtime kept local.

**Spec:** `docs/superpowers/specs/2026-11-02-navigation-and-desktop-design.md`

## Global Constraints

- Preserve the current branch and all uncommitted 0.2 work; no commit/push/merge.
- One reader `.app`; no installer/uninstaller shortcuts.
- No persistent timers or background scans during static reading.
- Do not modify game contents, saved move choices, or unrelated launcher entries.
- No global package installation or firmware partition modifications for emulation/launcher integration.
- Keep personal reader data and runtime downloads ignored under `local/` and `.sdk/`.

## Review Focus

- Write permission is independent of FAT chmod permission: fault-inject both and distinguish them (Task 1).
- Exit must close once even when save fails, without losing a working earlier state (Task 1).
- Ten moves differ from ten nodes in commented records; branches and passes must be retained (Task 2).
- Invalid numeric input must not mutate position or selected continuation (Task 3).
- Launcher merge must retain custom applications/groups and remain idempotent (Task 4).

### Task 1: Robust save and exit

**Files:** `src/storage.cpp`, `src/pocketbook.cpp`, test InkView adapter and UI/filesystem tests.
**Interfaces:** FAT-aware mode preservation in atomic writes; a single close helper that clears timers and handles save failure.

- [x] Reproduce existing mode-preservation failure and blocked CloseApp using fault injection; observe failing assertions.
- [x] Implement filesystem-aware mode handling, bounded error diagnostics, and reliable close on explicit exit/back/EVT_EXIT.
- [x] Run the full host suite.

### Task 2: Contrast and portable move navigation

**Files:** Viewer/render headers and implementations, native paint adapter, core and framebuffer tests.
**Interfaces:** Viewer last-move, seek-move and relative-move-jump operations; DrawOp stroke width; visible first/back-ten/number/forward-ten/last actions.

- [x] Add failing tests for thicker pixels, move jumps across comments/passes, chosen branches, endpoint clamping and atomic failure.
- [x] Implement the core operations and the navigation row with centered number.
- [x] Run core and adapter tests and inspect previews.

### Task 3: Compact menus and numeric jump UI

**Files:** `src/pocketbook.cpp`, `tests/ui_test.py`, test keyboard adapter.
**Interfaces:** Main menu <=6 items; Game/File/Settings/Association/Navigation submenus; number click opens validated numeric input.

- [x] Add failing realistic menu-flow and click/keyboard navigation tests; update old flows to enter the actual submenus.
- [x] Implement grouped menus and input validation; every navigation path cancels autoplay.
- [x] Run the full host suite.

### Task 4: Book icon and Games registration

**Files:** independent worker owns new icon generator, BMP assets, mounted-reader installer and its tests; parent integrates packaging/build/docs.
**Interfaces:** normal/focused BMPs under `applications/icons/`; merge `U_EraSGF` into `@Games` while preserving unrelated settings and backups.

- [x] Write and observe failing installer tests for preservation, duplicate/collision/symlink handling and idempotence.
- [x] Generate the Book icons and implement the host installer; do not write the reader yet.
- [x] Parent reviews files, includes icons in ZIP and runs packaging/installer tests.

### Task 5: Official SDK desktop emulator

**Files:** independent worker owns new SDK-emulation scripts and emulation documentation; parent integrates Make targets.
**Interfaces:** launch production `src/pocketbook.cpp` with official x86-64 InkView and synthetic local storage; runtime dependencies stay private.

- [x] Diagnose missing libraries and SDK resource requirements using actual tools and official sources.
- [x] Prepare a private runtime and repeatable emulator command, if feasible.
- [x] Launch and inspect the actual SDK UI; report concrete limitations and verification evidence.

### Task 6: Review, build and reader update

**Files:** version, build/package/docs, validation record and local installation report.

- [x] Run the integrated host suite and relevant sanitizers; request one independent review and reproduce/fix important findings.
- [x] Build the native ARM 0.3.0 app and package; verify ELF and archive.
- [x] Back up the connected reader app/configuration; install app/icons and merge Games registration.
- [x] Verify hashes, existing game/catalogue/resume preservation, icon references, flushed writes and report emulator versus hardware evidence clearly.
