# Contributing

Era-SGF is at the design stage. Please discuss a feature or substantial change in an issue
before implementing it, and read [docs/design.md](docs/design.md) for the intended scope.

## Principles

- Keep the viewer independent of KOReader and of online services.
- Keep parsing, board replay and decoding separate from PocketBook-specific UI code.
- Use actual screen dimensions and device capabilities rather than model-specific pixel coordinates.
- Preserve original SGF bytes and existing device configuration.
- Add synthetic SGF fixtures for parsing, encoding or navigation changes. Do not submit private games,
  comments, device backups, SDK archives or credentials.
- Document which tests were run and distinguish desktop results from physical-device checks.

## Device reports

For a compatibility report, include the PocketBook model, firmware version, Era-SGF version,
installation method and steps to reproduce. An anonymised or synthetic SGF helps investigate
formatting and encoding problems. Report both file association and launching from Applications
when they behave differently.

The project currently has no build or test commands. These instructions will be updated when
the implementation and first reproducible toolchain are added.

Contributions to the original project code are accepted under the repository's MIT license.
Retain all notices when proposing a third-party dependency.
