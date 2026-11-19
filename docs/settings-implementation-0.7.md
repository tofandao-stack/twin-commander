# Settings implementation — 0.7.0

This increment uses the 299 persistent disabled control instances identified in [the 0.6 audit](disabled-controls-audit.md) as its denominator. It implements 92 previously disabled configuration inputs and the bookmark editor: 93 / 299 = 31.1%. This counts UI controls, including related radio choices, rather than distinct product features. The original audit JSON remains frozen for comparison.

## Measured reduction

| Area | Previously disabled controls implemented |
|---|---:|
| Display | 11 |
| Icons | 11 |
| Color | 9 |
| Tabstops/Data Formats | 19 |
| Folder-Tabs | 11 |
| Auto Switch Mode | 1 |
| Operation | 5 |
| Refresh | 6 |
| Quick Search | 7 |
| History | 12 |
| Directory hotlist | 1 |
| **Total** | **93** |

Remaining persistent controls: **206** (185 configuration inputs, 20 button-bar fields and plugin search). [Before/after implementation inventory](reference/implemented-controls-0.7.json) · [Remaining runtime inventory](reference/disabled-controls-0.7.json).

## Implemented behavior

- Display: default hidden-file visibility, optional parent row at filesystem root, directory brackets and trailing slash, brief-column width limit, button and file-name tooltips, directory sorting by name versus the selected file sort, and case-sensitive, case-insensitive or numeric natural name sorting. Root's synthetic parent cannot be selected as an operation source.
- Icons: associated desktop icons, native standard icons or no icons; size and horizontal/vertical spacing; link overlays; hidden-entry dimming; icon frames; menu icons and menu icon size. Thumbnails retain their image previews independently of normal file-icon mode. Associated icons use Qt's Linux file-icon provider, not Explorer.
- Formats: extensions inline or in their own column; configured extension/size/date/attribute/comment widths; stretched Name column; byte/KiB/MiB/automatic binary size units and separate footer units; footer directory counts; year length, date order, AM/PM, seconds, date/time/group/decimal separators. Input ranges and closed separator choices prevent invalid formatting strings.
- Colors: direct foreground/background/mark/cursor/cursor-text/alternate color selectors, file-type coloring switch, inverted cursor and inverted marked selection. The existing picker buttons remain available.
- Tabs: equal widths, title-length limit, double-click close (last tab protected), background/foreground opening, insertion next to the current tab, always-visible tab header, optional target-panel activation, tab icons, Ctrl+Tab list or most-recent tab. Ctrl+Up opens the current directory entry in a new tab. Tab history uses identities rather than pointers to deleted widgets.
- Session: independently enable directory/tab restoration, panel display-mode restoration and command-line history persistence. Disabling a category clears its saved data at the next save; explicit command-line starting paths still take precedence.
- Selection: select files only or files and directories for mask, Select All and inversion. Explicitly marking a directory with Space/Insert remains possible.
- Quick search: Ctrl+Alt, Alt, plain typing, filter-dialog typing or disabled; beginning and pre-extension ending constraints for incremental selection. Ctrl+S remains the explicit quick filter. Quick search selects a cursor without changing marked selections.
- Automatic refresh: directory notifications for added/deleted/renamed entries; bounded rotating metadata checks (up to 64 entries per second) for size/time/permissions; optional append-new-entries ordering, foreground-only updates, and independent footer/header updates during automatic reloads. Native path identity and surviving selections are retained. Manual refresh remains available. Large-directory metadata detection is intentionally incremental, not an instantaneous full scan.
- History: menu thinning of consecutive ancestors (does not alter Back/Forward history), icons/font, save on exit, saved/displayed limits for history and frequent directories, recent/popular entries positioned midway in their sections, menu width and initial menu position. Frequent-directory save limits apply to both Save Now and save-on-exit. Paths remain native byte strings; abbreviated menu labels are presentation only.
- Auto Switch Mode: the page's master switch now controls whether automatic rules run.
- Directory hotlist: Configure opens a real editor with directory picking, duplicate prevention, removal, ordering, OK/Cancel and persistent native-path entries. Existing undecodable Linux paths retain their original bytes.

## Scope and remaining gaps

The other audited configuration settings, button-bar fields and plugin search are not counted as implemented. In particular, this increment does not implement Windows OLE/Explorer settings, Windows plugin DLLs, TC's proprietary PORT protocol, richer copy/recovery policies, the disabled archive compression options or language-pack loading. Some enabled options are Linux adaptations, as described above.

The settings dialog stages these preferences until Apply/OK. Cancel discards staged changes; immediate operation buttons from earlier releases retain their existing behavior. Numeric fields reject out-of-range edits and preserve the last valid value. Existing color-picker buttons and direct color combos share the same saved settings.

## Validation

The new preferences suite exercises the audited-control reduction, Apply/Cancel/reopen, formatted model values, numeric sorting, incremental search, selection policy, automatic refresh with retained selection, background tabs, session switches, history limits and bookmark editing. The runtime audit confirms 92 fewer disabled configuration controls (277 → 185), plus the implemented bookmark editor. All seven CTest suites pass against the final source in both Debug (27.71 s) and ASan/UBSan (37.82 s) builds. The icon regression check also verifies that changing command availability does not remove vertical toolbar icons when menu icons are hidden. Custom view-mode tab icons survive preference updates and are restored when tab icons are re-enabled. Leak reporting is disabled for sanitizer runs, and the optional 100,000-entry measurement is skipped. Installation to a workspace-local prefix and installed `--help`/`--version` smoke checks passed for 0.7.0. Desktop compositor behavior and full TC parity remain unverified.
