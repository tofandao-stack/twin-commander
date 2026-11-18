# Local operation contract (0.3)

This is an internal Linux alpha. These behaviors are implementation contracts tested locally, not observations of Total Commander. Phase 0 reference collection and the production reuse decision remain open.

## Copy

F5 copies selected entries (or the cursor item if nothing is selected) to a directory, defaulting to the opposite panel. The confirmation lists the sources and lets the user edit the destination and choose fail-on-conflict, skip-existing, or replace-regular-files. There is no implicit directory merge. Existing directory or symlink destinations cannot be replaced.

Each top-level entry is copied into a uniquely named `.tc-copy-*` sibling. Regular files use exclusive creation, bounded read/write buffers, source identity/size/mtime/ctime checks and fsync before publication. Directories are staged as complete trees. Symlinks, including dangling and cyclic links, are copied as links. FIFOs, sockets and device nodes are rejected. Traversal is limited to 256 nested levels. Source traversal refuses filesystem-device changes and destination-inside-source paths. Native filename and link-target bytes are retained.

The default commit uses Linux renameat2 with RENAME_NOREPLACE. A name created by another process while copying is retained and the operation reports an error. Explicit replacement checks that the previously observed regular file still has the same identity and metadata immediately before atomic rename. This check and rename are not one atomic compare-and-swap: concurrent writers after the final check remain a limitation of explicit replacement and editor saving. There is no directory locking or guarantee against hostile concurrent tree mutation.

Cancellation and failures remove uncommitted temporary output when possible. If cleanup fails (for example due to restrictive nested permissions), the report identifies remaining temporary output. Earlier completed top-level items remain applied. Incomplete output is never counted as a successful copy. A process crash can leave `.tc-copy-*` output; automatic cleanup/recovery is not implemented.

Basic mode bits and regular-file/directory timestamps are preserved. Ownership, ACLs, extended attributes, hard-link relationships and sparse allocation are not preserved. Special mode bits are stripped. File fsync is not a guarantee of power-loss durability for the complete job: directory entries are not durably committed as a filesystem transaction. Bind mounts sharing a device ID are not separately inventoried.

## Move, rename and creation

F6 moves into the chosen directory; Shift+F6 renames one selected entry. Both use no-replace rename. Skip-existing is available for moves. Cross-filesystem moves fail with the source retained; there is no automatic copy/delete fallback. Overwrite, directory merge, multi-rename and move resume remain pending.

F7 creates one directory, with a single-component name. Shift+F4 creates one new empty file using exclusive creation and opens the UTF-8 editor. Existing entries are never truncated by creation. Default permissions respect the process umask.

## Trash and permanent deletion

F8 or Delete asks for confirmation then uses Qt's desktop trash integration. It reports failures per selected entry. Trash currently requires a losslessly decodable top-level path; there is no fallback to permanent deletion. Trash cancellation takes effect between selected entries. Restoration is performed through the desktop trash interface.

Shift+Delete requires separate permanent-delete confirmation. Deletion traverses directory descriptors with no-follow opens and unlinks links themselves. It refuses device-boundary traversal and rejects root/dot/dot-dot entry paths. Cancellation stops remaining traversal; already deleted entries cannot be restored. Privilege elevation is not supported. This is not an adversarial filesystem sandbox or a transactional deletion service.

## Execution, reports and journals

One mutation job runs at a time, outside the UI thread. The background operations window (Ctrl+J) shows processed items and bytes, supports cooperative pause/resume and cancellation, and lets waiting jobs be reordered. Confirmation dialogs enqueue work; panels remain usable while it runs. The main window cannot close during an active mutation job. Completion refreshes panels without forcing application activation; skipped, failed and cancelled jobs produce detailed per-item reports.

Job intent and final outcomes use QSaveFile; per-item incremental outcomes are appended and flushed to a companion JSONL log under the application's local-data `jobs` directory. Native paths are base64-encoded in these JSON records. A finished flag distinguishes completed reports from interrupted jobs. Journal write failures are reported separately; they do not silently turn an operation into success or imply rollback. Journals are diagnostic, not a transaction log that promises resumability; previous-report inspection and a scheduler/queue are available; automatic recovery/resume remain pending.

## Editor

The editor accepts lossless UTF-8 without NUL bytes, up to 8 MiB. Saves run outside the UI thread. It compares the original bytes against the current regular file, writes a temporary sibling, fsyncs, checks identity/metadata again and renames the temporary file. Symlink editing is refused. Native filenames and basic permission bits are retained. Consistent CRLF line endings are preserved; mixed newline conventions may normalize through the text widget. Saving replaces a hard-linked file's directory entry rather than updating all links. ACLs, ownership and extended metadata are not guaranteed.

Unsaved changes are protected on Escape, window close and main-window close. Closing while a save is in progress is refused until the result arrives. There is no undo after a committed save; normal editing undo/redo is provided by the text widget.

## Validation

Tests exercise recursive bytes/permissions/link copying, undecodable names, conflict policies, self-copy and hard-link rejection, special-file rejection, cancellation before/during copy, destination races, source modification, constrained write failure (RLIMIT_FSIZE), same-device move/rename, cross-device move refusal, safe symlink deletion, isolated desktop trash, JSON outcomes and editor conflict refusal. Real disk-full mounts, process-kill recovery, network-mounted paths, hostile concurrent directory replacement, ACL preservation and power-loss durability remain unverified.

## Permissions and links (0.3)

Change permissions applies ordinary octal bits 000–777 only to the selected regular files/directories; it does not recurse or edit ownership. Symlinks and special files are refused. An O_PATH descriptor and `/proc/self/fd` anchor the chmod to the opened entry, including restoring a file whose mode was changed to 000. This requires the normal Linux proc filesystem. Source identity is checked before modification; no privilege helper is used.

Create symbolic link uses the selected absolute native path as its link target and creates an exclusive new link name, defaulting to the opposite panel. It never replaces an existing link/file. Relative-target editing and hard-link creation are not yet exposed.
