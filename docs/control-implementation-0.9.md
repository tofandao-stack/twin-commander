# Remaining 106 controls — 0.9.0

This increment connects the **106 configuration controls** in the frozen [0.8 inventory](reference/disabled-controls-0.8.json). The fresh runtime audit reports **0 disabled configuration inputs and 0 disabled toolbar-editor inputs**; native-field search remains enabled. The three FTP/sharing actions are conditional on connection/share state. The audit also sees Qt’s hidden Help menu container; the visible Help button opens that menu. Counts describe that inventory, not full Total Commander compatibility. Several Windows-specific captions retain their reference wording but use the native Linux behaviors described below.

## Coverage

| Configuration page | Controls |
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

[Implemented controls](reference/implemented-controls-0.9.json) · [Runtime audit](reference/disabled-controls-0.9.json)

## Behavior and platform mappings

**Layout and navigation.** One or two drive bars, command-line focus policy, Qt gradient chrome, drive icons/tooltips and removable/network icon policies now apply. Long-name and lowercase 8.3 preferences change display only; native file identities remain intact. System filtering recognizes Samba DOS attributes. Explorer/OLE descriptions use native file metadata and `user.xdg.comment` / `user.comment` attributes. Executable icons use native desktop associations; Windows PE/LNK resource parsing is not provided.

The complete-tree option preloads a cancellable, bounded tree (100,000 scanned entries, 10,000 directories, depth 32, current device, no symlink traversal). Manual tree selection requires activation before navigation. Wrapped tabs use a scrollable grid with up to eight visible rows. Locked roots can be set from the tab context menu; the asterisk and return-to-root settings apply. Custom-column scopes organize All / Local Linux / Native providers; they do not load Windows filesystem plugins. Priority view modes resist automatic rule replacement. Space can calculate a directory's size; cursor scrolling can retain extra lines below it.

**Operation and comments.** Optional counting runs before copy/delete. Size preferences select fresh scans, a short-lived native size cache, or local-filesystem-only scans. The Everything caption selects this native cache, not the Windows service. The one-hour timestamp tolerance applies to comparisons. Comments support native xattrs and bounded `descript.ion` / `files.bbs` sidecars, preferred format, reading both formats and CP437. Sidecar writes preserve unrelated lines and refuse symlinks; metadata failures are reported. Comment propagation can fail after file data has committed and does not roll that data back.

**Copy/delete.** Configurable same/other-drive buffers, explicit device-pattern groups, USB policy and compatibility globs select native I/O behavior. Modes use kernel-assisted copy with fallback, sequential native buffering, or Qt buffering. Optional MD5 compares source and staged destination before publication. Directory timestamps and comments can be copied. The Explorer-delete caption selects Qt file deletion and refuses undecodable names; the native POSIX route retains opaque filename support. Trash and confirmation preferences apply to GUI operations. Standard mouse copy uses the application's raw-path MIME format; drops retain no-overwrite conflict policy. F8 accepts native file drops.

**Language and application lifecycle.** English, a partial Romanian sample, and bounded user JSON translation packs are selectable. Missing translations fall back to English; TC LNG parsing and full Romanian coverage are not claimed. Single-instance locking is per settings profile. Minimize-to-tray requires a desktop tray; without one the minimized window remains accessible. Update checking requires a configured HTTPS JSON feed, reports availability and does not install software. Viewer selection supports internal, configured-first and per-open prompting. Hotkey parameters support the native command subset described by the UI; unsupported combinations report an error.

**FTP.** MODE Z uses the embedded Python 3 adapter for FTP and verified implicit FTPS. It streams zlib transfers with time/output/expanded-size limits, cancellation, passive/active mode, ASCII policy and bounded retries for reads only. The LAN opt-out checks the connected peer address. Automatic re-upload is opt-in, tracks at most 32 successful GUI downloads in the current connection, and uploads after two stable changed-file samples. A failed upload is not retried until another edit. Stop, disconnect and a new connection clear tracking; credentials and watches are not persisted.

**Thumbnails and history.** External previews can use native stdout-PNG providers, freedesktop `.thumbnailer` entries, or a configured ImageMagick-compatible converter with filename masks. Right-click the provider checkbox to configure a native provider or converter. Processes and decoded images are bounded; unavailable tools fall back to ordinary icons. These are native alternatives to Explorer, Lister and Irfan/XnView APIs. Owned thumbnail cache entries can follow copy/delete or be pruned for unmounted volumes. Frequent-directory records support settings, a bounded JSON data file, or session-only storage.

**Archive readers.** ZIP/ARJ/LHA/RAR/ACE reader switches choose libarchive or a configured native extractor. Formats unsupported by the installed libarchive can use the external route. External reads require bubblewrap and a suitable native codec; there is no unsandboxed fallback. The extractor runs with a read-only host, network isolation and a writable private output directory; output is checked for links, special files, counts and expanded size before exclusive publication. Listing through this backend requires bounded extraction. Codec availability varies, especially for ACE. ARJ long-name policy can create unique DOS-style aliases.

**ZIP/TAR creation and recovery.** Internal ZIP, native 7-Zip ZIP compatibility backend, scratch-directory selection, stored DOS aliases and UTF-8/CP437/ASCII policies are connected. The ZIP-NT caption selects native 7-Zip rather than Windows APIs. TAR selects pax versus ustar compatibility. Partial ZIP mode exposes only verified prefix entries and reports truncation. Keep-damaged mode explicitly disables ZIP CRC verification and always warns that recovered files may be corrupt; path/type/count/size validation still applies. It does not recover arbitrary corrupt compressed data. Ordinary extraction retains strict CRC checks. Source files are not renamed by archive alias preferences.

**7-Zip creation.** Native 7z/7zz or configured executable/provider choices, preset/custom compression, method, dictionary, word size, solid/block, threads and bounded extra method options feed the packer. Encryption prompts for a password, supplies it over stdin, and enables encrypted headers. Passwords are not stored in settings or command arguments. Native providers and tool options depend on installed programs; Windows plugin binary ABI compatibility is not provided. Creation retains private staging and exclusive publication.

## Validation

All nine CTest suites pass in Debug (77.31 s) and ASan/UBSan (48.90 s, leak reporting disabled). The optional 100,000-entry measurement was skipped; all new final-controls scenarios ran. The new final-controls suite covers the complete 106-control binding inventory and Cancel staging; three copy methods/MD5/comments; CP437 sidecars and no-follow refusal; display aliases/system attributes/opaque deletion; strict versus recovery ZIP behavior; encrypted 7-Zip creation; sandboxed extraction; MODE Z list/download/upload and LAN opt-out; stable-edit re-upload and Stop revocation; locked/wrapped tabs, translation/profile locking; frequent-directory storage and native PNG providers. The installed binary reports 0.9.0 and passes `--help`; both Python scripts parse successfully, and `git diff --check` passes. Desktop tray/compositor behavior and real external FTP servers require a desktop/integration environment; loopback and offscreen tests do not establish those properties.
