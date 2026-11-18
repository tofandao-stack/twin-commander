# GUI reference audit — 0.4

The structural reference is Total Commander 11.58.1 x64, built-in English. The official installer was obtained from [Ghisler's download server](https://totalcommander.ch/115801/tcmd115801x64.exe). This is a static resource audit, not a claim of pixel-perfect Windows runtime equivalence.

The executable's Lazarus form data supplies menu captions, command IDs, separators, dialog controls and design coordinates. `DEFAULT.BAR`, `VERTICAL.BAR`, `TOTALCMD.INC`, keyboard documentation and bundled CHM help establish button ordering, command mappings, shortcuts and layout defaults. The external English translation menu was not used as the canonical menu because its inventory differs from the executable's built-in menu.

`tools/audit_tc_reference.py` regenerates the main reference inventory from an extracted executable and DEFAULT.BAR. `tools/audit_tc_dialogs.py` regenerates the dialog catalog. Both outputs were reproduced byte for byte against the checked-in inventories. The checked-in JSON contains functional UI metadata; executable files, original artwork and CHM content remain ignored build inputs. Hashes and evidence limits are recorded in [reference-manifest.json](reference-manifest.json).

| Area | Implemented alignment | Remaining differences |
| --- | --- | --- |
| Menu bar | Files, Mark, Commands, Net, Show, Configuration, Start, Help; reference mnemonics, nested items, separators and order; Help aligned right | About uses Twin Commander branding; dynamic Start customization unavailable |
| Top button bar | All 26 default slots, including eight separators, in reference order | Independently drawn icons; external Notepad slot opens the built-in editor |
| Center button bar | Nine reference slots, including two leading spacers and separator | Independently drawn icons |
| Function-key bar | F3 View, F4 Edit, F5 Copy, F6 Move, F7 NewFolder, F8 Delete, Alt+F4 Exit | Qt styling and font metrics |
| Panel layout | Volume selector, space indicator, root/parent controls, path strip, hotlist/history buttons, five columns, panel status | Linux mount paths and permissions; no simulated drive letters |
| Full/Brief | Name, Ext, Size, Date, Attr; wrapping brief view with shared selection; parent row excluded from operation targets | Tree, thumbnails and custom columns unavailable |
| Defaults | Vertical bar on; drive button bars off; tabs hidden when single; quick filter hidden until requested; command line and key bar visible | Existing session layout overrides are retained |
| Configuration | All 26 category pages and reference control geometry; OK, Cancel, Help, Apply order | Layout checkboxes work; other unimplemented settings are disabled |
| Button-bar dialog | Reference dimensions, fields and button positions; original slot inventory selectable | Customization disabled |
| Multi-Rename | Reference groups, name/extension masks, six preview columns and footer order; [N], [E], [C] tokens | Range, date/time and plugin tokens, advanced casing, undo unavailable |
| Find Files | General, Advanced, Plugins, Load/Save tabs; right-side search/cancel/help and bottom result buttons | Resizable Linux layout; size/date controls retain existing backend semantics; plugin and result-list feeds unavailable |
| Other dialogs | Existing working Linux dialogs retained | Copy/move, archive, attributes, viewer, compare and progress dialogs need further reference alignment |

Unavailable commands remain visible in their reference positions and disabled. Extra implemented Linux tools remain reachable through the command browser (Ctrl+Shift+P). Ctrl+L calculates occupied space, Ctrl+S opens quick filtering, Ctrl+D opens the hotlist, and Ctrl+I matches the opposite panel's directory. The directory command line also accepts registered `cm_*` action names.

The appearance uses the Qt Windows widget style, a classic gray/navy palette and 11-pixel Tahoma where installed, otherwise Liberation Sans. Original binary icon artwork is not bundled. Window decorations, font rasterization, menu metrics, DPI scaling and actual Windows runtime dialog resizing have not been compared. The declared form's design dimensions are recorded separately from observed runtime measurements, which remain unset.

Regression tests check menu trees and command IDs against the frozen inventory, toolbar slots, function-key captions, column order, configuration application, parent-row safety and shared Brief selection. Existing filesystem, queue, archive and rename tests continue to cover the backend. Generated offscreen screenshots are review artifacts, not Windows pixel baselines.

Validation on Linux, 2026-11-18: all four CTest suites pass in Debug and with AddressSanitizer/UndefinedBehaviorSanitizer. Qt/system leak reporting is excluded (ASAN_OPTIONS=detect_leaks=0); this is not a leak-free certification. The optional 100,000-entry measurement was not run. Local installation version/help checks pass for 0.4.0. Reviewed screenshots are in build-linux/screenshots/0.4/; these are generated artifacts rather than Windows reference images.

The 0.5 [button implementation report](button-implementation-0.5.md) supersedes the unavailable-feature descriptions above where it adds button-bar customization, appearance/history/view-mode settings, search-result actions and rename masks. The static reference inventory remains unchanged.
