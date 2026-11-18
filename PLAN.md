# Total Commander parity on Linux — implementation plan

Planning date: 2026-11-17. Workspace is empty; this plan assumes a new project.

## 1. Product target and definition of 1:1

Build a native Linux desktop file manager that reproduces Total Commander's layout, keyboard workflows, commands, dialogs, configuration concepts, and observable file-operation behavior. Use independently implemented code and project-owned branding and artwork.

Freeze the reference at Total Commander **11.58.1 for Windows**, identified on the [official release page](https://www.ghisler.com/whatsnew.htm). Record installer checksum, architecture, language, configuration, Windows display scaling, and fonts in the reference manifest. Changes in later releases become separately scheduled work.

Treat parity as four independently measured targets:

| Target | Acceptance approach |
| --- | --- |
| Visual | Reference screenshots and geometry checks for every catalogued screen at fixed fonts, viewport, and scaling; approve rendering differences explicitly. |
| Interaction | The same action sequence produces the same focus, selection, navigation, dialogs, and command results. |
| Functional | All inventoried built-in features have passing reference scenarios or an explicitly documented Linux adaptation. |
| Compatibility | Each configuration format, command parameter, archive format, protocol, and plugin is covered by its own compatibility entry. |

A strict universal 1:1 claim cannot be made before the inventory is complete. Windows-specific integrations need Linux mappings; Windows plugin binaries need separate feasibility work. An MVP is an intermediate milestone, not completion of the parity objective.

Initial support target: Linux x86_64; Ubuntu 24.04 LTS and Debian 12 as baseline systems; GNOME and KDE; Wayland and X11. Validate these assumptions during discovery. Add ARM64 after the baseline passes.

## 2. First decision: reuse or implement

Spend the first two weeks comparing a Double Commander extension/fork with a new implementation. Double Commander already describes dual panels, tabs, background operations, archive browsing, remote protocols, and TC plugin interface support on its [official site](https://doublecmd.sourceforge.io/). Its [FAQ](https://doublecmd.github.io/doc/en/faq.html) is a starting point for platform compatibility checks. These claims do not establish exact TC parity.

Run the same 30 representative workflows on TC and Double Commander. Measure layout and behavior gaps, inspect the affected code, assess contributor familiarity, and examine maintenance and distribution license requirements. Its [development documentation](https://github.com/doublecmd/doublecmd/wiki/Development) and [license](https://github.com/doublecmd/doublecmd/blob/master/LICENSE.md) must inform the staffing and distribution decision. Estimate the cost of closing gaps without destabilizing existing features.

Decision gate: adopt Double Commander if its architecture and license fit the product and demonstrably reduce total parity work. Otherwise proceed with the architecture below. A fork keeps the upstream implementation language and build stack; do not combine a fork with a wholesale rewrite. Record the decision and evidence before production implementation.

## 3. Parity inventory

Create `docs/parity-matrix.csv` during discovery. Each row needs: feature ID, reference version, source/help topic, preconditions, exact actions, expected screen and filesystem state, priority, platform mapping, implementation owner, test ID, and status. Statuses: unexamined, specified, implemented, verified, adapted, blocked. Report verified coverage and adaptations separately.

The [official feature list](https://www.ghisler.com/featurel.htm) establishes these broad areas: configurable commands and menus; file viewing; thumbnails and columns; bulk renaming; file operations; comparison and synchronization; encoding and splitting; duplicate and content search; FTP/FTPS; archives. Use installed help, menus, settings, release notes, and observed behavior to enumerate details beyond that overview.

| Area | Planned inventory and implementation scope | Initial delivery |
| --- | --- | --- |
| Main window | Two panels, splitter, paths, volume controls, menus, toolbars, status, command line, function-key bar | MVP |
| Panel state | Tabs, history, favorites, sorting, filtering, hidden files, selection masks, selection restore, cursor retention | MVP |
| Keyboard behavior | All default bindings, contextual dispatch, user bindings, mouse selection modes, command-line interactions | Core first; exhaustive parity later |
| Local operations | Copy, move, rename, mkdir, trash, permanent delete, overwrite policies, queues, progress, cancellation | MVP |
| Views | Brief/full/tree, branch view, quick view, thumbnails, custom columns and view switching | After MVP |
| Search | Name/content/regex, dates and sizes, duplicates, archives, saved searches, results as a panel | After MVP |
| Viewer/editor | Text, hex, binary, encoding detection/override, images, large files, external editor integration | Basic viewer in MVP |
| Productivity | Multi-rename, comparison, editable diff, directory synchronization, checksums, split/join, encodings | After MVP |
| Archives | Browsing, extraction, creation, modification, nested access, archive-to-archive operations, encryption, multipart cases | Common formats first |
| Remote access | FTP/FTPS behavior, proxies, resume, queues, FXP where supported; SFTP and SMB as Linux capabilities | After local engine |
| Customization | Commands, parameter expansion, menus, toolbars, associations, colors, fonts, language and help | Progressive |
| Configuration | Import/export of supported INI, toolbar and menu structures; persistent sessions and settings | Progressive |
| Extensions | Packer, filesystem, viewer, content-provider interfaces; individually inventoried plugins | Late compatibility phase |
| Platform-specific | Mounts, permissions, ownership, links, desktop launch, privilege escalation, native equivalents of Windows integrations | Across phases |

This table is a discovery checklist, not a claim that the entire feature set has already been specified. Legacy and unusual features remain visible in the matrix until implemented or explicitly accepted as adaptations.

## 4. Architecture if building from scratch

Recommendation: **C++20, Qt 6 Widgets, CMake, Ninja**. Use a custom table model and delegates to reproduce dense panel layouts and selection behavior. Qt documents the separation of data, views, and delegates in its [model/view guide](https://doc.qt.io/qt-6/model-view-programming.html), and documents Linux platform/build considerations in [Qt for Linux](https://doc.qt.io/qt-6/linux.html). This stack choice is an engineering recommendation, subject to the prototype gate.

Pin dependency versions after validating availability on the baseline distributions. Evaluate archive and network libraries with real fixtures before committing to them; retain adapters so unsupported operations can use isolated external tools with explicit argument arrays.

| Module | Responsibility and boundary |
| --- | --- |
| Desktop UI | Windows, panels, dialogs, rendering, accessibility, focus; no direct transfer loops |
| Command registry | Stable command IDs, shortcut contexts, enabled state, parameter expansion; shared by menus, toolbar and command line |
| Panel/session model | Active panel, tabs, cursor, selection and history using stable item identities |
| Resource providers | Local, archive, remote, search-result and plugin locations behind capability-aware enumeration, metadata and stream APIs |
| Operation engine | Plan and execute jobs, resolve conflicts, schedule I/O, cancel, publish progress and persist outcomes |
| Tools | Search, rename, compare, sync, viewer and checksum services |
| Platform adapter | POSIX metadata and filenames, mounts, desktop integration, secrets, privilege boundary |
| Extension host | Versioned plugin API, discovery, process isolation where practical, timeouts and crash reporting |
| Configuration | Settings schema, migration, TC import adapters, session storage and diagnostics |

Use provider capabilities to distinguish rename from copy/delete, writable from read-only archives, and resumable from non-resumable streams. Preserve raw Linux filename bytes internally and decode only for presentation. Never require a resource to fit a local filesystem path.

All slow work runs outside the UI thread; results arrive in bounded batches. Cancel stale directory requests. Persist job intent and per-item outcomes, but inspect the destination and provider before offering recovery: a journal cannot guarantee transactionality or universal resume.

## 5. Linux behavior and data integrity

Specify these before implementing destructive operations:

- Map drive selectors to mounted volumes, root, home, removable devices and configured remote locations.
- Handle case-sensitive names, undecodable filename bytes, symlinks, dangling links, hard links, sparse files, ownership, permissions, ACLs and extended attributes. Distinguish required preservation from best-effort metadata and report failures.
- Keep links distinct from their targets. Detect traversal cycles and reject self-copy and destination-inside-source recursion. Specify mount-boundary policy.
- For same-filesystem moves use suitable rename semantics. For cross-filesystem moves, complete and validate the copy before deleting the source. Where supported, write to a temporary destination and rename after completion; document durability limits.
- Define behavior for source changes during a copy, destination races, disk-full, read-only mounts, disconnects, permission failures and cancellation. Preserve recoverable partial work without reporting false success.
- Provide trash and permanent-delete paths with appropriate Linux semantics. Use a narrow privileged helper only for operations requiring elevation.
- Preserve familiar shortcuts and expose mappings for desktop-reserved keys; verify behavior on each supported compositor. Wayland's [shortcut-inhibition protocol](https://raw.githubusercontent.com/wayland-mirror/wayland-protocols/main/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml) leaves some shortcut control with the compositor, so exact key delivery is a platform constraint to validate. Launch external programs with argument arrays and define explicit shell mode separately.
- Import only understood TC settings. Produce a migration report for unsupported values and Windows executable paths. Store secrets through a desktop credential service, outside plain configuration files.
- Extract archives defensively: reject path escape and unsafe link targets; bound extraction and preview resources. Plan synchronization before executing it and show ambiguous or destructive actions.

## 6. Delivery phases and exit gates

Estimates below assume 3–4 experienced engineers plus dedicated QA involvement. They are planning ranges, not commitments; re-estimate after discovery and each gate. Durations describe engineering stages, some of which can overlap after their dependencies pass.

| Phase | Duration | Deliverable and exit gate |
| --- | --- | --- |
| 0. Reference and feasibility | 2–3 weeks | Reproducible TC reference environment, initial matrix, 30 recorded workflows, Double Commander comparison, plugin feasibility notes, architecture decision |
| 1. Interaction prototype | 3–4 weeks | Two panels, custom rendering, tabs, sorting and shortcut/focus model; screenshot review and keyboard scenarios pass; large-directory prototype stays responsive |
| 2. Reliable local manager | 6–8 weeks | Local provider, operation engine, conflict dialogs, queue, trash, persistence and basic viewer; copy/move/delete fault scenarios pass |
| 3. Daily-use MVP | 4–6 weeks | Favorites/history, basic search, common archives, settings and installable package; pilot users complete reference workflows without blocking defects |
| 4. Advanced parity | 8–12 weeks | Full search, rename, compare, sync, extended views and remaining built-in tools; corresponding matrix rows verified |
| 5. Remote and integration | 6–10 weeks | Network providers, credentials, reconnect behavior, mounts, desktop integrations and configuration migration; network fault suite passes |
| 6. Extensions and remaining gaps | 8–16+ weeks | Native extension SDK and selected plugin compatibility, legacy features, dialog/menu/shortcut audit; all remaining matrix rows resolved |
| 7. Release hardening | 4–6 weeks | Packaging, accessibility, localization, performance, recovery and compatibility regression suites; release gates pass on supported systems |

Critical path: reference inventory → architecture decision → panel/command contracts → local operation engine → provider integration → exhaustive parity audit. UI specialists can work on advanced dialogs while engine specialists complete transfer behavior after shared contracts stabilize.

Planning envelope: roughly **4–6 months for a dependable daily-use MVP**, **12–18 months for broad built-in parity**, and **18–24+ months for exhaustive parity and selected legacy/plugin compatibility**, with the stated team. These are judgment estimates. A solo project needs substantial scope reduction or a longer schedule. Arbitrary Windows plugin compatibility has no credible fixed estimate until feasibility and the supported plugin list are established.

## 7. Plugin strategy

TC's [official plugin page](https://www.ghisler.com/plugins.htm) documents third-party extensions and interface guides. The [WCX SDK](https://ghisler.github.io/WCX-SDK/overview.htm) specifies Windows DLL loading, and the [Double Commander FAQ](https://doublecmd.github.io/doc/en/faq.html) explicitly distinguishes Windows plugin binaries from Linux ports. Inventory actual required plugins by interface, version, architecture, dependencies and redistribution terms.

Keep three promises distinct: native equivalents of plugin capabilities; source-portable interface compatibility; and execution of existing Windows binaries. Implement the first two where justified. Treat a Wine-based helper for selected binaries as an experimental track; UI embedding, calling conventions, dependencies and host callbacks require explicit prototypes. Never make general binary compatibility a prerequisite for the local-manager MVP.

Gate this track with one real plugin from each required interface family. Verify enumeration, streaming, progress, cancellation, Unicode/filename conversion, crashes and cleanup. Publish a per-plugin support table; isolate incompatibilities and do not infer ecosystem compatibility from one passing sample.

## 8. Validation and completion criteria

Use shared fixture descriptions to run equivalent action sequences against the reference and Linux implementation. Compare observable outcomes rather than Windows and Linux filesystem internals. Keep baseline screenshots, expected metadata and differences versioned alongside the tests.

- Unit and property tests: selection state, command expansion, sorting, path handling, rename plans and sync decisions.
- Integration tests: real temporary filesystems, archives and disposable remote servers; test low disk space, denied permissions, disconnects, source changes and process interruption.
- UI tests: keyboard-only workflows, focus after dialogs, mouse selection, drag/drop, scaling, accessible names and screen-reader navigation.
- Performance fixtures: directories with 100,000 entries, multi-gigabyte files, many small files, deep trees and slow providers. Initial target: visible first results within 500 ms on a declared local SSD fixture, UI event latency below 100 ms during background work, and copy throughput within 15% of a comparable platform tool under the same metadata/durability settings. Measure and adjust targets at the prototype gate.
- Compatibility coverage: baseline distributions/desktops, X11/Wayland, supported filesystems and network providers; use VMs or real desktops for compositor behavior that headless tests cannot prove.

Parity release gate: every inventory row has a passing test or a reviewed, published adaptation; every required shortcut/dialog workflow passes; no unresolved known data-loss defects; failure and recovery behavior is verified; all supported packages install, upgrade and uninstall successfully. Publish coverage counts and adaptations, and reserve an unqualified 1:1 claim until accepted differences meet the agreed definition.

## 9. First implementation backlog

1. Freeze the TC reference manifest and collect installed help, command IDs, shortcuts, settings and screenshots.
2. Create the parity matrix and 30 reproducible scenarios, including overwrites, interrupted moves, archive edits and focus restoration.
3. Run the Double Commander gap study and document the reuse decision.
4. Prototype panel rendering, selection/focus, 100,000-entry enumeration and one plugin per required family.
5. If the new implementation wins, establish the CMake build and Linux CI, then implement the command registry and panel state model.
6. Implement local enumeration and navigation, followed by copy-to-opposite-panel with progress, conflict resolution and cancellation.
7. Verify transfer failure semantics before adding move and deletion.
8. Ship the first installable internal build and update estimates using measured progress.

Implementation has started with a replaceable Qt interaction prototype. See `README.md` and `docs/implementation-status.md` for implemented behavior, validation, and gaps. Phase 0 reference collection and the production reuse decision remain open; the prototype does not establish parity.

The 0.2 local-manager increment adds staged local operations, same-device moves/rename, trash/deletion, a bounded viewer/editor, filename/content search and panel productivity controls. See `docs/capabilities.md` and `docs/local-operations.md`. This advances Phase 2 implementation without closing its fault/recovery gate or the Phase 0 reference/reuse gate. No percentage of exhaustive final parity has been established.

The 0.3 increment adds bounded archives, a serialized mutation queue, previewed multi-rename, content/directory comparison with non-deleting one-way synchronization, folder statistics, duplicate candidates, permission/link commands and saved advanced searches. See `docs/advanced-tools.md`. Cross-filesystem move/recovery and exhaustive compatibility remain separate gates.
