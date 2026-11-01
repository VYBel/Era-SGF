# Era-SGF

An independent SGF viewer for PocketBook e-readers, starting with **PocketBook Era 700**.

[Русский](README.ru.md) · [Design](docs/design.md) · [Contributing](CONTRIBUTING.md)

## Status

**Project setup and design. There is no working viewer or installable release yet.**

The first version will be a native PocketBook application built with the InkView SDK.
It will launch from the device's Applications menu and act as a handler for `.sgf` files.
KOReader is not required.

## First version

- Browse local SGF records on 9×9, 13×13 and 19×19 boards.
- Step forward and backward, select variations, and return to a branching point.
- Read move comments, game information, and board labels and marks.
- Use the touch screen and hardware page-turn buttons.
- Resume the last viewed position and variation.
- Read UTF-8 and legacy Cyrillic text: Windows-1251, KOI8-R, CP866 and ISO-8859-5.
- Respect SGF `CA[...]` metadata, with automatic detection and a remembered manual override.

Opening a record will not modify the original SGF. Editing, online play and engine analysis
are outside the first version.

## Compatibility

The intended platform is **PocketBook OS / Linux**, with a responsive monochrome interface.
The design must not hard-code the Era's screen size or assume every device has touch input.

| Device | Status |
| --- | --- |
| PocketBook Era 700 | Primary target; not tested yet |
| Other PocketBook OS / Linux readers | Planned; not tested yet |
| Android-based readers | Outside the initial platform scope |

Source portability does not guarantee that one binary runs on every PocketBook. Different
device ABIs and firmware versions may need separate builds. Actual compatibility will be
recorded by model, firmware and application version after testing.

## Installation and builds

No release is available yet. Installation commands will be published with the first tested build.
The package is intended to contain a native `.app`, required assets and an optional association
installer that preserves and backs up the user's existing `extensions.cfg`.

The proposed implementation uses C++17, CMake and PocketBook InkView. Portable SGF, encoding
and navigation logic will have desktop tests; device integration will be checked separately.
See the [design document](docs/design.md) for the current proposal and acceptance criteria.

## License

The original Era-SGF sources and documentation are licensed under [MIT](LICENSE).
PocketBook SDK components are separate third-party software under their own terms and are
not included in this repository. Future dependencies must retain their own notices.
