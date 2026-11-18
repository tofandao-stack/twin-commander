# Disabled controls audit — 0.6.0

Audited on 2026-11-18 against the current native Linux build, using a fresh isolated settings profile. This is an audit of controls actually disabled in the running GUI, supplemented by source inspection of conditional controls. No application functionality was changed.

**Finding: 277 disabled configuration controls across 24 of 26 pages, 20 disabled button-bar controls (10 repeated in each editor), and two additional unavailable functions: bookmark configuration and plugin search. Total: 299 persistently disabled control instances in the audited surfaces.** This is not a count of distinct missing features or a certification that every other feature is complete.

The previous 155-entry inventory counted buttons/actions and omitted most settings inputs. Its completion did not mean the whole GUI was implemented. Several disabled settings duplicate behavior available through another enabled editor; others represent missing functionality or Windows-specific options.

## Configuration summary

| Page | Disabled controls |
|---|---:|
| Layout | 7 |
| Display | 20 |
| Icons | 22 |
| Font | 3 |
| Color | 13 |
| Tabstops/Data Formats | 19 |
| Folder-Tabs | 17 |
| Custom Columns | 1 |
| View Mode | 6 |
| Auto Switch Mode | 2 |
| Language | 1 |
| Operation | 29 |
| Edit/View | 2 |
| Copy/Delete | 16 |
| Refresh | 9 |
| Quick Search | 7 |
| FTP | 15 |
| Thumbnails | 13 |
| Log file | 14 |
| History | 13 |
| Packer | 8 |
| ZIP Packer | 16 |
| 7-ZIP Packer | 15 |
| Misc. | 9 |
| Plugins | 0 |
| Ignore list | 0 |

Every configuration entry below is visible when its page is selected and has the renderer’s “not implemented” tooltip. `src/referenceforms.cpp:62` disables reference inputs by default; `configureAppearance()` and `configureRemainingOptions()` selectively enable supported inputs. IDs are authoritative. Captions on non-button fields are inferred from nearby labels and may be imprecise; unlabeled fields retain their ID.

## Full configuration inventory

### Layout

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `idFlatDriveBar` | CheckBox | Flat icons |
| `idTwoDriveBars` | CheckBox | Show two drive button bars (fixed width, above file windows) |
| `idShowBreadcrumb` | CheckBox | Show clickable path parts ('breadcrumb bar') |
| `idAutoOpenBreadcrumb` | CheckBox | Auto-open when moving mouse over it |
| `idExecCmdLineFocused` | CheckBox | Run command line only when focused |
| `idFlatInterface` | CheckBox | Flat user interface (button bar configured separately) |
| `idXPthemeBackground` | CheckBox | Windows XP theme background (menu+all bars) |

### Display

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `IdConfshowhidden` | CheckBox | Show hidden files |
| `IdConfLower83Names` | CheckBox | Show old 8.3 filenames lowercase (like explorer) |
| `IdShowUpdir` | CheckBox | Show parent dir [..] also in root of drive |
| `IdConf_longnames` | CheckBox | Use long names (recommended) |
| `IdDirBrackets` | CheckBox | Show square brackets [ ] around directories |
| `IdBriefViewWidthLimit` | CheckBox | Limit width of brief view column to: |
| `SortMethodCombo` | ComboBox | — |
| `BriefViewWidthLimitEdit` | LineEdit | — |
| `IdConfshowSystem` | CheckBox | Show system files |
| `IdDirBackslash` | CheckBox | Append \ |
| `IdConfDirsByName` | RadioButton | Always by name |
| `IdConfDirsLikeFiles` | RadioButton | Like files (also by time) |
| `IdConf_tooltips` | CheckBox | Show help texts for buttons (tooltips) |
| `IdConf_drivetips` | CheckBox | Show volume labels/network paths for drives |
| `IdConf_NameTips` | CheckBox | Show filename in file list as tooltip if truncated |
| `IdConf_Win32Tips` | CheckBox | Win32-style tips with file comments (if available) |
| `IdConf_DescriptComments` | CheckBox | Comments from descript.ion/files.bbs |
| `IdConf_ExplorerComments` | CheckBox | Comments from Explorer (OLE2) |
| `IdConfOldStyleTree` | CheckBox | Always load complete directory tree |
| `IdTreeNoAutoChange` | CheckBox | Tree: Press ENTER to open dir in other window |

### Icons

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `idiconsize` | ComboBox | — |
| `idIconExtraX` | ComboBox | Extra space: Horizontal: |
| `idIconExtraY` | ComboBox | Vertical: |
| `IdConfShowAllIcons` | RadioButton | All associated |
| `IdConfshowStandardIcons` | RadioButton | Only standard icons |
| `IdConfShowNoIcons` | RadioButton | No icons |
| `IdConfAllAndEXE` | RadioButton | All associated + EXE/LNK (slow) |
| `NotOnFloppy` | CheckBox | EXE/LNK not on floppy |
| `NotOnNet` | CheckBox | EXE/LNK not on net |
| `IdShowSpecialFoldersLocal` | CheckBox | Show icons for special folders: Normal file system |
| `IdShowSpecialFoldersRemote` | CheckBox | Show icons for special folders: Virtual folders (e.g. Network) |
| `IdShowOverlayIcons` | CheckBox | Show overlay icons, e.g. for links |
| `IdShowDefIconsFromExplorer` | CheckBox | Show default folder/file icons from Explorer |
| `IdHidSysExclamation` | CheckBox | Show '!' on hidden/system |
| `IdDimmedHidden` | CheckBox | Dimmed hidden files |
| `IdIconsForMenu` | CheckBox | Show icons for commands in the main menu |
| `IdMenuIconSize` | ComboBox | — |
| `Id3dFrame` | CheckBox | Show 3d frame around icon |
| `IdPressedButtons` | CheckBox | Show checked items as pressed buttons instead of checkmarks |
| `IdDriveIconsFromExplorer` | CheckBox | Show drive icons from Explorer |
| `idDriveExtraX` | ComboBox | Extra drive space: |
| `idDriveExtraY` | ComboBox | Extra drive space: |

### Font

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `DpiCheck` | CheckBox | Set dots per inch (DPI) to: |
| `DpiEdit` | ComboBox | — |
| `AutoSizeCheck` | CheckBox | Automatically adjust dialog boxes to larger fonts |

### Color

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `id_DarkModeCombo` | ComboBox | Dark mode: |
| `id_ForeColor` | ComboBox | — |
| `id_BackColor` | ComboBox | Font color: |
| `id_MarkColor` | ComboBox | Background 2: |
| `id_CursorColor` | ComboBox | Mark color: |
| `id_CursorFontColor` | ComboBox | Cursor color: |
| `id_BackColor2` | ComboBox | Background: |
| `id_CursorInverted` | CheckBox | Use inverted cursor |
| `id_ColorTypesEnabled` | CheckBox | — |
| `id_MarkInverted` | CheckBox | Use inverted selection |
| `id_OtherColor` | ComboBox | Others: |
| `id_OtherName` | ComboBox | Others: |
| `id_CursorThemed` | CheckBox | Use Windows theme for cursor |

### Tabstops/Data Formats

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `id_confExtAfterName` | RadioButton | directly after filename |
| `id_confExtAligned` | RadioButton | aligned (with Tab) |
| `id_tabSize` | LineEdit | Size: |
| `id_TabDate` | LineEdit | Date/Time: |
| `id_TabAttr` | LineEdit | Attributes: |
| `id_tabExt` | LineEdit | Extension: |
| `id_TabComments` | LineEdit | Comments (fixed): |
| `id_ShowCentury` | CheckBox | Show century in date (20xx) |
| `id_UseAMPMFormat` | CheckBox | Use 12 hour AM/PM format |
| `id_AdjustWidth` | CheckBox | Adjust Tabs to window width |
| `id_sizeDisplay` | ComboBox | — |
| `id_size_footer` | ComboBox | Size display: |
| `id_dircountinfooter` | CheckBox | Show directory count in footer |
| `id_confDateFormat` | ComboBox | Date format: |
| `id_confTimeFormat` | ComboBox | — |
| `id_sepDate` | ComboBox | Date: |
| `id_sepTime` | ComboBox | Time: |
| `id_sepThousands` | ComboBox | Thousands: |
| `id_sepDecimal` | ComboBox | Decimal: |

### Folder-Tabs

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `ChkTabsMultiLines` | CheckBox | Tabs on multiple lines |
| `ChkTabsSameWidth` | CheckBox | All tabs have same width |
| `ChkTabsLimitOption` | CheckBox | Limit tab title length to: |
| `EdtTabsLimitLength` | LineEdit | — |
| `ChkTabsConfirmClose` | CheckBox | Confirm close all tabs |
| `ChkTabsCloseDoubleClick` | CheckBox | Close tab on double click |
| `ChkTabsOpenForeground` | CheckBox | Ctrl+Up opens new tab in foreground |
| `ChkTabsOpenNearCurrent` | CheckBox | Open new tabs near current tab |
| `ChkTabsAlwaysVisible` | CheckBox | Show tab header also when there is only one tab |
| `ChkTabsLockedAsterisk` | CheckBox | Show locked tabs with an asterisk * |
| `ChkTabsActivateTarget` | CheckBox | Activate target panel when clicking on one of its Tabs |
| `ChkTabsDriveInTitle` | CheckBox | Always show drive letter in tab title |
| `ChkTabsIcons` | CheckBox | Show icons on tabs |
| `ChkTabsRevertToRoot` | CheckBox | Return to locked tab root directory on tab change |
| `ChkTabsSaveFiltersSeparately` | CheckBox | Save displayed file types separately for each tab |
| `ChkTabsOpenList` | CheckBox | Ctrl+Tab opens as list |
| `ChkTabsLastVisitedFirst` | CheckBox | Ctrl+Tab switches to last visited |

### Custom Columns

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `FileSystemCombo` | ComboBox | — |

### View Mode

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `AdditionalSortEdit` | LineEdit | Sort order: |
| `id_TabColorBg` | ComboBox | Tab color and icon: |
| `id_TabColorBg2` | ComboBox | Background: |
| `AutoRunEdit` | LineEdit | Background 2: |
| `id_TabColor` | ComboBox | Additional sort orders: |
| `PriorityCheckbox` | CheckBox | Priority |

### Auto Switch Mode

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `ModeChooseCombo` | ComboBox | — |
| `AutoSwitchModeCheck` | CheckBox | Automatically switch view mode on directory change: |

### Language

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `W_langlist` | ListWidget | Change the language/ Sprache wechseln/ Changer de langue |

### Operation

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `IdConfrightbutton` | RadioButton | Use right mouse button (as in NC) - context menu after 1 second |
| `IdConfleftbutton` | RadioButton | Left mouse button (Windows standard) |
| `IdClickOnIcon` | CheckBox | By clicking on icon |
| `IdOnlyOnTouchscreen` | CheckBox | Only on touchscreen |
| `IdRubberband` | CheckBox | Rubber band selection |
| `IdConfFilesOnly` | RadioButton | Select files only |
| `IdConfAlsoDirs` | RadioButton | Also select directories |
| `IdConfsavedirs` | CheckBox | Directories |
| `IdConfsavepanels` | CheckBox | Panels (brief/full, etc.) |
| `IdConfCommandLines` | CheckBox | Old command lines |
| `IdConf_Countfiles` | CheckBox | when selecting with the space bar |
| `IdConf_CountfilesCopy` | CheckBox | before copying/deleting |
| `IdConf_EverythingForSize` | CheckBox | 'Everything' |
| `IdConf_GetDirSize` | ComboBox | — |
| `Idconf_1hourdif` | CheckBox | Ignore 1 hour time delay |
| `IdConf_comments_bothformats` | CheckBox | Read from both |
| `IdConf_comments_copy` | CheckBox | Copy comments with files |
| `IdConf_comments_doscharset` | CheckBox | DOS charset |
| `IdPreferredCommentFormat` | ComboBox | — |
| `IdConf_oneinstanceonly` | CheckBox | Allow only 1 copy of Total Commander at a time |
| `Id_ConfMoveToTray` | CheckBox | Move icon to system tray when minimized |
| `Id_ConfAlwaysToRoot` | CheckBox | Always go to the root of a drive when changing drives |
| `Id_ConfselOnlyName` | CheckBox | Select only the file name when renaming (not the extension) |
| `Id_ConfSingleClickStart` | CheckBox | Open files and directories with a single click |
| `id_ConfAutoComplete` | CheckBox | Auto-complete paths |
| `id_ConfAutoSuggest` | CheckBox | Auto-append suggested name |
| `id_checkforupdates` | CheckBox | Check for updates |
| `id_scrollahead` | CheckBox | Extra lines below cursor: |
| `id_extralines` | LineEdit | — |

### Edit/View

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `ViewerHotkeyCombo` | ComboBox | Internal Viewer with file under cursor |
| `IdAutoReUpload` | CheckBox | Plugins/FTP: Automatically re-upload changed files |

### Copy/Delete

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `EdSmallSame` | LineEdit | Source+Target on same disk: |
| `EdSmallOther` | LineEdit | Source+Target on different disks: |
| `EdBigSame` | LineEdit | Source+Target on same disk: |
| `EdBigOther` | LineEdit | Source+Target on different disks: |
| `ChkCompatibility` | CheckBox | Use compatibility mode for the following drives: |
| `EdCompatLetters` | LineEdit | — |
| `RdDefaultCopy` | RadioButton | Use default method only (reliable, not very fast) |
| `RdBigfileCopy` | RadioButton | Also use big file copy mode (not using cache) |
| `RdCopyPaste` | RadioButton | Use copy+paste via Explorer (only in case of copy problems) |
| `EdPhysDisks` | LineEdit | The following drive letters are on the same physical harddisk (example: CDE,FGH ): |
| `ChkUsbSticks` | CheckBox | USB sticks |
| `ChkCopyStamp` | CheckBox | Copy date/time of directories |
| `ChkExplorerDelete` | CheckBox | Use Explorer delete method (faster, cannot handle comments) |
| `IdConf_trashcan` | CheckBox | F8/Del deletes to recycle bin (Shift=directly) |
| `ChkVerifyAfterCopy` | CheckBox | Verify after copying (MD5 checksum) |
| `id_StandardCopyMethod` | CheckBox | Use standard copy method (recommended) |

### Refresh

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `NoRereadEdit` | LineEdit | No refresh for these drives: |
| `NoReread2Edit` | LineEdit | Do not remember these drives on exit: |
| `WatchDirsDetect` | CheckBox | Refresh when files are created, deleted and renamed |
| `WatchDirsSizeDateAttr` | CheckBox | Also when size, date, or attributes change |
| `WatchDirsAtEnd` | CheckBox | Put added files at the END of the file list (default: sorted) |
| `WatchDirsNoBackground` | CheckBox | Don't react to updates while in the background |
| `WatchDirsFooter` | CheckBox | Update footer (total number of files) |
| `WatchDirsHeader` | CheckBox | Update header (free disk space) |
| `WatchDirsExcludeEdit` | LineEdit | No refresh for these drives: |

### Quick Search

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `IdConfAltForMenu` | RadioButton | Ctrl+Alt+Letters |
| `IdConfAltForQuickSearch` | RadioButton | Alt+Letters |
| `IdConfLetterQuickSearch` | RadioButton | Letter only |
| `IdConfLetterDlgQuickSearch` | RadioButton | Letters - with search dialog |
| `IdConfDisabledQuickSearch` | RadioButton | Disabled |
| `IdConfExactBeginning` | CheckBox | Beginning (name must start with first typed character) |
| `IdConfExactEnding` | CheckBox | Ending (last character before a typed dot . must match) |

### FTP

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `FTP_Retry` | LineEdit | — |
| `FtpTransferModeBox` | ComboBox | Delay between retries (seconds) |
| `FTP_retry_delay` | LineEdit | — |
| `FtpOpenInBox` | ComboBox | Default transfer mode: |
| `FtpBackGnd` | CheckBox | All uploads/downloads in background |
| `FtpPassiveMode` | CheckBox | Use passive mode by default |
| `FtpModeZ` | CheckBox | Compress during transfer (MODE Z) |
| `FtpModeZnotonLAN` | CheckBox | Not on LAN |
| `FtpPORTlimitrange` | CheckBox | PORT mode: only use ports from limited range: |
| `FtpPortFrom` | LineEdit | — |
| `FtpPortTo` | LineEdit | - |
| `FtpAutoResumeOption` | CheckBox | Auto-resume transfer if no data received for: |
| `FtpAutoResumeValue` | LineEdit | — |
| `FtpPortIPbox` | CheckBox | PORT mode: external address (IP/DNS/HTTP): |
| `FtpPortIPedit` | LineEdit | — |

### Thumbnails

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `ThumbsCopyDeleteChk` | CheckBox | Copy/delete thumbnails with files |
| `ThumbsCustomFieldsChk` | CheckBox | Show custom fields below thumbnails (e.g. image size): |
| `RemoveThumbsForCDs` | CheckBox | Remove thumbnails for currently ejected CDs |
| `ThumbsFromExplorerChk` | CheckBox | Use Explorer method (OLE2) for: |
| `ThumbsFromListerPluginsChk` | CheckBox | Get from Lister plugins for: |
| `ThumbsFromTextChk` | CheckBox | Plain Text file thumbnails for: |
| `ThumbsFromExplorerEdt` | LineEdit | — |
| `ThumbsFromListerPluginsEdt` | LineEdit | — |
| `ThumbsFromTextEdt` | LineEdit | — |
| `ThumbsFromIrfanXnChk` | CheckBox | Get from Irfan-/Xnview for: |
| `ThumbsFromIrfanXnEdt` | LineEdit | — |
| `ThumbsFromInternalChk` | CheckBox | Internal thumbnails from images: |
| `ThumbsFromInternalEdt` | LineEdit | — |

### Log file

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `LogCpMvShChk` | CheckBox | Copy/Move/Create shortcut |
| `LogDeleteChk` | CheckBox | Delete |
| `LogFoldersChk` | CheckBox | Create/Delete folders |
| `LogArchiveChk` | CheckBox | Pack/Unpack/Test archives |
| `LogFsChk` | CheckBox | File system plugins |
| `LogSuccess` | CheckBox | Log successful operations |
| `LogErrors` | CheckBox | Log errors |
| `LogSkipped` | CheckBox | Log skipped files (when choosing skip or skip all) |
| `IdLogNever` | RadioButton | Never (always log to same log file) |
| `IdLogEveryDay` | RadioButton | Every day after midnight |
| `IdLogWhenLarger` | RadioButton | When the log file becomes larger than |
| `IdLogSizeLimit` | LineEdit | — |
| `IdKeepMaximum` | CheckBox | Keep a maximum of |
| `IdLogFileCount` | LineEdit | — |

### History

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `id_historythinning2` | CheckBox | Directory history thinning |
| `ShowHistoryIconsChk` | CheckBox | Icons |
| `HistoryFontCombo` | ComboBox | Font: |
| `SaveHistoryOnExitChk` | CheckBox | Save history on exit |
| `HistoryMaxSaveEdit` | LineEdit | Maximum items to save: |
| `HistoryMaxShowEdit` | LineEdit | Maximum items to show: |
| `ShowLastVisitedInMiddleChk` | CheckBox | Show last visited directory closest to the middle of the list |
| `FrequentlyUsedMaxSaveEdit` | LineEdit | Maximum items to save: |
| `FrequentlyUsedMaxShowEdit` | LineEdit | Maximum items to show: |
| `DirUsageLocationCombo` | ComboBox | Storage location for list of most frequently used folders: |
| `ShowFreqentlyUsedMaxInMiddleChk` | CheckBox | Show most frequently used directory closest to the middle of the list |
| `ScrollListToCombo` | ComboBox | — |
| `MaxWidthEdit` | LineEdit | Max. width in characters (average): |

### Packer

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `zip_likedirs` | CheckBox | Treat archives like directories |
| `arj_useinternal` | CheckBox | Use internal un-ARJ if possible |
| `arj_longnames` | CheckBox | Pass long filenames to ARJ (ARJ 2.55 or later required) |
| `lzh_useinternal` | CheckBox | Use internal un-LZH if possible |
| `rar_useinternal` | CheckBox | Use internal un-RAR if possible |
| `ace_useinternal` | CheckBox | Use internal un-ACE if possible |
| `tar_linuxcompatible` | CheckBox | Create TAR archives for Linux (unchecked: for SunOS) |
| `zip_setdate` | CheckBox | Set archive date to newest file date |

### ZIP Packer

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `zip_internalpack` | CheckBox | Use internal ZIP packer |
| `zip_zipnt` | CheckBox | Use ZIP-NT 2.01 |
| `zip_useinternal` | CheckBox | Always use internal unzipping (if possible) |
| `zip_use7z` | CheckBox | 7Z |
| `internalzip_default` | RadioButton | normal compression (6) |
| `internalzip_max` | RadioButton | maximum compression (9) |
| `internalzip_min` | RadioButton | fastest compression (1) |
| `internalzip_other` | RadioButton | other: |
| `internalzip_value` | LineEdit | — |
| `internalzip_settemp` | RadioButton | default (run 'cd %TEMP%' in command line to see directory) |
| `internalzip_otherdir` | RadioButton | other: |
| `internalzip_tempdir` | LineEdit | — |
| `zip_83name` | CheckBox | Use 8.3 short filenames |
| `zip_openpartial` | CheckBox | Open partial ZIP files |
| `zip_keepdamaged` | CheckBox | Keep damaged files (bad CRC) |
| `internalzip_unicodemethod` | ComboBox | Pack Unicode names: |

### 7-ZIP Packer

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `z7_internalpack` | CheckBox | Use internal 7-Zip packer |
| `z7_preferplugin` | CheckBox | Prefer 7-Zip plugin if installed |
| `z7_extraparameters` | LineEdit | Extra parameters: |
| `z7_default` | RadioButton | normal compression (6) |
| `z7_max` | RadioButton | maximum compression (9) |
| `z7_min` | RadioButton | fastest compression (1) |
| `z7_other` | RadioButton | other: |
| `z7_value` | LineEdit | — |
| `z7_method` | ComboBox | Compression method: |
| `z7_dict` | ComboBox | Dictionary size: |
| `z7_word` | ComboBox | Word size: |
| `z7_solidblock` | ComboBox | Solid block size: |
| `z7_solid` | CheckBox | Create solid archive |
| `z7_threads` | ComboBox | Number of CPU threads: |
| `z7_encryptnames` | CheckBox | Also encrypt names |

### Misc.

| Control ID | Type | Caption / nearby label |
|---|---|---|
| `confirm_overwrite` | CheckBox | Overwriting files |
| `confirm_readonly` | CheckBox | Overwriting/Deleting read-only files |
| `confirm_hidsys` | CheckBox | Overwriting/Deleting hidden/system files |
| `confirm_mouse` | CheckBox | Drag&Drop (copying files with mouse) |
| `confirm_dirs` | CheckBox | Deleting non-empty directories |
| `confirm_mouse_delete` | CheckBox | Drag&Drop to the 'F8 Delete' button |
| `Hot_Params_Edit` | LineEdit | Parameters: |
| `SND_onlylong` | CheckBox | Only play for long operations: |
| `SND_Delay` | LineEdit | > |

## Button-bar editors

These ten controls are disabled in **both** the horizontal and vertical bar editors (`src/customization.cpp:125`). Other editor buttons are enabled.

| Control ID | Unavailable setting |
|---|---|
| `id_btn_iconindex` | Icon index |
| `W_btn_param` | Command parameters |
| `id_btn_assymbol` | Run minimized |
| `W_btn_startpath` | Start directory |
| `id_btn_maximized` | Run maximized |
| `id_btn_barsize` | Button size |
| `id_FlatIcons` | Flat icons |
| `id_SmallIcons` | Small icons |
| `id_XPstyle` | XP style |
| `id_smalliconsize` | Small icon size |

External program commands and parameters in button bars are not supported by the current editor; it supports internal commands. Importing a file does not add an external-program execution implementation.

## Other unavailable functions

| Location | Disabled function | Evidence |
|---|---|---|
| Directory hotlist / bookmarks | Configure… editor | `src/mainwindow.cpp:691` explicitly disables the action. Adding/removing the current directory is implemented. |
| Find files → Plugins | Search in plugins | `src/toolwindows.cpp:249` disables the checkbox. Native content providers elsewhere do not implement search predicates. |

## Disabled because of current state

These are implemented controls that can be disabled normally; they should not be counted as missing implementations.

| Control | Enabling condition |
|---|---|
| FTP Disconnect; FTP Show Hidden Files | Active FTP connection (`src/networkcommands.cpp`) |
| Unshare Directory | An active native HTTP share (`src/networkcommands.cpp`) |
| Multi-Rename Start | Valid preview with actual changes, no mutation running (`src/advancedwindows.cpp`) |
| Multi-Rename Next / Undo | Completed rename; Undo also requires a retained inverse plan |
| Search Start | No search currently running (`src/toolwindows.cpp`) |
| Search View / Edit | A current result; Feed to listbox requires results |
| Comparison Scan / Compare contents / Apply | Worker idle; Apply additionally requires a valid comparison plan |
| Archive extract all / selected | Archive listing complete |
| Report Save / Print | Report generation complete |
| Stop / cancel controls | Operation still running |
| Viewer mode selector | Viewing rather than editing; also restricted while loading |
| Toolbar proxy action | Mirrors the underlying action; unknown imported commands are disabled |
| Tab-strip overflow arrows | Qt enables them when tabs overflow |

The startup QAction scan also contains an unnamed `&Help` action. This is an automatically disabled empty menu shell, not the functional right-aligned Help button or the implemented `cm_HelpIndex` action. It is excluded from the missing-function count.

## Important interpretation

- Some disabled controls have working alternatives: color-picker buttons versus disabled color combos; view-mode secondary-sort/autorun/color buttons versus disabled direct fields; native thumbnails versus disabled provider toggles. The disabled direct controls still need implementation, synchronization, or removal.
- A disabled setting does not imply its entire underlying behavior is absent. Hidden-file toggling, tabs, file copying, logging and FTP transfers already have implementations, but many settings for those features do not.
- Windows Explorer/OLE integration, Windows theme options, drive-letter settings and Windows DLL plugin compatibility need explicit Linux product decisions. Enabling a checkbox alone would not implement these contracts.
- This audit covers all rendered configuration pages, both bar editors, startup actions, and source disable/enable sites. It does not prove every enabled control fully implements its advertised behavior, or exhaust every possible dynamically created extension dialog.

## Reproduce the runtime inventory

Build `commander_ui` first, then run from the repository root (the archive runtime path below matches this environment):

```sh
c++ -std=c++20 -fPIC -Isrc tools/audit_disabled_controls.cpp \
  build-linux/libcommander_ui.a \
  $(pkg-config --cflags --libs Qt6Widgets Qt6Concurrent Qt6PrintSupport Qt6Network) \
  /usr/lib/x86_64-linux-gnu/libarchive.so.13 \
  -o build-linux/audit-disabled-controls
QT_QPA_PLATFORM=offscreen build-linux/audit-disabled-controls \
  docs/reference/disabled-controls-current.json
```

The audit isolates settings in a temporary directory and does not modify normal user preferences. The JSON records runtime widget types, IDs, visibility and tooltip data.
