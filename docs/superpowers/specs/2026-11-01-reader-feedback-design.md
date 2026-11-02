# Era-SGF 0.2: feedback from the first Era 700 test

The owner tested 0.1.1 on a physical Era 700 and requested all changes below, followed by another USB installation. This document records implementation choices for that request. The current `codex/native-viewer` checkout is clean; work continues there. No GitHub push or merge is part of this USB update.

## Display

Black stones must be filled black. White stones must have a white interior and a visible black outline. InkView's `DrawCircle` fills the circle on B288; the old adapter painted white and then covered it with this call. Use explicit scanlines for both fill and outline, and verify the native adapter's pixels as well as the shared scene.

Show separate black and white player labels, including ranks when present. Reserve two compact header rows. Enlarge the board by using the space previously occupied by permanent codec and comment controls. Center the move number immediately below the board. Do not show an empty-comment message, the current encoding, or “Комментарий 1/1”. Real comments remain readable and expandable; pagination appears only when needed. “Ветки” is grey and inactive with zero or one child, and active at a real choice of at least two continuations.

## Library and files

Maintain an atomic, persistent local catalogue under `system/config/era-sgf/library.db`. Store paths relative to the reader root, source fingerprints, file size/mtime, SGF date, players, event, and viewed status. Use the existing C++14 core and filesystem routines; no additional runtime dependency is needed for this small local catalogue. Catalogue entries describe SGF files, and indicate collections with multiple games.

The library scans `sgf/` and the bundled `Books/Era-SGF/` recursively on first use or explicit refresh. Read the saved catalogue on subsequent starts. Browsing, searching and filtering operate in memory; viewing a static position does not rescan directories or periodically write state. Invalid SGFs appear with an error indication without preventing other entries from being listed. Replacing a file resets its viewed status. External moves can recover an absent record's viewed status by its source fingerprint; duplicate files remain distinct entries.

Filters: all, new (not marked viewed), viewed; game year; text search across players, event, date and filename. Viewing a leaf after navigation marks the file viewed, as does an explicit menu action. Merely opening a file does not mark it viewed. A menu action can reset it to new.

The current file can be moved to `sgf/viewed/<year>/` or deleted from reader storage. Both actions require an in-app confirmation. A move must preserve bytes, resume state and catalogue status and must never overwrite an existing file. A delete verifies that the selected source is still the same regular file, then removes only that file and its catalogue entry. A failure must leave the viewer operable. File actions reject symlinks and paths outside the reader's SGF storage.

During this USB update, reorganize the 1714 already copied games into `sgf/<year>/`, using SGF `DT` first and the filename second. Preserve every byte, filenames without results, all duplicate copies, and the existing resume position at node 12. Back up the current application/configuration locally before updating. The original Windows collection remains unchanged.

## Autoplay and power

Autoplay is explicit, defaults to a 3-second pause, and allows selecting 1, 2, 3, 5, 10, 15, 30 or 60 seconds. An active timer advances one SGF node at a time, including comment-only nodes. Stop on arriving at a branch, at the end, on manual navigation, menus, hide/background/exit, orientation changes and errors. Starting at a branch must not select a continuation implicitly. A branch choice is manual; autoplay can then be started again.

Use one InkView timer only while autoplay is active, apart from a single startup transition. Clear it on every stop path. No periodic redraws, library polling, network activity, sleep bans, or disabled power-off. Do not redraw when navigation makes no change. Physical energy consumption still requires a reader measurement; automated checks establish the application's idle behavior.

## Screenshot and branding

Add a menu action saving a BMP screenshot of the current position and player labels under `screenshots/Era-SGF/`, with collision-free names and an explicit success/error message. Capture the board view rather than an open menu or full-comment overlay. Use the reader's framebuffer capture API; verify that a saved image has the expected dimensions and white/black stone pixels.

Create seven original monochrome vector logo options and a numbered contact sheet for the owner's choice. Make all seven selectable in the application and persist the choice. The owner selected option 5, Book, as the primary mark; use it by default and on the owner's reader. The launch screen shows the mark, Era-SGF, and version 0.2.0. A single short startup transition may dismiss it; it must not become a repeating timer.

## Installation and verification

Ship only `applications/era-sgf.app`; keep association actions in its menu and CLI. Remove the old `era-sgf-install.app` and `era-sgf-uninstall.app` from this reader and from future packages. Preserve the existing SGF association.

Acceptance: host core/filesystem/UI tests, framebuffer checks, timer lifecycle and branch-stop tests, catalogue/status/search/move/delete regression tests, deterministic package tests, a real ARM B288 build, archive checksums, then verified USB update and per-file hashes after year organization. Only the owner can confirm the final on-screen hardware behavior after USB disconnection.
