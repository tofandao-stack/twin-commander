# Remaining control implementation — 0.6

This increment implements the remaining feature-backed push buttons, tool buttons and commands in the frozen [0.4 inventory](reference/disabled-buttons-0.4.json). It preserves the reference menu/button ordering. The [per-control status inventory](reference/control-status-0.6.json) distinguishes native adaptations and state-dependent controls. Unavailable prerequisites are not replaced by success messages: disconnect requires a connection, unshare requires a running share, and rename Start/Next/Undo follow the state of a real preview or completed operation. The tab-strip scroll arrow in the old inventory is a Qt overflow control, not an unimplemented feature.

The inventory contains 53 command actions, 73 configuration buttons, 14 button-bar editor buttons, 11 rename buttons and four search/overflow buttons. The 0.5 release implemented 62 of those entries; 0.6 adds the remaining command/configuration/rename implementations. This inventory does **not** certify all checkboxes, settings fields, TC plugin binaries or final-product functionality. Reference options without an implemented Linux behavior remain disabled.

## Local tools and views

- Split writes a new directory with ordered pieces and a SHA-256 manifest. Combine accepts that manifest or selected pieces in filename order, validates manifest names/sizes/checksums, streams into a private staging file, and publishes without overwriting. Changes, cancellation and malformed manifests remove staging output. Piece sizes are bounded at 64 MiB and count at 100,000.
- Encode/decode supports single-part MIME/base64, UUE and XXE. BinHex decoding validates the HQX alphabet, run lengths, header/fork lengths and CRCs according to [RFC 1741](https://www.rfc-editor.org/rfc/rfc1741). BinHex resource forks are refused rather than silently discarded. Encoding inputs are limited to 64 MiB; decoded BinHex expansion is bounded.
- Comments use Linux `user.twincommander.comment` extended attributes without following a final symlink. Filesystems that do not support them return an error. Comments view and native custom columns expose these values.
- Tree mode navigates real directories and uses the selected directory as its operation target. Root is excluded. Separate trees can be hidden, shared between panels, or displayed for both sides. Quick View embeds the existing bounded viewer.
- Thumbnail mode decodes supported raster images in workers, preserves the shared native-path selection model, supports dimensions, caption fields and fields by mask, and caches app-owned images/metadata. Compaction removes obsolete records from this cache; it is not a generic database-cleanup operation.
- Custom column sets support native path, permissions, numeric owner, parent, size, comments and executable content providers. File-type colors and hint templates affect actual model presentation.
- Rename adds native content-field tokens, case options, automatic counter width, case-sensitive/insensitive replacement and extension replacement. Next starts another preview on the renamed paths. Undo uses signatures captured when the rename completed; modified/replaced targets or new conflicts are refused. Undo does not overwrite external changes.

## Customization

- Configuration buttons now operate their editors/pickers: external tools, native extensions, custom columns, per-type colors, hints, ignore patterns, logging paths, thumbnail cache/fields, automatic view rules, view-mode colors/icons/secondary sorting/internal commands, keyboard bindings, aliases and completion sounds.
- View modes apply columns, sort/secondary sort, background/alternate/tab colors and tab icons. Automatic rules match a directory path and optional filename-presence sub-rules. Inactive panels defer focus-affecting view changes until activated.
- Hotkeys restore factory defaults before applying saved remappings. User aliases cannot replace built-in command names. Aliases execute from the command line without launching a shell.
- The Start menu stores internal commands and optional shortcuts. Supported reference command-name aliases are loaded from a reproducible, names/IDs-only TOTALCMD.INC audit. Custom menu JSON uses a `menus` array with `caption`, nested `items`, separators (`caption: "-"`) and registered command names or native IDs. Invalid/oversized menus are refused and the current menu is retained. Clearing the menu-file setting restores the reference menu.
- Internal open/view/edit associations and configured external viewers/editors execute programs with argument arrays. Desktop association changes use installed `.desktop` application IDs through `xdg-mime`.
- Configured external packers and native packer providers appear in Pack. They run without a shell, stage their output, reject links/special inputs and source changes, and publish without replacing an existing archive. Program exit failures, missing outputs, cancellation and time/output limits are reported. External unpacker configuration does not bypass the bounded libarchive extractor.
- Frequently-used directory save/delete controls operate a real navigation-usage snapshot, displayed in directory history. File-operation and FTP log paths are opt-in; logs avoid credentials. Completion sounds use an installed `paplay` or `aplay` executable.

## Networking and platform mappings

FTP/FTPS uses installed `curl` for cancellable directory listing, upload and download. FTPS uses certificate verification. Credentials are entered separately, passed through curl's standard-input configuration, and are not saved in profiles or placed in process arguments. Saved addresses reopen the credential dialog. Local downloads stage output and never replace existing entries; remote upload replacement requires confirmation when the listing contains the target. Cancellation/failure can leave a remote partial upload. URL-list downloads support HTTP(S) as well as FTP(S). The remote browser is a dedicated window, not a virtual remote filesystem inside the local panels.

Share/Unshare and Native PC Link provide tokenized, read-only HTTP sharing, bound to loopback by default. Listening on network interfaces requires selecting that option in the share dialog. Requests reject traversal, links and special files and preserve raw Linux filename bytes in URLs. Files stream with bounded socket buffers. This is **not** SMB publishing or Total Commander's proprietary PORT protocol, and public-interface HTTP traffic is not TLS-encrypted.

Network Connections and administrative SMB shares delegate to the desktop's installed GVfs/KIO handlers. Disconnect uses `gio` for session-owned mounts. Native filesystem providers can return an existing local mount path for the active panel. Volume label changes use the matching installed Linux filesystem utility and `pkexec` when needed; unsupported filesystems, absent utilities, authorization refusal and command failures are reported.

Registration Info explains this independently implemented application's status. Update checking accepts an explicit HTTPS JSON feed with `version` and `url` fields; there is no invented update service and no binary auto-installation. Language/plugin download buttons open the reference publisher's resource pages; downloaded Windows language packs or DLLs are not automatically imported/executed.

## Native extension contract

The plugin configuration buttons manage Linux executable providers, separately by family. Records contain `name`, `kind`, `mask`, `program` and `arguments`:

- `content`: returns bounded UTF-8 text; arguments support `{path}`. Built-ins are `comment`, `parent` and `size`.
- `viewer`: opens a matching file using `{path}`.
- `filesystem`: receives `{url}` and returns an existing absolute local mount path.
- `packer`: receives `{archive}` and selected sibling source names, and must create that archive.

Providers are user-configured executable code, with process time/output bounds. They are not sandboxed. This contract does **not** implement the Windows WCX/WFX/WLX/WDX ABI. Platform-specific Explorer/OLE options and unsupported reference settings remain unavailable rather than being silently mapped to unrelated behavior.

## Validation scope

Final 0.6.0 validation: all six CTest suites pass in both Debug and AddressSanitizer/UndefinedBehaviorSanitizer builds (109 Qt test cases passed per build, including setup/cleanup cases). Leak reporting was disabled for the sanitizer run. The optional 100,000-entry measurement was skipped. Installation to a workspace-local prefix and the installed executable's `--version` (0.6.0) and `--help` checks passed. Offscreen screenshots were captured under `build-linux/screenshots/0.6`; this does not establish compositor or pixel-level reference parity.

Regression tests cover the frozen command/configuration inventory, actual rename Next/Undo workflows, native field providers, custom menus, key bindings/aliases, automatic view rules, views/cache state, split/combine integrity and no-clobber behavior, encoding validation, and loopback sharing/FTP/FTPS. Physical desktop integration, authenticated third-party servers, filesystem relabeling, physical sound/printer devices and Windows binary/proprietary-protocol compatibility are separate from these local tests.
