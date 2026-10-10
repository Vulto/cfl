# CFL Quality Audit and Engineering Plan

## Scope

CFL is a C23/ncurses dual-pane file manager with directory previews, file metadata, multi-selection, copy/move/delete/trash, bookmarks, fuzzy search, directory pickers, and optional Überzugpp image previews.

This audit prioritizes memory safety, filesystem correctness, deterministic behavior, and performance. A source-review finding is not considered a reproduced crash unless a test or runtime trace demonstrates it.

## Confirmed defects addressed on the audit branch

### Memory and path handling

- The former in-place `replace()` implementation used an incorrect `memmove()` length and attempted to expand newline-containing paths in buffers allocated to the original length. The selection path could therefore write beyond its allocation. The helper has been removed and replaced with bounded, reversible percent encoding for '%' and newline bytes.
- Selection records are read through a bounded line reader. It distinguishes clean EOF, I/O errors, overlong records, blank records, and final records without a trailing newline.
- Directory scanning now checks allocation growth for overflow, releases partial results on failure, initializes the output pointer, and detects `readdir()` errors.
- Directory checks now use `stat()` plus `S_ISDIR`; a broken symlink or FIFO is no longer misclassified as a directory simply because it is not a regular file.
- Path construction normalizes a trailing separator and reports truncation.

### Filesystem operations

- Copy/delete now check `fork()`, wait for the child even when interrupted, inspect exit status, pass `--` before paths, and retain the selection when an operation fails.
- Move checks destination-path truncation and retains the selection after failed moves.
- Rename and trash report failures and avoid clearing selection after partial failure.
- Selection removal uses case-sensitive path comparison and checks temporary-file write/rename failures.

### Preview lifecycle and serialization

- Image-preview JSON values are escaped before serialization, so quotes, backslashes, and control characters in paths cannot corrupt the JSON message.
- Überzugpp child cleanup waits for termination rather than immediately issuing a non-blocking wait.
- libmagic initialization is cached instead of loading the MIME database for every preview.

### Directory-list performance

- Directory type is now determined once per item before sorting. The previous comparator performed filesystem metadata queries during each sort comparison, causing repeated `stat()` calls on the same entries.
- Selection membership now uses a cached sorted index with binary search instead of reopening and linearly scanning the selection file for every displayed row.
- Window recreation now frees old ncurses windows; undersized terminals and failed window allocation are handled without dereferencing null windows.
- Parent-path handling now tolerates relative paths and root without null-pointer arithmetic.

## Remaining audit risks

These items require additional implementation and regression coverage before CFL should be considered production-hardened:

1. **Selection lookup complexity:** rendering now uses a cached sorted in-memory index and binary search rather than reopening and linearly scanning the selection file for every row. Each lookup still checks selection-file metadata; a generation counter or explicit invalidation at all mutation points could remove even that per-row `stat()`.
2. **Synchronous operations:** large copies, deletes, scans, metadata probes, and external programs can block the UI. Add progress/cancellation where safe, or run long work asynchronously while keeping UI state coherent.
3. **Header coupling:** `functions.h` contains global state and many function definitions and includes multiple implementation headers. Split state, filesystem operations, previews, selection storage, and UI into translation units with explicit interfaces.
4. **Filesystem race conditions:** paths can be replaced between inspection and operation. Prefer descriptor-relative APIs (`openat`, `fstatat`, `renameat`, `unlinkat`) where practical and do not treat a prior permission check as authorization.
5. **Partial-operation recovery:** copy/move/trash batches should report per-item outcomes and preserve only failed items in the selection rather than retaining a mixed list of succeeded and failed paths.
6. **Clipboard write durability:** selection-file writes should detect short writes, flush/close errors, and use an atomic replacement for bulk edits.
7. **Terminal edge cases:** test resize storms, failed `newwin()`, and modal dialogs wider than the terminal. Tiny terminals now exit cleanly, but all modal layouts still need cell-aware clamping.
8. **Preview limits:** test very large files, special files, unreadable files, malformed media, broken pipes, and missing optional programs. Avoid reading FIFOs/devices as ordinary previews.
9. **Unicode display:** byte-length truncation is not equivalent to terminal-cell width for wide and combining characters. Rendering should use wide-character APIs and cell-aware clipping.
10. **Permission handling:** avoid automatic `sudo` retries for every failed operation. Distinguish permission errors from invalid paths, missing files, read-only filesystems, and other failures.
11. **Signal and child handling:** centralize child-process lifecycle, handle `EINTR`, avoid zombie processes, and ensure a dead preview process cannot terminate CFL through `SIGPIPE`.
12. **Directory scan semantics:** retain a clear policy for hidden files, symlink traversal, permission-denied subdirectories, and errors during iteration.

## Regression suite

Run the core tests with:

```sh
cc -std=c2x -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror \
  tests/test_core.c -o tests/test_core
./tests/test_core
```

Run filesystem integration tests after installing ncursesw and libmagic development headers:

```sh
cc -std=c2x -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic \
  tests/test_filesystem.c -lncursesw -lmagic -o tests/test_filesystem
./tests/test_filesystem
```

The GitHub Actions workflow builds the application, runs both test binaries, and runs the core tests under AddressSanitizer and UndefinedBehaviorSanitizer.

### Minimum test matrix

- Path joining: root, trailing separator, spaces, Unicode, and insufficient buffers.
- Selection encoding: newline and percent signs, including literal strings resembling escape sequences.
- Line parsing: blank lines, exact-capacity lines, missing final newline, overlong records, and I/O errors.
- Directory listing: empty and populated directories, hidden files, allocation growth, and nonexistent paths.
- Type detection: regular files, directories, directory symlinks, file symlinks, and broken symlinks.
- Operations: child failure, permission denial, existing destinations, cross-device moves, partial batch failure, and cancellation.
- UI: tiny terminals, resize, invalid selection, empty lists, and rapid navigation.
- Previews: binary/text boundary, huge files, malformed image headers, paths with quotes/backslashes, and preview process termination.

## Performance comparison and design lessons

The following are upstream project references, not benchmark claims:

- [nnn](https://github.com/jarun/nnn): low-overhead, small-footprint design; plugin-based extensions and batch workflows.
- [fff](https://github.com/dylanaraps/fff): minimal keyboard workflow, fast filtering, shell integration, and low dependency count. The original repository is archived.
- [ranger](https://github.com/ranger/ranger): mature preview/opening integration, bulk rename, bookmarks, tabs, mouse support, and extensibility.
- [lf](https://github.com/gokcehan/lf): asynchronous operations, configurable commands/keybindings, and server/client workflows; its explicit non-features illustrate the value of delegating pager/editor tasks to existing tools.
- [superfile](https://github.com/yorukot/superfile): multi-panel workflow, configurable themes/hotkeys, bulk operations, and plugin support.

CFL already combines dual panes, text/hex/image previews, file information, fuzzy search, selection, bookmarks, and destination pickers. The main gaps are reliability and operational behavior, not simply the number of features.

Priority order:

1. Memory safety and filesystem-operation correctness.
2. Automated tests and reproducible CI.
3. Eliminate repeated filesystem queries and repeated selection-file I/O.
4. Non-blocking long operations with safe progress/error reporting.
5. Modular interfaces and explicit ownership.
6. Optional features only after the core test suite is stable.

## Merge policy

Do not merge this audit branch until GitHub Actions is green. A successful compile alone is insufficient: sanitizer tests, filesystem integration tests, and regression coverage must pass. Any crash claim must include a reproducer or sanitizer trace.
