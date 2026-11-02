# Official InkView desktop emulator

The B288 6.8 SDK contains a native **x86-64 GTK2 InkView implementation** in
`usr/local/lib/libinkview.so`. This runs `src/pocketbook.cpp` and the production
parser, viewer, painter, menus and timers in an actual desktop window. It is a
separate build from the ARM reader application and from the automated InkView
test stub.

## Run

On x86-64 Linux with the B288 SDK already extracted:

```bash
bash scripts/run-emulator.sh
```

Equivalent Make targets are `make emulator-setup`, `make emulator-build` and
`make emulator`. From this checkout, `make emulator` is sufficient.

The first invocation fetches six pinned amd64 packages from the
[official Debian archive](https://archive.debian.org/debian/dists/stretch/main/binary-amd64/Packages.xz)
and verifies every package against its recorded SHA256. Five theme/keyboard data
files are fetched from the pinned [public SDK mirror](https://github.com/blchinezu/pocketbook-sdk/tree/64e9fa210dd90569f8eabea92da4d40f3d072fc1)
at commit `64e9fa210dd90569f8eabea92da4d40f3d072fc1`, also with pinned SHA256.
These private runtime files are not included in the application ZIP. `dpkg-deb -x` extracts
them beneath `.sdk/emulator-runtime/`; no installation, root access, shell
configuration or system library changes are involved. The SDK itself comes from
the publisher's [B288 6.8 release](https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8)
and can be prepared by the existing `bash scripts/setup-sdk.sh` command.

To prepare, build, or run without downloading again:

```bash
python3 scripts/setup-emulator.py --offline
bash scripts/run-emulator.sh --offline --build-only
bash scripts/run-emulator.sh --offline --no-build
```

An optional SGF argument is copied into the synthetic reader before opening it:

```bash
bash scripts/run-emulator.sh --offline tests/fixtures/utf-8-declared.sgf
```

Use an existing fixture path or any local SGF. Its original file is preserved,
including when exercising the application's move/delete menu. The default record
is `examples/first-steps.sgf`, copied to `local/emulator/reader/Go/`.

The host needs a C++14 compiler, Python 3, `dpkg-deb`, GTK2 and its normal desktop
dependencies, and an active `DISPLAY` (X11 or XWayland). Setup detects missing
libraries and reports them; it does not install host packages. Host Liberation
Sans is copied privately with the filename expected by InkView. If it is absent,
run `python3 scripts/setup-emulator.py --font /path/to/a/sans-font.ttf` once.
`POCKETBOOK_SDK` may select another extracted B288 SDK directory.

The SDK maps desktop keys to reader keys as follows:

| Desktop key | SDK reader event |
| --- | --- |
| Left / Right | Previous / next node in the app |
| Page Up / Page Down | Previous / next page key |
| Enter | OK; opens the app's main menu |
| F2 | Menu |
| F3 | Back; closes the app from its normal view |
| Escape | Power (it is not the reader's Back key) |

## Isolation and evidence

- `local/emulator/system/` holds the SDK's private config, fonts, temporary files
  and fallback resource directories. The SDK's original `hwconfig_700` selects
  **1264×1680**. Existing config and files are preserved on repeated setup.
- `ERA_SGF_ROOT` always names `local/emulator/reader/`; it never names the mounted
  PocketBook. Application state, catalogue and screenshots stay under this root.
- The launch log is `local/emulator/emulator.log`; the checked library resolution
  is `local/emulator/runtime-ldd.txt`. All downloaded packages, binaries and local
  reader data are ignored by Git.
- Only two private library search directories are used: the extracted Debian
  libraries and `SDK-B288/usr/local/lib`. In particular, `SDK-B288/usr/lib` is
  excluded so the old SDK cannot replace the host's modern `libstdc++`.

The private packages supply OpenSSL 1.0.2, the `CURL_OPENSSL_3` libcurl ABI,
JPEG 62, TIFF 5, ICU 57 and libcurl's old LDAP ABI. They are used only by this
SDK process. There are no aliases between incompatible library SONAMEs.

The window was actually launched on 2026-11-02 using this runtime. A synthetic SGF,
Russian labels, the Era-sized board, the splash timer, native InkView menu and
Right keyboard navigation were visually checked. A separate minimal SDK probe
confirmed F3 generates Back and completes the SDK's normal CloseApp/EVT_EXIT path.
Initial evidence is in
`local/emulator/second-window.png`, `right-node.png`, `direct-click-window.png`
and `second-start.log` (the initial binary reports version 0.2.0).

The final 0.3.0 adapter was built and launched on the same date. Actual SDK-window
captures in `local/emulator/v030-board.png`, `v030-menu.png`, `v030-navigation.png`
and `v030-number.png` show the stronger grid/white outline, five move controls,
six-item main menu, navigation submenu and opening numeric keyboard. Back cancelled
the input, Left returned to move0 (`v030-move-zero.png`), and Back closed the window
with node0 saved in the synthetic reader. This SDK session returned143 and printed
a GTK `gtk_main_quit` warning during shutdown; a zero exit status is not claimed
for this final session. Earlier minimal no-keyboard probes exited0.

## Differences from the reader

The emulator runs a host ELF, not the ARM executable or full PocketBook firmware.
It cannot establish FAT chmod semantics, reader write permissions, USB behaviour,
power management, physical key placement, E Ink contrast, ghosting or refresh time.
Those require the reader and the separate filesystem/adapter tests.

B288's SDK archive contains no runtime fonts, theme pack or keyboard layouts.
Private host fonts and the pinned older SDK resource pack render the menus and
numeric digits. Some newer keyboard/theme icons remain placeholders. Physical
desktop digit keys did not confirm a numeric-input callback; full entry/acceptance
in the GUI remains unverified, while application input validation and seeking
are covered by the host adapter tests. The reader supplies its own firmware assets.
Touch automation with XTest can be restricted by Wayland; actual SDK keyboard
events (for example Right, Left, Enter and F3) work. The SDK's own right-click
menu provides further device events and display modes. GTK2 emits harmless
warnings about callbacks on the SDK debug menu's branch items.
