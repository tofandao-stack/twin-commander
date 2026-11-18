# Advanced tool contracts — 0.5 internal alpha

These are local implementation contracts, not independently verified TC behavior. They extend `local-operations.md`; they do not close the production reliability/recovery gate.

## Background operations and reports

File workers use a dedicated four-thread pool so long or paused operations cannot occupy Qt's global rendering pool. The queue-window regression forces the global pool to one thread to exercise this isolation.

Copy, move, creation, trash/delete, permission/link changes, archive jobs, batch rename and synchronization share one serialized background queue. The panels stay usable and new confirmations can enqueue additional work. Ctrl+J opens the operation table with state, processed-item and byte counters, pause/resume, cancellation, waiting-job ordering and detailed outcomes. Finished entries can be cleared from memory. Closing the application is refused while a job is queued, running or paused; use the queue to cancel or finish it first.

Pause and cancel are cooperative checkpoints, not immediate interruption of a blocking filesystem/library call. Cancellation wakes a paused operation. Rollback/cleanup attempts intentionally continue after cancellation. Cancelling a waiting job does not run its work. A failed job does not block the next waiting job. Jobs are not retried or resumed automatically after restart.

Queue summaries persist labels, states and bounded outcomes in the local-data `jobs` directory. Local operations also persist full native-path intent, an append/flush JSONL outcome stream and a final JSON snapshot. Batch rename records every temporary name and phase similarly. Previous job reports displays completed/interrupted snapshots and bounded incremental records. Queue summaries alone are not a replayable intent format for every tool. The logs do not make filesystem mutations transactional or power-loss durable. No automatic recovery or deletion of temporary output is provided.

## Archives

The system libarchive shared library supplies archive codecs. Browse archive (Alt+F9, or Enter on common archive extensions) lists normalized paths, declared size, type and extraction restrictions; a substring filter narrows the view. The table displays up to 10,000 rows. ZIP, TAR and TAR+gzip creation are exposed by Alt+F5. Other formats supported by the installed libarchive may be browsed; exact format/encryption support is not a compatibility promise.

Creation accepts regular files/directories with UTF-8 names, including empty directories. It refuses links, special files, duplicate output names and destinations inside a source directory. Sources are streamed with identity/metadata checks. A temporary sibling is fsynced and published using no-replace rename. Existing archive files are retained. Source hard-link relationships, ownership, ACLs and extended metadata are not encoded as a preservation guarantee.

All/selected extraction always creates a **new** destination folder; it never merges into an existing directory. Selecting a directory includes its descendants. The reader rejects parent traversal, absolute paths, backslashes/drive-like roots, overlong/deep names, conflicting duplicate paths, symbolic/hard links and special entries. An unsafe entry causes extraction of the whole archive job to fail, including when that entry was not selected. Output components are created/opened relative to directory descriptors with no-follow flags. Files use exclusive creation. Nothing is published until the stream validates and the source archive's identity/metadata still match.

Default limits: 100,000 headers, 128 path levels, 4 GiB expanded bytes in the entire archive and 512 MiB per file. Listing and skipped entries are also streamed through bounded buffers to enforce actual expansion limits and permit cancellation. Browsing therefore validates payloads and can take longer than reading a ZIP directory alone. Archive file size, expanded bytes and displayed row counts are different limits. Encrypted/multipart/nested archives, archive edits and arbitrary plugin codecs are not provided.

Extraction deliberately uses ordinary directory/file permissions (0755/0644 inside a private 0700 root) rather than trusting archive privilege bits, ownership or links. Original timestamps and metadata are not restored. Errors/cancellation remove staging output where possible; cleanup failures identify its location. A process crash may leave `.tc-extract-*` or `.tc-pack-*` siblings for manual inspection. No guarantee is made against a hostile same-user process replacing directory paths concurrently.

## Multi-rename

Ctrl+M previews up to 10,000 selected sibling entries. The GUI separates name and extension masks: `[N]` is the stem, `[E]` is the extension, and `[C]` is the counter with start/step/width. Its default preserves extensionless names. Name/extension/full-path ranges, parent name, size, modification-date/time fields and saved GUI presets are also implemented; see [0.5 button report](button-implementation-0.5.md). Unsupported bracket tokens are rejected. The backend also accepts `{name}` (stem), `{ext}` (original extension including dot), `{extname}` (extension without dot) and `{n}` (counter); legacy complete patterns containing `{ext}` are retained. Find/replace applies to the stem, with optional Qt regular expressions and replacement captures. Case conversion applies to the final name. Hidden filenames without an extension retain their leading dot. Transformations require lossless UTF-8 names; ordinary single-entry rename still supports selecting native byte identities.

Preview rejects invalid names, excessive lengths, duplicate targets and destinations occupied by unselected entries. Apply revalidates source signatures and conflicts. All changing sources are first renamed to unique `.tc-rename-*` siblings; targets are then published with no-replace rename, allowing cycles/swaps without truncation. Failure/cancellation attempts to move published names back to temporary names, then restore originals. Rollback never overwrites external entries, checks object identities and reports every name requiring manual recovery. This is not a filesystem transaction; crash/power-loss recovery is manual. Phase events are appended rather than rewriting the entire plan after every rename.

## Comparison and one-way synchronization

Compare selected files streams two regular files, validates identities/metadata, and reports identical bytes or the first different byte offset. It is a binary comparison report, not an editable line diff. Symlinks/special files are refused.

The Commands → Compare Dirs action marks newer or missing regular files in the current listing. The separate Synchronize Dirs dialog scans both roots recursively without following symlink directories, then compares metadata or file contents. States are equal, left-only, right-only, different or type/unsupported conflict. Link targets can be compared as text but are not synchronization actions. Scans are capped at 200,000 entries per root and 128 levels; unreadable discovered directories produce errors and prevent apply. Results are a point-in-time observation and may become stale. The GUI displays at most 10,000 filtered rows and approves only rows presented in that preview.

Copy checked left → right creates checked directories and copies/replaces checked regular files, creating required parent directories. It **retains right-only entries**, does not delete, does not merge type conflicts and does not copy symlinks. The dialog consistently maps the physical left and right panels. Reversed synchronization requires swapping the current panel directories (Ctrl+U) and opening a new comparison. Queued jobs retain their previously confirmed paths when panels are swapped. Before mutation it verifies roots and every approved source/destination snapshot; changes require a rescan. It checks each source again as work proceeds. Underlying copy has the same optimistic replacement limits as documented in `local-operations.md`. Earlier completed actions and created parents remain after failure/cancellation; no rollback across the entire sync is promised. Overlapping roots and incomplete/error/truncated plans are refused.

## Statistics and duplicate candidates

Selected folder statistics counts regular files, directories (including selected roots), links, logical bytes and allocated file bytes (`st_blocks * 512`). Links are not followed. Hard-link aliases contribute separately to statistics; allocated totals are not unique physical-volume usage. Errors and truncation mark partial counts.

Duplicate candidates scans the active tree, groups regular files by size, then streams SHA-256 only for size groups with multiple entries. Symlinks and repeated device/inode aliases are excluded. Hashing validates snapshots and reports changed/unreadable files. Equal hashes are candidates for user review; the tool never deletes files. Scan limits match directory comparison, groups are capped at 5,000 and report display at 10,000 paths. File contents can change after a completed observation.

## Search additions

Alt+F7 adds name regex, content regex, minimum/maximum byte size and inclusive modified-date/time filters. Content regex decodes UTF-8 text and uses Qt regex case options; literal matching retains byte semantics with ASCII case folding. Queries are capped at 4,096 UTF-8 bytes and file reads at 8 MiB. Regular-expression matching itself is not interrupted mid-match; cancellation is observed around file reads/matches. Invalid patterns and reversed ranges are reported.

Saved search profiles store name/content patterns, options, size/date filters and the native root path in Qt settings. Load a profile by selecting it from the editable profile combo. Saving/removing profiles does not run a search. Loading a profile can change its root; the status displays that directory. Permission-denied subdirectories can be skipped by the search iterator, so a zero-error result is not proof that inaccessible trees were searched.
