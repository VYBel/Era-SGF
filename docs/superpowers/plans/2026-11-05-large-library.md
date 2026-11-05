# Full-archive library implementation plan

Goal: open the complete local collection (117,245 SGFs), preserve viewed/resume state, and measure time and peak memory. User approved the incremental-library approach in chat.

Architecture: retain the native C++14 catalogue, add indexed lookups and incremental refresh. Stream snapshots; store small atomic generation-bound change batches between snapshots. No new runtime dependencies. Keep compatibility with v1 catalogues; version 1.2.0. No reader installation or Git publication in this task.

Alternatives: increasing the constants leaves quadratic work and repeated full writes; SQLite would add a new reader dependency and migration. First measure the bounded native implementation on the full collection.

- [x] Add regression coverage for unchanged/forced refresh, migrated state, interrupted change batches, large catalogues, cached filters and mutation failures.
- [x] Add file stamps and path index; incremental refresh with explicit full verification option; linear matching of external moves.
- [x] Add streaming atomic snapshots and generation-bound atomic delta persistence; invalidate query cache on changes.
- [x] Update UI refresh/random entry points, version and documentation.
- [x] Benchmark baseline (only limits lifted in a disposable copy) and new implementation on the exact collection: initial index, reload, unchanged refresh, full/filtered/cached queries, viewed write. Report RSS and disk size, clearly separate desktop and reader results.
- [x] Run full tests, sanitizers, native ARM build and independent review. Preserve private archives outside Git.

Risks: coarse FAT timestamps require an explicit forced refresh; metadata-matched external edits can escape incremental detection. Old snapshot plus stale delta must not replay changes after compaction. Failed writes must preserve in-memory and durable state. Symlinks must remain rejected. Cold device I/O and actual reader RAM cannot be inferred from host measurements.
