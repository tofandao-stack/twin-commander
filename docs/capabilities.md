# Capability inventory — internal alpha 0.6

These are concrete locally implemented capabilities. This list is not the exhaustive final TC inventory, and its row count cannot establish a percentage of final parity. `docs/parity-matrix.csv` retains scenario-level reference status; static GUI structure has been audited against 11.58.1 resources; installed runtime behavior remains unverified.

| Area | Available capabilities |
| --- | --- |
| Main window | Two independent panels; resizable splitter; active path highlighting; dense sortable columns; project-owned icons and link indication; shared menus/toolbar/function-key actions; right-click actions |
| Navigation | Editable paths; parent; previous/next directory; home; root; directory Enter/double-click; desktop file opening; opposite-panel matching; swap current panel directories; tabs; per-tab history |
| Panel state | Hidden files; bounded worker enumeration; loading counts; stale request cancellation; retained listing on failure; refresh selection/cursor restoration; native entry identity |
| Customization | Persisted horizontal/vertical internal-command bars with icons/tooltips, import and drag reorder; font/color drafts; saved Brief/Full and sort view modes |
| File exports | Native-byte selection save/load; recursive file lists and printing; GNU MD5/SHA1/SHA256 manifest creation/verification; atomic new-file publication |
| Selection | Insert/Space marking; multiselect; select all; clear; invert; wildcard select/deselect; saved/restored selection; parent excluded from operation targets; current-item fallback for commands; selected byte totals; copy paths to clipboard |
| Filtering | Debounced case-insensitive name substring filter per panel; clear/Escape; Ctrl+S focus; restore last filter |
| Reference GUI | Eight menus; 26 top toolbar slots; nine vertical slots; seven function buttons; Full/Brief views; 26 configuration pages with working layout options; unavailable commands disabled; see [GUI audit](gui-audit.md) |
| Sessions | Tab directories including raw Linux bytes; current tabs; splitter; geometry; persistent native-path bookmarks |
| Local copy | Regular files; recursive directories; link preservation; fail/skip/explicit regular-file replace; staged publication; progress/cancel; per-item errors; JSON intent/outcome journals |
| Local changes | Same-device move; rename; mkdir; exclusive new-file creation; desktop trash; separately confirmed recursive permanent deletion; nonrecursive ordinary permission editing; symbolic-link creation |
| Operations | Serialized background queue; pause/resume at checkpoints; queued/running cancellation; reorder waiting jobs; per-job details; clear finished entries; persisted queue summaries and previous-report inspection |
| Network | Dedicated FTP/FTPS browser using curl; certificate verification; listing/upload/new-file download; URL lists; tokenized read-only HTTP sharing; desktop SMB/SFTP delegation and executable filesystem providers |
| Additional tools | SHA-validated split/combine; MIME/UUE/XXE codecs; validated BinHex data-fork decoding; Linux xattr comments; tree/separate trees, thumbnails and docked quick view |
| Archives | ZIP/TAR/TAR+gzip creation; libarchive-supported archive browsing; path filter; all/selected subtree extraction into a new folder; no-clobber commit; traversal/link/special-file rejection; expanded-size/count/depth limits |
| Batch rename | Counter patterns; stem find/replace or regex; extension retention; upper/lower case; live preview; collision and stale-source validation; rename cycles through temporary names; rollback attempts and phase journals |
| Comparison / sync | Streaming binary file comparison with first differing offset; recursive metadata/content directory comparison; checked-row preview; one-way additive/update synchronization; stale-plan rejection; right-only entries retained |
| Viewer | UTF-8 text; text find with wrap; hex preview; supported raster-image preview; bounded file size |
| Editor | UTF-8 editing; widget undo/redo; find; atomic staged save; external-change refusal; native paths; close/unsaved guards; CRLF preservation |
| Search | Filename glob or regex; literal UTF-8 or regex content search; byte-size and modified-date filters; saved profiles including native root paths; recursive/current-directory mode; hidden/case toggles; cancellation; result cap; reveal in originating panel; result viewer/editor and feed to a new panel; independent process |
| Inspection | Native lstat properties; octal permissions and numeric owner/group; symlink target; volume free space; background SHA-256 checksums with cancellation and change detection; recursive folder statistics; size/SHA-256 duplicate candidates with hard-linked aliases excluded |

## Deliberately unfinished

Cross-filesystem moves, resumable jobs, automatic crash recovery, recursive metadata/ACL preservation, ownership editing, general archive modification/encryption/nesting, bidirectional or deleting synchronization, editable text diffs, full virtual remote-panel semantics, Windows plugin binary ABI compatibility and exhaustive advanced view modes, thumbnails, full advanced-search parity, configuration import, complete shortcut compatibility and installed Windows reference screenshots remain pending. Progressive first-row display and performance of large committed widgets/interactive header sorting also remain open.

## Measuring progress

Track executable scenarios, implementation coverage and reference verification separately. A defensible percentage of the *final* functionality requires finishing the feature inventory and agreeing its weighting; counting a handful of broad groups or adding easy rows would overstate coverage. The current release is a substantial local-manager/tooling increment. Full-product percentage coverage and parity certification remain unestablished. See `advanced-tools.md` for the new contracts and limits.
