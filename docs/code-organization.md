# Source organization and naming

Production code is grouped by responsibility under `src/`. Includes use paths relative to that root, for example `#include "archives/externalarchivebackend.h"`. These are source packages within the existing `commander_ui` library, not separately shipped libraries or an enforced dependency hierarchy.

| Package | Responsibility |
|---|---|
| `app/` | Entry point, main-window orchestration, command registry and command handlers |
| `archives/` | Libarchive operations and sandboxed external archive backends |
| `filesystem/` | Native descriptors, file analysis/transforms/exports, comments, filename presentation and directory sizing |
| `network/` | FTP browser/transfers, transfer options and directory sharing |
| `operations/` | Local mutations, batch rename, job queue, operation logs and worker pool |
| `platform/` | Native extension records, associations, mounting and external program launching |
| `search/` | File-search requests and execution |
| `settings/` | Application, interface and panel preferences |
| `thumbnails/` | Native thumbnail providers and owned cache maintenance |
| `ui/dialogs/` | File content/search dialogs and file-operation dialogs |
| `ui/panels/` | Directory model, file panel, panel layout, folder-tab preferences and native path MIME |
| `ui/settings/` | Reference form rendering, configuration editors, appearance/toolbars and panel preference bindings |

## Responsibility names

`remainingoptions` was a translation unit containing `MainWindow` methods, not a class. It is now `ui/settings/configurationeditors.cpp`; its main entry point is `MainWindow::configurePreferenceEditors`. Names that described implementation increments have also been replaced:

| Previous name | Current name | Kind |
|---|---|---|
| `GapSettings` | `InterfacePreferences` | Namespace |
| `FinalSettings` | `ApplicationPreferences` | Namespace |
| `FinalArchives` | `ExternalArchiveBackend` | Namespace |
| `NativeSettings` | `NativeExtensions` | Namespace |
| `ExtendedTools` | `FileTransforms` | Namespace |
| `Productivity` | `FileExports` | Namespace |
| `Preferences` | `PanelPreferences` | Namespace |
| `FileTools` | `FileAnalysis` | Namespace |
| `RemoteTools` | `RemoteServices` | Namespace |
| `FileWindow` | `FileContentDialog` | Class: viewer and editor |
| `ReportDialog` | `TaskReportDialog` | Class |
| `ArchiveDialog` | `ArchiveBrowserDialog` | Class |
| `RenameDialog` | `BatchRenameDialog` | Class |
| `CompareDialog` | `DirectoryComparisonDialog` | Class |
| `RemoteDialog` | `FtpBrowserDialog` | Class |
| `QueueDialog` | `JobQueueDialog` | Class |
| `PanelHost` | `FilePanelHost` | Class |
| `ShareServer` | `DirectoryShareServer` | Class |
| `NativeFs::Fd` | `NativeFs::FileDescriptor` | Class: owned POSIX descriptor |

Services previously embedded in settings modules now have their own headers and implementation files: `OperationLog`, `ExternalPrograms`, `FtpTransferOptions`, `FileComments`, `FilenamePresentation`, `DirectorySizes`, `NativePathMime`, `FolderTabPreferences` and `ThumbnailProviders`. Panel preference bindings are separate from panel formatting/history storage. Event-filter and profile-lock implementation classes also describe their purpose.

Tests use responsibility names: `application_preferences_tests`, `interface_preferences_tests`, `command_dialog_tests` and `native_services_tests` replace `final_controls_tests`, `gaps_tests`, `remaining_gui_tests` and `remaining_tests`. All existing scenarios remain in the nine-suite CTest configuration.

## Compatibility and conventions

This is a source refactor. Saved settings keys (including the historical `final/` prefix), widget object names, command IDs, embedded reference inventories, transport formats and on-disk formats retain their existing values. No compatibility headers or aliases keep the obsolete internal C++ names alive. Source links in the documentation follow the new layout; frozen reference JSON retains its original audit data.

Use PascalCase for classes and service namespaces, camelCase for functions, and responsibility-based filenames matching the existing compact lowercase style. Use precise domain names for dialogs and services. Keep unrelated runtime services out of preference-binding modules, and avoid progress labels such as “remaining,” “gap,” “final,” or version numbers in production symbols.

## Validation

Both builds complete successfully. All nine CTest suites pass in Debug (62.54 s) and ASan/UBSan (52.83 s, leak reporting disabled). A fresh runtime disabled-control audit matches the pre-refactor 0.9 inventory exactly. Project include paths resolve from `src/`, production string literals match the pre-refactor source, `git diff --check` passes, and installed `--version` / `--help` smoke checks pass. The optional large-directory measurement was skipped.
