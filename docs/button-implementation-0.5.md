# Disabled-button implementation — 0.5

The pre-change 0.4 runtime inventory contains **155 disabled push buttons, tool buttons and unique command actions**. It covers the main window, all configuration pages, both button-bar editors, Multi-Rename and Find Files. Checkboxes, radio options, text inputs and combos are outside this button count. Existing working archive/queue dialogs were not expanded to inflate the denominator. A few temporary disabled controls, such as Start with an empty rename selection, remain in the baseline, making the denominator conservative.

The frozen [baseline](reference/disabled-buttons-0.4.json) and [implemented checklist](reference/implemented-buttons-0.5.json) record context, ID and caption. **62 of 155 controls (40%) now have implementations**. This includes **21 of the 53 disabled main command actions**, independently exceeding the requested 30% for that group. The GUI test verifies checklist membership, uniqueness and enabled state in a valid context; separate behavior tests exercise the implementations.

| Context | Newly implemented buttons/actions | Behavior |
| --- | ---: | --- |
| Main commands | 21 | Save/load selection; mark newer and hide same; CD tree; independent search process; system information; terminal; Desktop; programs/custom/all/selected filters; unsorted listing; direct settings editing; file lists with/without recursion; text printing; checksum creation/verification; archive testing; saved view modes |
| Configuration | 17 | File-list/main/dialog font pickers; text/background/mark/cursor/cursor text/alternating row/button colors; default column widths; save/delete/show directory history; add/delete/rename saved view modes |
| Horizontal bar editor | 7 | Import bar, delete/add, choose command, choose icon, command dropdown, export icon |
| Vertical bar editor | 7 | Same capabilities for the center bar, with independent persisted configuration |
| Multi-Rename | 7 | Name/extension/full-path range buttons, modification date and time, additional fields, saved presets |
| Find Files | 3 | View F3, Edit F4 and feed results into a new file panel |

## Actual behavior and limits

Button bars support up to 200 entries, internal registered commands, separators, drag and Alt+Left/Right/context-menu reordering, icon images and tooltips. OK persists edits; Cancel discards them. JSON and the internal-command subset of TC .BAR imports are accepted. External commands, parameters, working directories and executable icon extraction remain unavailable. Icon export uses Qt's supported ICO/PNG writers. Default bars retain reference ordering until customized.

Appearance and saved view-mode changes are drafts until Apply or OK. View modes save Brief/Full and sort column/direction. History's Save now/Delete now buttons act immediately. Colors apply to the actual palette and marking delegate; the Others picker currently controls button backgrounds. Fonts use installed Linux fonts. Column Default restores the implemented five-column widths.

Selection files use a versioned JSON format with base64 filenames to preserve native Linux bytes, reject parent traversal, and operate within the current directory. They are not claimed to be TC's selection-file format. Missing listed entries are ignored by panel selection.

File-list, selection and checksum exports publish a new file atomically and refuse to replace an existing entry, including symlinks. Choose a new destination name. File lists are bounded at 200,000 entries, 128 levels and approximately 8 million characters; recursive listing does not traverse directory links. Printing uses the Qt print dialog, including PDF output where available. File-content printing accepts lossless UTF-8 regular files up to 8 MiB.

Checksum manifests support GNU-style MD5, SHA1 and SHA256 entries for sibling files, including binary-marker syntax when verifying. Paths containing newlines/backslashes cannot be exported; verification refuses absolute and subdirectory paths. Files are streamed with cancellation and identity/change checks, and symlinks are refused. CRC32/SFV is not implemented. Archive testing reads/decompresses every entry through the existing bounded libarchive validator; unsupported encrypted formats produce errors rather than a success claim.

Programs means regular executable Linux files; directories remain navigable. Custom filters accept semicolon-separated filename globs. Only Selected snapshots the marked paths. All files clears view and quick filters. Unsorted restores filesystem enumeration order. Search-result panels retain absolute native identities for nested files; navigating away returns to normal directory browsing. The independent search process uses the application's --search option. Terminal launching tries x-terminal-emulator, GNOME Terminal, Konsole and xterm and reports failure if none can start.

Rename masks now support [N], [E], [P], [S], [C], file modification-date/time fields such as [YMD] and [hms], literal brackets, and positive/negative ranges with endpoints or lengths. Ranges count Unicode code points. Zero/space padding, inline counter overrides, plugins, special time modifiers and undo remain unavailable. The original collision and source-change checks still gate queued mutations.

Direct settings editing opens the actual settings file in the existing guarded editor. Manual changes require restart; saving live application state can supersede overlapping keys. This is not a live settings importer.

## Validation

All four CTest suites pass in Debug and AddressSanitizer/UndefinedBehaviorSanitizer builds. Qt/system leak reporting is excluded (ASAN_OPTIONS=detect_leaks=0); the optional 100,000-entry measurement remains opt-in. Regression cases cover baseline membership and the 30% threshold, toolbar OK/Cancel persistence, font/color drafts, saved view modes, executable/custom/selection filters, search-result native identities including hidden and extensionless paths, byte-preserving selection files, export no-clobber behavior, checksum tampering/traversal/symlink/cancellation handling, non-following recursive lists, and Unicode/date/range rename masks.

The local installation reports 0.5.0 and supports --search. Independent search startup was smoke-tested offscreen. Physical printer, terminal-emulator and desktop integration still require native desktop checks. Generated screenshots for this increment are under build-linux/screenshots/0.5/.
