# Era-SGF

An independent SGF viewer for PocketBook e-readers, starting with **PocketBook Era 700**.

[Русский](README.ru.md) · [Install](docs/install.md) · [Build](docs/build.md) · [Validation](docs/validation.md) · [Contributing](CONTRIBUTING.md)

## Status

**Experimental version 0.1.0. Native ARM build and desktop tests pass; physical-device testing is pending.**

Era-SGF is a native PocketBook application built with the InkView SDK.
It launches from the device's Applications menu and can register a handler for `.sgf` files.
KOReader is not required.

The initial B288 package targets PocketBook firmware **6.8+**. Other devices and the stock
library's handling of a newly registered `.sgf` format need their own compatibility checks.

## Features

- Browse local SGF records on 9×9, 13×13 and 19×19 boards.
- Step forward and backward, select variations, and return to a branching point.
- Read move comments, game information, and board labels and marks.
- Use the touch screen and hardware page-turn buttons.
- Resume the last viewed position and variation after closing or hiding the application.
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

No stable release is published yet. To build the experimental archive on Linux:

```sh
make test all
bash scripts/setup-sdk.sh
make package
```

The SDK download is about 1.3 GB and stays outside Git. See [build requirements](docs/build.md).
The resulting ZIP contains the native `.app`, association install/remove launchers, synthetic
examples and license notices. Extract it into the reader's USB storage root, then launch
`era-sgf` from Applications. Run `era-sgf-install` explicitly to register `.sgf` files.
Existing configuration is backed up and other handlers are retained. Read the
[installation guide](docs/install.md) before the first device test.

The implementation uses C++14 with the official SDK's GCC 6.3, Make or optional CMake, and
PocketBook InkView. Portable logic and the event adapter have desktop tests. The
[validation report](docs/validation.md) distinguishes these results from physical-device checks.
See the [design document](docs/design.md) for scope and acceptance criteria.

## License

The original Era-SGF sources and documentation are licensed under [MIT](LICENSE).
PocketBook SDK components are separate third-party software under their own terms and are
not included in this repository. Future dependencies must retain their own notices.
