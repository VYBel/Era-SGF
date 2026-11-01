# Era-SGF: native PocketBook SGF viewer

Date: 2026-11-01

Status: approved for implementation on 2026-11-01; no device build has been validated.

## Intent and agreed requirements

The owner wants to read Go game records, comments and variations on a PocketBook without
installing KOReader. The first target is PocketBook Era 700. Other PocketBook OS / Linux
devices should be supported through a portable core, adaptive UI and documented build profiles.
Era-SGF is a public project maintained independently of the owner's other Go applications.

The application must associate with `.sgf` files and handle legacy Russian text encodings,
rather than assuming every record is UTF-8.

The technology, UI defaults and license below are implementation proposals, not additional
requirements supplied by the owner.

## Approaches

1. Adapt a GOReader plugin: potentially faster, but requires KOReader. The owner rejected
   this dependency, so this approach is not selected.
2. Build an independent native `.app` using PocketBook InkView: selected in the conversation.
   It requires cross-compilation and physical-device checks, but provides a standalone viewer
   and direct integration with PocketBook file handlers.

The proposed implementation language is C++17, with CMake and the SDK toolchain. The original
project sources use MIT so other people can study, redistribute and adapt the viewer. No
GOReader/KOReader code or SDK distribution is copied into the repository.

## First-version scope

- Open a local file passed by the PocketBook launcher or selected inside the application.
- Display Go boards of sizes 9, 13 and 19, including setup stones, passes and captures.
- Support SGF collections with more than one game through a simple game selector.
- Step to the next/previous node and start/end of the selected line; choose a variation and
  return to the previous branching point. Comment-only and setup nodes remain navigable.
- Show current-node comments, game comments and basic game metadata; long comments are paged.
- Display labels, triangles, squares, circles and crosses. Show a clear last-move indicator.
- Save the last opened file, node path and manually selected encoding in application-owned state.
- Launch from Applications and through a `.sgf` file association.

Editing SGF, adding variations, engine analysis, network access and online accounts are outside
this version. The original files are always read-only. Unsupported game types and unsupported
board sizes produce a readable message rather than an incorrect board.

## Components and data flow

1. **Document reader:** reads raw bytes with an explicit size limit and identifies a collection
   of game trees. The parser handles SGF escaping, multiple values, empty values and nested
   variations. It reports malformed input with a useful location and enforces nesting/node limits.
2. **Text decoding:** resolves an encoding for each game tree and decodes only after respecting
   the SGF property's text semantics. Raw data is retained so a manual encoding change can
   re-render the record without opening or rewriting a converted copy.
3. **Replay/navigation:** keeps the tree separate from board state. Reconstructs the selected
   node from its ancestor path so captures and setup changes in sibling branches cannot leak.
4. **Portable view state:** holds the selected tree, node path, board, annotations and comment.
5. **InkView adapter:** handles device events, screen updates, text rendering, file selection,
   launch arguments and persistent state. The core has no InkView dependency.

A desktop test target exercises the first four components. A host preview may be added if it
helps layout work, but it is not evidence of physical-device compatibility.

## Encodings

Initial codecs: UTF-8, Windows-1251 (CP1251), KOI8-R, CP866, ISO-8859-5 and ISO-8859-1.
Conversion to UTF-8 is internal; the source file is never converted in place. Use `iconv` when
the target SDK provides working converters; if the device toolchain lacks them, provide small
tables for the required single-byte encodings rather than adding a large runtime dependency.

Resolution policy:

1. A remembered manual override for this file takes precedence, including over an incorrect `CA`.
2. Otherwise, honour a supported `CA[...]` from the root of the selected game tree.
3. When `CA` is absent, accept a UTF-8 BOM or strictly valid UTF-8. ASCII needs no inference.
4. For other bytes, score the supported Cyrillic encodings using the record's text, selecting
   a candidate only with sufficient confidence; otherwise use the SGF-defined Latin-1 default
   and make the encoding selector readily available.

The UI offers Auto and explicit encodings. The selection is remembered per file and can be
reset. Ambiguous short comments are expected: the viewer must not claim perfect detection.
Invalid declared encodings or decoding errors are surfaced instead of silently replacing text
or discarding the rest of a comment. An unsupported declared codec can be manually overridden.

UTF-16/32 and additional East Asian legacy codecs are not promised in the initial release.
Unsupported declarations produce a clear explanation. Aliases such as CP1251 and WINDOWS-1251
are normalised. SGF collections may use different `CA` values for different game trees.

This policy deliberately offers practical compatibility for old files without `CA`; the SGF
specification itself defines ISO-8859-1 as the default.

## E Ink interface and device compatibility

Use a monochrome, high-contrast interface without animation. Compute layout from the current
screen dimensions, orientation, font metrics and input capabilities, rather than Era-specific
pixel coordinates. Default to a large board with a comment area and compact navigation controls;
provide a larger comment view when text exceeds the available area.

Map hardware page-turn buttons to previous/next node. Touch-capable devices also allow choosing
a variation on the board or through a list. Provide button-accessible navigation and menus,
including variation and encoding selection. The exact bindings for each device are builder
decisions to record during testing.

Refresh changed regions on an action and perform full refreshes when needed to clear ghosting.
Screen refresh behaviour, font rendering, suspend/resume, orientation and button mappings
must be checked on hardware. Merely building for ARM does not confirm them.

Era 700 is the initial target, but its firmware version is not yet known. Select the SDK profile
after checking its minimum supported firmware and userland ABI. Keep build profiles explicit;
publish separate binaries when devices require different ABIs. Maintain a compatibility table
with confirmed model, firmware and application version rather than claiming support for all models.

## Installation and file association

Distribute a ZIP with `applications/era-sgf.app`, required resources, concise installation
instructions and an optional association installer. The application accepts the document path
as a launch argument; launching without an argument opens the recent-file/file-selection screen.

PocketBook uses `system/config/extensions.cfg` for user file handlers. The installer reads the
existing file, creates a backup before a change and merges only the `.sgf` association. It never
replaces unrelated entries or requires a firmware change. If no file exists, it creates the user
override. Resolve the executable lookup location and icon entry against the selected firmware.

Uninstalling the association removes only this application's handler and preserves any other
SGF handlers. On-device acceptance must check both whether the stock file browser exposes `.sgf`
and whether opening one passes the complete path correctly, including spaces and Cyrillic names.
The installer is explicitly launched by the user; opening a game does not edit associations.

Application state lives in an application-specific writable directory. Its final path and update
strategy are builder decisions that must preserve the user's files and existing associations.
Installation instructions and any device configuration changes remain separate from normal viewing.

## Verification and release criteria

Portable tests cover SGF escaping and comments containing brackets/backslashes, nested branches,
setup stones, passes, captures, branch switching, metadata, multiple games, and malformed input.
Synthetic text fixtures cover every initial codec, `CA` aliases, missing/wrong declarations,
mixed encodings across game trees, ambiguous short text, invalid UTF-8 and manual override reset.
Input files must remain byte-for-byte unchanged during viewing and encoding changes.

Association tests cover an absent configuration, existing unrelated handlers, an existing SGF
handler, repeated installation, rollback and filenames requiring quoting. Reproducible release
builds use a documented SDK version and produce archive checksums. SDK archives and private SGF
records stay outside Git; only synthetic or explicitly redistributable fixtures are committed.

Before describing the first release as tested on Era 700, verify launching both ways, reading
a real legacy Cyrillic record, long comments, sibling variations, hardware keys, rotation,
refresh quality, close/reopen and suspend/resume on that device. Other models get their own
test reports. Desktop tests, cross-compilation and successful hardware checks are reported separately.

## References

- [PocketBook SDK 6.8 release](https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8): the publisher states firmware 6.8+ compatibility; this is a candidate, not yet a selected toolchain.
- [InkView native application examples](https://github.com/pmartin/pocketbook-demo).
- [SGF FF[4] specification](https://www.red-bean.com/sgf/sgf4.html).
- [SGF properties, including CA](https://www.red-bean.com/sgf/properties.html).
- [Existing PocketBook association implementation](https://github.com/koreader/koreader/blob/master/frontend/device/pocketbook/device.lua): reference for the firmware's configuration format only; no code reuse is planned.
