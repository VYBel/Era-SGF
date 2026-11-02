# Era-SGF 0.3: navigation, contrast and desktop integration

The owner requested these corrections after testing 0.2 on the Era 700. Continue the current uncommitted `codex/native-viewer` checkout and install the tested result when the reader is mounted. Keep the selected primary logo, option 5 (Book), and preserve game files, catalogue and saved positions. No commit, push or merge is requested.

## Reader UI

Increase the board grid from single-pixel lines to at least two pixels (three at Era resolution), and the black outline of a white stone to four pixels at Era resolution. Keep the interior white. Stroke width belongs to shared drawing operations so native rendering and SVG previews represent the same design.

Immediately under the board, provide one row: start, ten moves back, a centered clickable move number, ten moves forward, end. The number opens a numeric keyboard for an exact move. Ten-move jumps count moves including passes, skip comment-only nodes, clamp at endpoints and retain the selected continuation. A direct out-of-range or malformed number leaves the current position unchanged and explains the valid range. Physical page keys retain node-by-node navigation, including comments. Every manual navigation stops autoplay.

The main menu has at most six actions while a game is open: Library, Game, File, Screenshot, Settings, Exit. Submenus group game information/variations/navigation/autoplay, file opening/marking/move/delete, and encoding/pause/logo/orientation/associations. Keep destructive confirmations and all existing features. Start/end/ten-move controls stay visible under the board.

## Exit and filesystem

The old exit path calls `persist()` before `CloseApp()`; any save error bypasses the close call. Correct this independently of the underlying write error: attempt to save once, report/log a failure and close reliably, clearing timers. Avoid retrying the same failure from EVT_EXIT after an explicit close attempt.

The likely permission-error path is `fchmod` when replacing an existing state file on FAT. FAT does not store normal Unix ownership and mode; the reader app can have write access without permission to change ownership-derived modes. Determine the descriptor filesystem; on FAT omit the irrelevant Unix-mode preservation, while retaining write/fsync/rename error checks. On POSIX filesystems retain mode preservation and refuse real failures. Add failure-injection tests for FAT mode rejection and genuine write errors, and bounded diagnostic logging on error only. The exact original reader error has not yet been supplied, so hardware confirmation remains required.

## Launcher integration

The reader's actual `system/config/desktop/view.json` is version 7 and documents user IDs beginning with `U_` and icon object paths. Its `@Games` group includes `PB_Chess`. Register `U_EraSGF` there, with the native app path and two original BMP icons based on Book. Preserve all existing applications, group entries, comments and unrelated configuration, and save a backup before modifications. Use a tested host installer with the mounted reader root; keep a single `.app` in Applications. Native firmware files do not need modification.

## Emulation

Prefer the official x86-64 InkView library already present in B288 SDK. It uses GTK2 and requires several older shared libraries absent from the current host. Investigate a private local runtime from official package archives, without system package changes; launch the production adapter using a separate synthetic reader directory. Provide a repeatable command and document differences from real hardware, especially FAT permissions, power and E Ink refresh. If a real SDK emulator is unavailable, report the concrete blocking condition before choosing a clearly labelled simulator fallback.

## Acceptance

Normal and fault-injected host tests; pixel checks for thicker grid and white outlines; move-navigation tests with comments/passes/branches and invalid targets; actual submenu flows; idempotent desktop merge and icon/package checks; real SDK emulator launch and visual inspection if possible; independent whole-change review; ARM B288 build and USB installation with backups, hashes, saved-state and catalogue preservation.
