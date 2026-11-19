# Command and application icons

The built-in icon system uses original Qt-painted artwork in a compact, colored file-manager style. Command IDs such as `tc_501` resolve through the reference command catalog, so numeric menu commands receive the same semantic glyph as their named command. Icons cover file operations, archives, navigation, selection, search, comparisons, networking, views, sorting, checksums, settings, help and other native commands. Related commands share a glyph family; unclassified native commands use a settings gear.

Menu commands, submenus, both toolbars, the F3–F8 function bar, configuration categories and dialog action buttons receive icons. Top-level menu headers retain readable text labels. Checkboxes and radio buttons retain their native state indicators. The configuration category list has extra space for its icons and labels. Dynamic/context menus and newly opened dialogs are decorated when shown. Explicit custom icons are preserved. The existing custom PNG directory and menu-icon visibility/size preferences remain supported; hiding menu icons no longer removes shared command icons from toolbars.

Command artwork is rasterized at multiple sizes (16–128 pixels) for Qt's icon selection at different display scales. The application has its own dual-panel icon with a gold transfer arrow, embedded at 16, 32, 48, 64, 128, 256 and 512 pixels. It is used for windows and the system tray, and installed into the standard hicolor app-icon directories. The desktop entry uses `Icon=twin-commander`, and the application declares the corresponding desktop file identity. Desktop shells may refresh their icon caches asynchronously after installation.

The artwork is independently drawn for Twin Commander; these are not extracted Total Commander image resources or a claim of pixel-for-pixel artwork parity.

To regenerate the checked-in application PNGs using Qt Gui:

```sh
c++ -std=c++20 -fPIC tools/render_app_icon.cpp $(pkg-config --cflags --libs Qt6Gui) -o build/render-app-icon
QT_QPA_PLATFORM=offscreen build/render-app-icon resources/icons
```

All nine CTest suites pass in Debug (65.22 s) and ASan/UBSan (89.21 s; leak reporting disabled). The optional large-directory measurement was skipped. Icon coverage checks traverse the active menu hierarchy, verify numeric command mapping and distinct operation glyphs, inspect all configuration categories and dialog action buttons, exercise custom PNG overrides/menu visibility, and decode all seven application PNG sizes. Offscreen screenshots were inspected for menu-label visibility and configuration-category spacing. Workspace-prefix installation verifies all seven hicolor PNGs byte-for-byte and the desktop entry icon name; installed version/help checks and `git diff --check` pass. Desktop-shell cache refresh and compositor rendering were not exercised by the offscreen checks.
