# Era-SGF


An independent SGF viewer for PocketBook e-readers, starting with **PocketBook Era 700**.

[Русский](README.ru.md) · [Install](docs/install.md) · [Build](docs/build.md) · [Validation](docs/validation.md) · [Contributing](CONTRIBUTING.md)

## Status

**Version 1.2.4** opens individual SGFs and resumes the last game without loading
the full catalogue. Random unviewed games use a compact disk index with bounded
retries for missing files. Library scanning is explicit; viewed history is preserved.
Checked with a 117,309-game catalogue; [measurements and limits](docs/library-performance.md).
[Download the compiled application](https://github.com/VYBel/Era-SGF/releases/tag/v1.2.4).
The application UI is currently in Russian.

Era-SGF is a native PocketBook application built with the InkView SDK.
It launches from the device's Applications menu and can register a handler for `.sgf` files.
KOReader is not required.

The B288 package targets PocketBook firmware **6.8+**. Other devices and the stock
library's handling of a newly registered `.sgf` format need their own compatibility checks.

## Features

- Browse local SGF records on 9×9, 13×13 and 19×19 boards.
- Step forward and backward, select variations, and return to a branching point.
- Use beginning, −10 moves, a clickable move number, +10 and end below the board.
- Read move comments, game information, and board labels and marks.
- Use the touch screen and hardware page-turn buttons.
- Resume the last viewed position and variation after closing or hiding the application.
- Read UTF-8 and legacy Cyrillic text: Windows-1251, KOI8-R, CP866 and ISO-8859-5.
- Respect SGF `CA[...]` metadata, with automatic detection and a remembered manual override.
- Keep a persistent catalogue with all/new/viewed, year and text filters.
- Mark viewed records, move them to `sgf/viewed/<year>/` while retaining resume state,
  or delete the selected file after confirmation.
- Autoplay with a default 3-second pause and a mandatory stop at branches.
- Choose full refresh every 5/10/20/50 changes or manually, with a top-right refresh button.
- Save board screenshots as PNG and choose among seven monochrome launch marks.
- Show the result at the main-line end, game details via the top-left button, and About in Settings.

Static viewing has no application polling, periodic redraw, network activity or playback timer.
Actual battery consumption also depends on the reader firmware and frontlight and needs measurement.

Opening a record will not modify the original SGF. Editing, online play and engine analysis
are outside the first version.

## Compatibility

The intended platform is **PocketBook OS / Linux**, with a responsive monochrome interface.
The design must not hard-code the Era's screen size or assume every device has touch input.

| Device | Status |
| --- | --- |
| PocketBook Era 700 | Primary target; owner tested pre-release builds, see the validation report |
| Other PocketBook OS / Linux readers | Planned; not tested yet |
| Android-based readers | Outside the initial platform scope |

Source portability does not guarantee that one binary runs on every PocketBook. Different
device ABIs and firmware versions may need separate builds. Actual compatibility will be
recorded by model, firmware and application version after testing.

## Installation and builds

Download **Era-SGF-1.2.4-b288.zip** from the [release page](https://github.com/VYBel/Era-SGF/releases/tag/v1.2.4).
Extract it into the reader storage root, safely eject USB and launch `era-sgf` from Applications.
No SDK or compiler is needed to install the release. The optional Linux/POSIX host installer,
also attached to the release, registers the icon in Games with a configuration backup.
Follow the [installation guide](docs/install.md).

To build from source on Linux:

```sh
make test all
bash scripts/setup-sdk.sh
make package
```

The SDK download is about 1.3 GB and stays outside Git. See [build requirements](docs/build.md).
The resulting ZIP contains one native `.app`, two BMP icons, synthetic examples and notices.
The [host installer](docs/desktop-integration.md) backs up the desktop configuration and adds
the Book icon beside Chess in Games. Extracting the ZIP alone installs the app without
this registration. Use Settings → SGF association in the app to register `.sgf` files;
existing handlers are retained and the configuration is backed up. Read the
[installation guide](docs/install.md) before the first device test.
Remove obsolete `era-sgf-install.app` and `era-sgf-uninstall.app` shortcuts when upgrading from 0.1.

The implementation uses C++14 with the official SDK's GCC 6.3, Make or optional CMake, and
PocketBook InkView. Portable logic and the event adapter have desktop tests. The
[validation report](docs/validation.md) distinguishes these results from physical-device checks.
See the [design document](docs/design.md) for scope and acceptance criteria.

An [official SDK desktop emulator](docs/emulator.md) is available on Linux via `make emulator`.
It uses separate synthetic storage and never writes to the connected reader.

## License

The original Era-SGF sources and documentation are licensed under [MIT](LICENSE).
PocketBook SDK components are separate third-party software under their own terms and are
not included in this repository. Future dependencies must retain their own notices.

Dependency notices and the InkView investigation: [THIRD_PARTY.md](THIRD_PARTY.md).
