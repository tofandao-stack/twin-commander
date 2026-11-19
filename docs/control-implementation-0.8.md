# Another 100 controls — 0.8.0

This release implements **100 additional controls** from the frozen 0.7 inventory of 206 disabled controls. **106 remain disabled**, all configuration inputs. All 20 previously disabled toolbar-editor fields and native content-field search now have implementations. These figures count UI controls, including related radio choices and the same field in both bar editors; they are not percentages of full Total Commander compatibility.

The original 299-control audit now has 193 resolved entries and 106 remaining. [Implemented inventory](reference/implemented-controls-0.8.json) · [Remaining runtime inventory](reference/disabled-controls-0.8.json).

## Implemented groups

| Area | Newly implemented controls |
|---|---:|
| Log file | 14 |
| FTP | 13 |
| Operation | 10 |
| Icons | 6 |
| View Mode | 5 |
| Thumbnails | 5 |
| ZIP Packer | 5 |
| Layout | 4 |
| Color | 4 |
| Font | 3 |
| Folder-Tabs | 3 |
| Refresh | 3 |
| Miscellaneous | 2 |
| Auto Switch Mode | 1 |
| Packer | 1 |
| Horizontal bar editor | 10 |
| Vertical bar editor | 10 |
| Search in native content fields | 1 |
| **Total** | **100** |

## Working behavior

**Toolbar editors.** Both bars support external executable buttons with literal argument arrays and a working directory, multi-image icon frame selection, button/icon sizes, flat and raised Qt styles, and per-button window-state flags. Executables can be absolute paths or names resolved through PATH. Arguments use Qt's command-line splitting; there is no implicit shell and no TC parameter-macro interpreter. Scripts are supported when explicitly chosen as executable programs. Button edits and presentation settings save on OK and are discarded on Cancel.

Minimize/maximize apply to newly opened internal Qt dialogs/windows. External-program window-state requests return an explicit error because generic desktop applications cannot be controlled this way across X11 and Wayland. Icon indexes select frames of image formats supported by Qt (such as GIF/ICO/TIFF); Windows DLL/EXE resource extraction is not implemented. The XP-style checkbox selects raised Qt borders, not Windows theme APIs.

**Logging.** Operation logs filter copy/move/rename/link events, deletes, folder operations, archives and native filesystem providers, separately by success/error/skipped status. Native filesystem-provider results use the filesystem category. Rotation supports never, UTC day changes, or a size threshold in KiB. Retention counts include the active log. New logs start with an application marker followed by escaped JSON lines. Existing unmarked logs can be appended but their unmarked rotations are not eligible for pruning. Pruning only considers regular, marked files in the configured log's owned rotation namespace; symlinks and unmarked files are excluded. Logging refuses a symlink as the active file. Logging is best effort and does not change a completed file operation's result; rotation is serialized inside this process, not a transaction across multiple application instances. The separate FTP protocol log retains its existing credential-redaction behavior.

**FTP.** Settings affect curl requests: bounded retry counts/delays, binary/ASCII/extension-based transfer modes (automatic text mode recognizes .txt, .csv, .html, .xml and .md), passive or active FTP, active port ranges and an external interface/address. Retries apply to listings and private staged downloads; mutating uploads are never retried automatically. The stall option retries stalled downloads safely with bounded curl low-speed/retry settings; it does not promise byte-range resume, and never resumes uploads. Transfer time/output/size bounds and verified FTPS remain in force. Download destination behavior can prompt for a new filename or use the browser's local directory without replacing existing files; remote names must be single safe components. Foreground transfers use a cancellable modal progress window, while background transfers leave the main window available. MODE Z remains disabled because this backend does not implement it.

**Internal archives.** ZIP normal/maximum/fastest/custom choices feed compression levels 6/9/1 or a validated level from 0–9 into libarchive. The archive-date option sets a newly created internal ZIP/TAR archive to the newest regular source file timestamp, excluding directory timestamps. Archives containing only directories keep their ordinary creation timestamp. External packers retain their own compression/timestamp behavior. Existing source validation, private staging and no-overwrite publication remain in force.

**Views and automatic rules.** Secondary sort, autorun command, background, alternate background and tab color can be edited directly in the same staged view-mode records used by the existing picker buttons. Colors must parse as Qt colors. Autorun accepts registered internal commands. The mode chooser assigns a saved mode to a selected automatic rule. Rule changes now stage until Apply/OK, including the existing Add/Delete/More/Less controls, so Cancel does not save a partial rule edit.

**Thumbnails.** Internal image and text thumbnail providers have independent switches and filename masks. Text previews read at most 4 KiB from a pinned, regular, non-symlink file and render into a bounded image; binary data containing NUL is excluded. The custom-caption switch controls native content fields beneath thumbnails. Explorer/OLE, Windows lister DLL and Irfan/XnView provider switches remain disabled.

**Layout, interaction and navigation.** Clickable breadcrumb parts navigate using original native path bytes; optional hover menus list at most 1,000 immediate child directories. The path editor remains accessible through its edit button. Flat interfaces and drive buttons change real Qt frames. Mouse settings provide right-button marking with a delayed context menu, left-button selection, icon-only marking (optionally limited to synthesized touch clicks), drag selection and single-click opening. Name-only renaming selects the stem in the real rename dialog. Panel path fields offer popup or inline filesystem completion. Volume switching can return to a remembered directory or always use the mount root.

**Appearance.** Light/dark preferences and the existing Dark/Normal command share one setting. Additional palette-role selectors edit actual Qt colors; the cursor-theme switch uses Qt's style highlight. Manual DPI scales fonts from an unscaled base, avoiding cumulative scaling. Automatic geometry scaling applies when reference configuration and toolbar dialogs are opened; reopening is needed after changing that option. Special-folder/native default icons use the Linux Qt provider. Hidden-name badges, pressed menu checks and drive-button spacing affect actual rendering. They do not load Explorer resources.

**Tabs, refresh and sound.** Tab titles can include a Linux mount-root prefix. File-type filters can stay separate per tab or follow tab switches. Ctrl+Shift+W closes all other tabs and honors the confirmation preference while keeping the current tab. Refresh exclusions and session-save exclusions accept semicolon-separated Linux directory globs; explicit manual refresh still works. Completion sounds can be limited to jobs exceeding a configurable duration and still require an installed paplay/aplay backend.

**Search.** The Plugins tab filters results by a built-in or configured native content field and a substring, respecting the case-sensitive switch. Filename/content/size/date filters still combine with it. Providers retain their existing bounded process contract and are trusted user-configured executables, not Windows DLLs. Cancellation is checked between provider calls; an in-flight provider remains bounded by its own timeout.

## Remaining scope

The 106 remaining entries are listed with page, ID, type and caption in the runtime inventory. They include copy/delete policy options, advanced ZIP/7-ZIP settings, language loading, legacy comment formats, Windows-specific shell integration and other reference-only settings. Native adaptations above count as functioning controls; they do not establish binary or protocol parity with Total Commander.

| Remaining page | Disabled controls |
|---|---:|
| Layout | 3 |
| Display | 9 |
| Icons | 5 |
| Folder-Tabs | 3 |
| Custom Columns | 1 |
| View Mode | 1 |
| Language | 1 |
| Operation | 14 |
| Edit/View | 2 |
| Copy/Delete | 16 |
| FTP | 2 |
| Thumbnails | 8 |
| History | 1 |
| Packer | 7 |
| ZIP Packer | 11 |
| 7-ZIP Packer | 15 |
| Misc. | 7 |
| **Total** | **106** |

## Validation

The new gaps suite checks the 100-control reduction, cancellation of staged preferences, log filters/rotation/retention/no-follow behavior, bounded curl options with no upload retries, ZIP compression and timestamps, native-field search, text thumbnails, external toolbar argument literalness/working directory/presentation, breadcrumbs, mouse marking and direct mode/theme/DPI preferences. Existing suites continue to cover queued file operations, FTP/FTPS loopback transfers, sharing, search, rename and native path identity. All eight CTest suites pass against the final source in Debug (32.20 s) and ASan/UBSan (43.49 s) builds. Leak reporting was disabled for sanitizer runs; the optional 100,000-entry measurement was skipped. Workspace-prefix installation and installed `--version` (0.8.0) / `--help` checks pass. Desktop compositor/window-manager integration, external active-FTP servers and physical sound output remain outside these offscreen/loopback tests.
