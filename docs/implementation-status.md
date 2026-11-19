# Implementation status

## Current increment

Version 0.9 connects the remaining 106 audited configuration inputs. The runtime audit records zero persistent disabled configuration and bar-editor controls; startup-only FTP disconnect/sharing actions remain conditional. Native Linux mappings, optional codecs/tools and implementation limits are described in [0.9 implementation](control-implementation-0.9.md). All nine CTest suites pass in Debug and ASan/UBSan (leak reporting disabled; optional large-directory measurement skipped), and installed 0.9.0 smoke checks pass. This resolves the original 299-control inventory, not full product parity.

Version 0.8 implements another 100 disabled controls from the 0.7 inventory: 79 configuration inputs, 20 bar-editor fields and native content-field search. That increment left 106 configuration inputs, subsequently addressed in 0.9. All eight CTest suites pass in Debug and ASan/UBSan builds, and installed 0.8.0 smoke checks pass. See [0.8 implementation and limits](control-implementation-0.8.md).

Version 0.7 implements 92 audited configuration inputs and bookmark configuration (93 of 299 disabled controls, 31.1%). All seven CTest suites pass in Debug and ASan/UBSan builds (leak reporting disabled; optional large-directory measurement skipped), and installed 0.7.0 smoke checks pass. See [settings implementation](settings-implementation-0.7.md). The frozen 0.6 audit remains the baseline; its then-remaining fields were handled in subsequent increments.

Version 0.6 adds the remaining baseline command/configuration/rename button handlers, native views and extensions, FTP/FTPS and sharing. The button/action inventory is distinct from the many reference settings fields and from Windows binary/proprietary protocol compatibility. See [remaining control implementation](remaining-controls-0.6.md) for actual behavior and limits. A subsequent [runtime disabled-controls audit](disabled-controls-audit.md) found 277 disabled configuration inputs, 20 disabled bar-editor inputs and two other unavailable functions; the earlier button inventory did not cover these.

Version 0.5 implements 62 of 155 disabled button/action instances from the frozen 0.4 runtime inventory (40%). This includes 21 of 53 main commands and functional toolbar, appearance, view-mode, search-result and rename controls. See [button implementation report](button-implementation-0.5.md). This percentage describes the disabled-button baseline, not final-product parity.

Version 0.4 adds a static 11.58.1 GUI reference audit, reference menu and button ordering, classic panel presentation, Brief view, configuration pages and revised search/multi-rename dialogs. See [GUI audit](gui-audit.md) for coverage and limits.

Version 0.3 adds archives, a mutation queue, multi-rename, comparison/synchronization, statistics/duplicates, permissions/links and advanced saved searches. See `advanced-tools.md` and `capabilities.md`.

Version 0.2 expands the Qt prototype into an internal local-manager alpha: recursive copy, same-device move/rename, mkdir/new file, trash/permanent deletion, viewer/editor, search, checksums and panel productivity controls. Phase 2 reliability gates remain incomplete; see `local-operations.md` for exact semantics and limits. All application code is independently written in this workspace.

## Architecture decision: provisional prototype

Use the plan's C++20/Qt Widgets option to produce a reviewable interaction prototype. The workspace began with only PLAN.md and contains no existing application to extend. This decision validates implementation mechanics; it is not evidence that a rewrite has lower total cost than adapting Double Commander.

The Phase 0 reuse gate remains open. No installed TC versus Double Commander comparison, license assessment, exhaustive command inventory, or plugin compatibility experiment has been completed. Keep the prototype replaceable until that evidence supports a production choice. No claim of exact visual or behavioral parity is made.

## Modules

See [source organization and naming](code-organization.md) for the complete package map, renamed classes and extracted services.

| File | Responsibility |
| --- | --- |
| `src/ui/panels/directorymodel.*` | Native entry identity, background enumeration, stale-result rejection, sorting, table presentation |
| `src/ui/panels/filepanel.*` | Per-tab directory state, selection, path input, history and contextual keyboard handling |
| `src/app/commandregistry.h` | Shared actions and shortcut dispatch |
| `src/filesystem/fileexports.*` | Selection files, atomic exports, bounded file lists and checksum manifests |
| `src/app/filecommands.cpp` | Additional reference command handlers, printing and system integration |
| `src/ui/settings/appearanceandtoolbareditors.cpp` | Persisted bar editing, fonts/colors and view-mode presets |
| `src/app/mainwindow.*` | Independent panel tab groups, menus, toolbar, directory command and session settings |
| `src/operations/fileoperations.*` | Descriptor-based local mutations, staged copy/save, conflict/cancellation handling and journals |
| `src/search/filesearch.*` | Bounded local glob/content search service |
| `src/ui/dialogs/filecontentandsearchdialogs.*` | Text/hex/image viewer, UTF-8 editor and recursive search dialog |
| `tests/commander_tests.cpp` | Temporary-filesystem and widget interaction regression scenarios |
| `tests/operation_tests.cpp` | Real local operation, failure, search and atomic-save regression scenarios |
| `src/operations/iothreadpool.h` | File-worker pool isolated from Qt's global rendering pool |
| `src/operations/jobqueue.*` | Serialized background mutations, cooperative controls, reordering, summaries and queue UI |
| `src/archives/archives.*` | Bounded libarchive reading/packing and descriptor-based staged extraction |
| `src/operations/batchrename.*` | Preview validation, cycle-aware temporary renames, rollback attempts and phase journals |
| `src/filesystem/fileanalysis.*` | Statistics, duplicate candidates, content/directory comparison and non-deleting sync |
| `src/ui/dialogs/fileoperationdialogs.*` | Archive browser, rename preview, directory comparison and analysis reports |
| `tests/advanced_tests.cpp` / `tests/queue_tests.cpp` | Archive/fault, rename, comparison/sync, inspection, permission and queue regressions |

The prototype has a local directory model rather than the final provider API. Avoid defining archive/remote contracts until stream, identity and capability requirements have fixtures. Entry operations, bookmarks and persisted tab paths preserve native bytes internally. Entered path text, display paths, desktop launch and desktop trash still have decoding boundaries documented in `README.md`.

## Reference manifest

`reference-manifest.json` records the intended TC version with collection state `pending`. Installer checksum, reference environment, font and screenshots are deliberately unset. Collect them from an actual installation; do not treat public feature documentation as executable reference evidence.

The parity matrix contains the 30 initial scenarios plus follow-up implementation scenarios. `implemented` means the prototype supports the scenario, not that its behavior has been compared with TC. `specified` means planned behavior that still needs implementation. Test identifiers refer to local regressions only; rows require independent reference observations before changing to `verified`.

## Next work

1. Complete the reference manifest and execute the 30 scenarios against TC and Double Commander; decide reuse versus production continuation.
2. Capture screenshots and evaluate focus, selection, header geometry and shortcut differences on actual GNOME/KDE desktops.
3. Add progressive first-row display and measure actual large-directory widgets, refreshes and interactive sorting. Bounded batch delivery, background final sorting, a model measurement fixture and same-directory cursor/selection restoration are implemented.
4. Complete the fault/recovery gate: actual ENOSPC/permission fixtures, process-kill recovery, concurrent replacement review, metadata policy and recovery UI.
5. Add cross-device moves only after safe source-validation/deletion and recovery behavior are specified; then extend recovery, archive compatibility and advanced-search parity.

## Validation scope

Version 0.6.0 passes all six CTest suites in native Linux Debug and ASan/UBSan builds, with leak reporting disabled and the optional large-directory measurement skipped. Workspace-prefix installation and installed `--help`/`--version` checks pass. See [0.6 validation and compatibility limits](remaining-controls-0.6.md#validation-scope). The historical increment notes below describe earlier checkpoints.

The test executable exercises hidden entries, directories-first sorting, failed and superseded navigation, persistent selection identity during sorting, keyboard selection and history, native Linux filename/link identity, symlink-parent semantics, panel switching, tab shortcuts and session restore. CI is configured to build and run the same suite on Ubuntu; the hosted workflow has not been executed in this workspace.

The initial increment was validated on Ubuntu 24.04 in WSL, GCC 13.3 and Qt 6.4.2. The debug build, offscreen regression suite, installation into a workspace-local prefix, and installed executable's `--help` and `--version` smoke checks pass. Desktop compositor behavior, visual equivalence, Debian packaging, actual 100,000-entry widget latency, and crash recovery remain unvalidated.

## Follow-up increment: reload state restoration

Same-directory reloads (refresh, hidden-file toggles and navigation to the same canonical directory) now preserve surviving selections and the current item by native path bytes, including undecodable filenames. State is captured immediately before the model reset so changes made while enumeration runs are included. The current column is retained; when the current item disappears, the cursor uses its previous row clamped to the new listing. Empty listings have no current item. Navigating to a different directory starts without selected rows.

Added regression scenarios for changed row positions, removed entries, empty listings, hidden-file toggles, different-directory navigation and distinct undecodable filenames. These scenarios now pass on the native Linux environment described below.

## Follow-up increment: bounded enumeration delivery

The worker delivers at most 256 entries per batch through a queue capped at two waiting batches. When the UI falls behind, the producer waits and checks cancellation at most every 25 ms while waiting. A UI timer consumes at most one batch per tick and updates the loading count. Superseded requests discard staged batches; closing a panel cancels a producer even when its queue is full. Final listing sorting runs in a worker thread; if the requested sort changes before commit, the staged listing is sorted again using the latest order.

The old listing remains usable until enumeration and sorting succeed, then the model commits the new listing and restores surviving selection/cursor identities. This bounds queued deliveries, not total listing memory. Old and staged listings coexist while loading. Progressive first-row visibility, interactive header sorting on large committed listings and large-directory widget/selection restoration timings remain open.

Regression tests cover batch sizes/counts, old-listing retention during delivery, changing sort order during enumeration, navigation from a batch callback and destruction with a waiting producer. The optional `largeDirectoryMeasurement` scenario creates 100,000 temporary regular files, measures a model-only cold application load with warm filesystem metadata, and samples the event loop with a 1 ms timer. It excludes fixture creation/deletion and is skipped by default. It does not establish the plan's visible-first-results target or desktop latency.

Current environment: native Linux Mint 22.3, GCC 13.3, Qt 6.4.2, Debug build, ext4 temporary directory; one exposed Intel Core i5-9600K CPU. Build directory: `build-linux` (separate from the migrated build cache). The offscreen regression suite passes. Desktop/compositor validation is still pending.

Final standalone measurement on this environment: first batch **7 ms**, completed listing **2,607 ms**, maximum sampled event-loop gap **35 ms**. These are one-run diagnostic measurements rather than release guarantees. The initial run exposed a roughly 1.2 s final-sort UI pause; moving final load sorting to a worker removed that pause in the follow-up measurement.

## Local-manager increment (0.2)

`capabilities.md` inventories the available commands and GUI. `local-operations.md` records the mutation, cancellation, metadata, editor and journal contracts. The original copy-failure gate now has executable conflict, source-change, destination-race, cancellation and constrained-write tests. Same-device move/rename and descriptor-based deletion have been added with separate tests and confirmations. Actual disk-full filesystems, hostile concurrent mutation, crash/power-loss recovery and metadata compatibility remain unresolved; this increment does not close the production reliability gate.

The viewer/editor, search and checksums perform file reads/hashing in worker threads. Search returns bounded results; viewers enforce input-size and image-allocation limits. The editor refuses changed source bytes and symlink paths and protects modified documents on Escape and application close. UTF-8 text rendering and image decoding still occur in widgets and require additional responsiveness measurements.

GUI additions include functional F3–F8 buttons, action menus/context menus, progress/cancellation, detailed results, active path colors, theme icons, selection byte/free-space status, quick filters, wildcard selection, bookmarks and properties. Shift+F4 creates and opens a new file. Sessions now store native tab-path bytes, retaining compatibility with earlier display-string settings. Desktop file association launching is available on Enter/double-click.

Validation: native Linux Mint 22.3, Qt 6.4.2, GCC 13.3, Debug/offscreen. Both regression executables pass, including real Copy/New Folder dialogs, an editor save and unsaved Escape protection, selection/filter/reveal, native-byte session restoration and a known SHA-256 digest. Hosted CI and GNOME/KDE compositor behavior remain unverified. No reference matrix row is marked `verified`.

Coverage snapshot: 33 of the 41 currently catalogued scenarios are implemented, 8 remain specified, and 0 are reference-verified. This denominator is the current scenario inventory, not the exhaustive final functionality. The latest tests additionally exercise cancelled-dialog focus, search result reveal, CRLF preservation and a 64-level directory copy/delete.

The Ubuntu 24.04 workflow in `.github/workflows/linux.yml` builds, runs both suites and checks a staged installation. It has not been run on GitHub from this workspace.

Final 0.2 validation: all **36 default functional scenarios** pass across the two regression executables; the separate 100,000-entry measurement remains opt-in. The complete suite also passes with AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`). Qt/system-library leak reporting was excluded; no leak-free claim is made. A workspace-local installation passes `--help` and reports version `0.2.0`. The main-window offscreen screenshot was inspected at `build-linux/screenshots/main-window.png`. Native GNOME/KDE rendering and compositor shortcuts still need desktop validation.

## Advanced local tools increment (0.3)

The mutation scheduler now serializes local operations and the new archive/rename/sync services while keeping panels usable. Jobs can be paused, resumed, cancelled and reordered while waiting. Histories include generic queue summaries plus incremental native-path records. Large local and batch jobs append per-item events and write a final snapshot, avoiding quadratic full-journal rewrites.

Libarchive provides native ZIP/TAR/TAR+gzip creation and supported-format reading. Extraction stages a private new directory, enforces path/type/count/byte limits, and publishes exclusively. Source identity/change checks and actual streamed data limits apply to browsing and extraction. Public 3.7.2 headers are vendored as a fallback for the runtime-only installation in this environment; notices are retained under `third_party/libarchive` and installed with the binary. CI/documented development setup installs `libarchive-dev`.

Batch rename previews counters, replacements/regex and case transforms; collisions/stale plans are refused. Cycles are resolved through temporary names with identity-checked rollback attempts. Directory comparison offers checked additive/update synchronization; right-only entries are retained, and source/destination/root snapshots are revalidated. File comparison, statistics, duplicate candidates, permissions and symbolic links are available through Tools/Files. Search adds regex, size/date filters and persisted profiles.

The new services and actual archive/rename/sync dialogs have local tests. Resource limits, partial-work behavior, metadata exclusions and unresolved concurrent-mutation/recovery boundaries are documented in `advanced-tools.md`. No final parity percentage or reference-verified status is inferred from the expanded scenario inventory.

Final 0.3 validation: all four regression executables pass on native Linux Mint 22.3 / Qt 6.4.2 / GCC 13.3. They exercise **68 default functional cases** (including data rows), with the separate 100,000-entry measurement still opt-in. The same four suites pass with ASan/UBSan (`detect_leaks=0`, `halt_on_error=1`); no leak-free or external-library certification is claimed. Tests include 128 MiB archive streaming cancellation, truncated/hard-link archives, a 512-entry rename publication race with rollback, stale synchronization destinations, saved profiles and actual archive/rename/sync/queue GUI controls.

The queue-window test exposed a real single-core rendering deadlock caused by occupying Qt's global pool. Every application QtConcurrent task now uses the dedicated four-thread file pool. The regression forces the global pool to one thread and verifies that painting, pause/resume and cancellation remain responsive during a long operation. This also isolates directory enumeration/sorting and read-only tools from Qt rendering work.

A workspace-local installed binary reports `0.3.0`, passes `--help`, and includes libarchive copyright notices. Offscreen screenshots of the main window, archive browser, multi-rename, directory comparison and background-operation queue were inspected under `build-linux/screenshots`. Native desktop/compositor behavior remains unverified. The current matrix has **54 implemented and 4 specified scenarios, with 0 reference-verified**; this is not a final-product parity denominator. Hosted CI has not been executed from this workspace.

0.4 validation: all four regression suites pass in Debug and ASan/UBSan builds, with Qt/system leak reporting excluded and the large-directory measurement left opt-in. Main and dialog resource inventories regenerate byte for byte from the frozen reference inputs. Installation smoke checks pass for version 0.4.0. See [GUI audit](gui-audit.md) for implemented coverage and remaining visual gaps.

0.5 validation: the frozen baseline contains 155 disabled button/action instances; the checked implementation list contains 62 (40%), including 21 of 53 main command actions. All four CTest suites pass in Debug and ASan/UBSan builds, with Qt/system leak reporting excluded. Installation version/help and independent --search startup smoke checks pass. Native printer/terminal/desktop integration remains a manual validation item.
