# Twin Commander

A native Linux two-panel file manager, implemented in C++20 and Qt 6 Widgets according to [PLAN.md](PLAN.md). Version 0.9 is an internal local-manager alpha with file operations, viewer/editor and search. Total Commander parity has not been established.

## Build and run

On Ubuntu 24.04 or Debian 12:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build qt6-base-dev libarchive-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
./build/twin-commander
```

Optional native archive/thumbnail/FTP backends use `p7zip-full`, `bubblewrap`, `imagemagick` and `python3`; ordinary FTP also uses `curl`. Configure native alternatives in preferences as needed.

Optionally supply left and right directories:

```sh
./build/twin-commander "$HOME" /tmp
```

The executable needs a graphical Linux desktop. Automated widget tests use Qt's offscreen backend and do not require a desktop:

```sh
ctest --test-dir build --output-on-failure
```

An optional 100,000-entry model measurement creates temporary files and reports time to first batch, total load time and maximum event-loop gap:

```sh
QT_QPA_PLATFORM=offscreen TC_LARGE_DIRECTORY_TEST=1 ./build/commander_tests largeDirectoryMeasurement
```

To install the binary and desktop launcher into a local prefix:

```sh
cmake --install build --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is on `PATH` when using this prefix. The launcher uses the desktop's standard file-manager icon.

## Available behavior

- Independent directory panels with sortable Name, Size, Type and Modified columns.
- Background enumeration in batches of up to 256 entries, with at most two batches queued for delivery and a loading count. Final listing sorting runs in a worker thread. Superseded requests are cancelled; failed navigation retains the previous listing.
- Tabs, history, editable paths, hidden-file toggle, multi-selection, and directory navigation.
- Refresh and hidden-file toggles retain surviving selections and the cursor using native file identities. If the current entry disappears, the cursor uses the nearest remaining row.
- F3 viewer (UTF-8 text, hex and images), F4 editor with staged saving and unsaved-change protection.
- F5 recursive copy with explicit conflict policy, F6 same-device move, Shift+F6 rename, F7 mkdir and Shift+F4 new text file.
- F8/Delete desktop trash and separately confirmed Shift+Delete permanent deletion.
- Background operations with progress, cancellation, per-item reports and diagnostic JSON journals.
- Recursive glob/content search with reveal-in-panel, SHA-256 checksums and native file properties.
- Per-panel quick filters, wildcard selection, inverse selection, path clipboard and persistent bookmarks.
- A background operation queue with pause/resume, cancellation, waiting-job ordering, per-job results and report history.
- ZIP/TAR/TAR+gzip packing, archive browsing, filtering and guarded all/selected extraction into a new folder.
- Multi-rename preview with counter patterns, find/replace, regex, case conversion, collision checks and rollback attempts.
- Binary file comparison, recursive directory comparison and checked one-way synchronization that retains right-only files.
- Folder byte/allocation statistics, SHA-256 duplicate candidates, permission editing and symbolic-link creation.
- Search regex, size/date filters and saved profiles.
- Menus and toolbar share a command registry with stable command IDs.
- Tab paths, current tabs, splitter position and window geometry persist through Qt settings.
- Linux entry identities retain native filename bytes, including names that cannot be decoded for display. Symlinks are identified separately.

| Input | Behavior |
| --- | --- |
| Tab in a file list | Focus the opposite panel |
| Enter / double-click | Open a directory or launch the desktop file association |
| Backspace / Alt+Up | Parent directory |
| Alt+Left | Previous directory |
| Insert | Toggle current row and advance |
| Space | Toggle current row |
| Ctrl+T / Ctrl+W | Open / close active tab; retain at least one tab per side |
| Ctrl+L | Calculate occupied space |
| Ctrl+R | Refresh active panel |
| Ctrl+H | Toggle hidden entries in active panel |
| F3 / F4 | View / edit the cursor or selected file |
| F5 / F6 | Copy / move selected entries to a directory (opposite panel by default) |
| Shift+F6 | Rename one entry |
| F7 / Shift+F4 | Create folder / create text file |
| F8 or Delete | Confirm move to desktop trash |
| Shift+Delete | Confirm permanent deletion |
| Alt+F7 | Find files; glob/regex, content, size/date filters and saved profiles |
| Ctrl+M | Multi-rename preview |
| Alt+F5 / Alt+F9 | Create / browse archive |
| Ctrl+J | Background operation queue |
| Ctrl+U | Swap the current left/right directories |
| Ctrl+Shift+S | Restore the last quick filter |
| Ctrl+S | Focus quick filter; Escape clears it |
| Ctrl+A / Ctrl+Num− / Num* | Select all / clear / invert selection |
| Num+ / Num− | Select / deselect by filename mask |
| Ctrl+Shift+C | Copy selected display paths |
| Ctrl+D | Directory hotlist, including Add current directory |
| Alt+Home / Ctrl+I | Home / open active path in opposite panel |
| Alt+Enter | Native file properties |
| Ctrl+Shift+L | Focus directory command field |
| Ctrl+Shift+P | Search all commands, including additional Linux tools |
| Alt+F4 | Exit |

The directory command field supports `cd <path>` and `cd` for home. Relative paths resolve against the active panel. It does not launch programs or evaluate shell syntax.

## Current boundaries

Read [the local operation contract](docs/local-operations.md) before using this internal alpha on valuable data. Cross-filesystem moves, general copy directory merging, automatic crash recovery, remote access and plugins remain unimplemented. Archive mutation/encryption/nesting and bidirectional/deleting synchronization also remain pending. See [advanced tool contracts](docs/advanced-tools.md). Copies preserve basic permissions/timestamps and links, but not ownership, ACLs, extended attributes, sparse allocation or hard-link relationships. Destructive behavior has local regression coverage, not a completed fault/recovery certification.

The viewer/editor accepts regular files up to 8 MiB; hex preview is capped at 256 KiB. Editing requires lossless UTF-8 without NUL bytes. Content search examines regular files up to 8 MiB using literal UTF-8 bytes (ASCII case folding when case-insensitive) or UTF-8 text regular expressions, skips permission-denied subdirectories and symlinks, and returns at most 5,000 results. Symlink directories are not traversed. Trash and desktop launch require decodable top-level paths; operation identities and sessions preserve raw Linux bytes.

Listings are staged in bounded deliveries and become visible after enumeration and sorting succeed. Progressive first-row display and large-directory widget performance remain open; complete listing storage still grows with directory size. Filesystem watching, custom shortcuts and TC configuration import remain planned.

See the [capability inventory](docs/capabilities.md) for implemented features and remaining work.

See [implementation status](docs/implementation-status.md) and the [parity matrix](docs/parity-matrix.csv). Automated implementation tests are distinct from comparison against an installed TC reference.

Archive support links the system libarchive library. Public headers and license notices are included under `third_party/libarchive` as a build fallback for systems with only the shared runtime installed. The documented development setup uses `libarchive-dev`.

The main menu, toolbar slots and function-key captions follow the built-in English 11.58.1 reference. See the [GUI audit](docs/gui-audit.md) for evidence and remaining visual differences. Unsupported reference commands stay visible and disabled.

Version 0.5 implements 62 of the 155 previously disabled buttons/commands (40%), including editable button bars, appearance settings, saved view modes, selection files, file-list printing, checksum manifests, archive testing, additional rename masks and search-result panels. See the [button implementation report](docs/button-implementation-0.5.md) for the measured inventory and exact limits.

Version 0.6 implements the remaining buttons/commands from the frozen inventory, including FTP/FTPS, native PC sharing, split/combine, encoders, comments, views and settings editors. See [remaining control implementation](docs/remaining-controls-0.6.md) for Linux mappings and unsupported Windows-specific options. FTP requires `curl`; its loopback tests also use `python3` and `openssl`.

Version 0.7 implements 93 of the 299 controls in the subsequent full disabled-controls audit (31.1%), including display/formatting, icons/colors, tabs/session, history, quick search, automatic refresh and bookmark editing. See [settings implementation](docs/settings-implementation-0.7.md) for behavior and remaining gaps.

Version 0.9 connects the remaining 106 audited configuration controls. The runtime inventory now has zero persistently disabled configuration/bar-editor inputs. New work includes copy verification and I/O policies, comments, archive recovery and 7-Zip encryption, MODE Z and edit re-upload, wrapped/locked tabs, native thumbnail providers and language packs. Some backends require installed tools. See [control implementation](docs/control-implementation-0.9.md) for behavior, validation and Linux mappings.
