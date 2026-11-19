#include "ui/icons/commandicons.h"
#include "filesystem/pathencoding.h"
#include "ui/panels/filepanellayout.h"
#include "settings/applicationpreferences.h"
#include "settings/interfacepreferences.h"
#include "app/mainwindow.h"
#include "ui/settings/referenceformrenderer.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <unordered_map>

namespace {
QKeySequence referenceShortcut(const QString &text) {
    if (text.contains("Num")) {
        int key = text.contains('+')   ? Qt::Key_Plus
                  : text.contains('-') ? Qt::Key_Minus
                  : text.contains('*') ? Qt::Key_Asterisk
                                       : Qt::Key_Slash;
        int modifiers = Qt::KeypadModifier;
        if (text.contains("Ctrl"))
            modifiers |= Qt::ControlModifier;
        if (text.contains("Alt"))
            modifiers |= Qt::AltModifier;
        return QKeySequence(modifiers | key);
    }
    return QKeySequence(QString(text).replace("Left Arrow", "Left").replace("Enter", "Return"));
}
QString captionOnly(const QString &caption) { return caption.section('\t', 0, 0); }
} // namespace
void MainWindow::buildMainWindowUi() {
    const auto reference = commanderUiReference();
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(2, 0, 2, 2);
    layout->setSpacing(0);
    left_->setObjectName("leftTabs");
    right_->setObjectName("rightTabs");
    leftHost_ = new FilePanelHost(left_, splitter_);
    rightHost_ = new FilePanelHost(right_, splitter_);
    verticalBar_ = new QWidget(splitter_);
    verticalBar_->setObjectName("verticalButtonBar");
    verticalBar_->setFixedWidth(28);
    auto *verticalLayout = new QVBoxLayout(verticalBar_);
    verticalLayout->setContentsMargins(1, 0, 1, 0);
    verticalLayout->setSpacing(0);
    splitter_->addWidget(leftHost_);
    splitter_->addWidget(verticalBar_);
    splitter_->addWidget(rightHost_);
    splitter_->setHandleWidth(3);
    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 0);
    splitter_->setStretchFactor(2, 1);
    splitter_->setSizes({580, 28, 580});
    layout->addWidget(splitter_, 1);
    commandRow_ = new QWidget(central);
    commandRow_->setObjectName("commandLineRow");
    auto *command = new QHBoxLayout(commandRow_);
    command->setContentsMargins(0, 2, 0, 2);
    command->setSpacing(3);
    commandPrompt_ = new QLabel(this);
    commandPrompt_->setObjectName("commandPrompt");
    commandPrompt_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    commandPrompt_->setMinimumWidth(90);
    commandPrompt_->setMaximumWidth(450);
    command->addWidget(commandPrompt_);
    commandHistory_ = new QComboBox(this);
    commandHistory_->setEditable(true);
    commandHistory_->setLineEdit(commandLine_);
    if (QSettings().value("session/commands", true).toBool())
        commandHistory_->addItems(QSettings().value("classic/commandHistory").toStringList());
    commandLine_->clear();
    commandLine_->setPlaceholderText({});
    commandLine_->setAccessibleName("Command line");
    command->addWidget(commandHistory_, 1);
    layout->addWidget(commandRow_);
    functionBar_ = new QWidget(central);
    functionBar_->setObjectName("functionKeyBar");
    auto *keys = new QHBoxLayout(functionBar_);
    keys->setContentsMargins(0, 0, 0, 0);
    keys->setSpacing(1);
    layout->addWidget(functionBar_);
    setCentralWidget(central);
    bookmarks_ = new QMenu("Directory hotlist", this);
    bookmarks_->setObjectName("directoryHotlist");
    connect(bookmarks_, &QMenu::aboutToShow, this, &MainWindow::updateBookmarks);

    commands_.add("cm_DirectoryHotlist", "Directory hotlist", QKeySequence("Ctrl+D"), [this] {
        if (active_)
            bookmarks_->popup(active_->mapToGlobal(QPoint(15, 20)));
    });
    commands_.add("cm_GotoNextDir", "Go forward", QKeySequence("Alt+Right"), [this] { active_->forward(); });
    commands_.add("cm_DirectoryHistory", "Directory history", QKeySequence("Alt+Down"),
                  [this] { active_->showHistory(); });
    commands_.add("cm_RestoreFilter", "Restore last filter", QKeySequence("Ctrl+Shift+S"),
                  [this] { active_->focusFilter(true); });
    commands_.add("cm_OpenLeftDrives", "Left drive list", QKeySequence("Alt+F1"),
                  [this] { leftHost_->findChild<QComboBox *>("volumeSelector")->showPopup(); });
    commands_.add("cm_OpenRightDrives", "Right drive list", QKeySequence("Alt+F2"),
                  [this] { rightHost_->findChild<QComboBox *>("volumeSelector")->showPopup(); });
    commands_.add("cm_CommandBrowser", "Additional commands", QKeySequence("Ctrl+Shift+P"),
                  [this] { showCommandBrowser(); });
    commands_.add("cm_CopyPathToCommand", "Copy current path to command line", QKeySequence("Ctrl+P"),
                  [this] {
                      commandLine_->setText(displayPath(active_->path()) + "/");
                      commandLine_->setFocus();
                  });
    commands_.add("cm_CommandHistory", "Command line history", QKeySequence("Alt+F8"),
                  [this] { commandHistory_->showPopup(); });
    commands_.add("cm_SrcShort", "Brief", {}, [this] { active_->setBrief(true); });
    commands_.add("cm_SrcLong", "Full", {}, [this] {
        active_->model()->setColumns({});
        active_->setBrief(false);
        active_->refresh();
    });
    commands_.add("cm_SaveSelection", "Save selection", {}, [this] { active_->saveSelection(); });
    commands_.add("cm_RestoreSelection", "Restore selection", {}, [this] { active_->restoreSelection(); });
    commands_.add("cm_SelectCurrentExtension", "Select same extension", {}, [this] {
        const auto *entry = active_->model()->entry(active_->table()->currentIndex().row());
        if (entry && !entry->parent && !entry->directory)
            active_->selectMask("*" + displayPath(entry->path.extension()));
    });
    commands_.add("cm_CopyNamesToClip", "Copy selected names", {}, [this] {
        QStringList names;
        for (const auto &path : active_->selectedPaths())
            names << displayPath(path.filename());
        QApplication::clipboard()->setText(names.join('\n'));
    });
    commands_.add("cm_CopyDetailsToClip", "Copy selected details", {}, [this] {
        QStringList lines;
        for (const auto &path : active_->selectedPaths())
            for (int row = 0; row < active_->model()->rowCount(); ++row)
                if (active_->model()->entry(row)->path == path) {
                    QStringList values;
                    for (int column = 0; column < 5; ++column)
                        values << active_->model()->index(row, column).data().toString();
                    lines << values.join('\t');
                }
        QApplication::clipboard()->setText(lines.join('\n'));
    });
    commands_.add("cm_CopyPathDetailsToClip", "Copy paths and details", {}, [this] {
        commands_.get("cm_CopyDetailsToClip")->trigger();
        QApplication::clipboard()->setText(displayPath(active_->path()) + "\n" +
                                           QApplication::clipboard()->text());
    });
    commands_.add("cm_CompareDirs", "Compare directories", QKeySequence("Shift+F2"), [this] {
        std::unordered_map<std::filesystem::path, DirectoryEntry> targets;
        for (int row = 0; row < oppositePanel()->model()->rowCount(); ++row) {
            const auto *entry = oppositePanel()->model()->entry(row);
            if (!entry->parent && !entry->directory)
                targets.emplace(entry->path.filename(), *entry);
        }
        std::vector<std::filesystem::path> marked;
        for (int row = 0; row < active_->model()->rowCount(); ++row) {
            const auto *entry = active_->model()->entry(row);
            if (entry->parent || entry->directory)
                continue;
            const auto it = targets.find(entry->path.filename());
            if (it == targets.end() || entry->modified > it->second.modified)
                marked.push_back(entry->path);
        }
        active_->selectPaths(marked);
    });
    for (const auto &pair : {std::pair<QString, int>{"cm_SrcByName", 0},
                             {"cm_SrcByExt", 1},
                             {"cm_SrcBySize", 2},
                             {"cm_SrcByDateTime", 3}})
        commands_.add(pair.first, pair.first, {}, [this, column = pair.second] {
            active_->table()->sortByColumn(column, Qt::AscendingOrder);
        });
    commands_.add("cm_SrcNegOrder", "Reversed order", {}, [this] {
        auto *header = active_->table()->horizontalHeader();
        active_->table()->sortByColumn(
            header->sortIndicatorSection(),
            header->sortIndicatorOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder);
    });
    commands_.add("cm_SrcAllFiles", "All files", {}, [this] {
        active_->clearFilter();
        active_->filterView();
    });
    commands_.add("cm_VerticalPanels", "Vertical arrangement", {}, [this] {
        splitter_->setOrientation(splitter_->orientation() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
    });
    commands_.add("cm_Config", "Options", {}, [this] { showOptions(); });
    commands_.add("cm_ButtonConfig", "Button bar", {}, [this] { showButtonBarEditor(false); });
    commands_.add("cm_ButtonConfig2", "Vertical button bar", {}, [this] { showButtonBarEditor(true); });
    commands_.add("cm_ConfigSavePos", "Save position", {}, [this] {
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("classic/splitter", splitter_->saveState());
    });
    commands_.add("cm_ConfigSaveSettings", "Save settings", {}, [this] { saveSession(); });
    commands_.add("cm_DarkNormal", "Dark/Normal", {}, [this] {
        const bool dark =
            QSettings().value("appearance/theme", qApp->property("commanderDark").toBool() ? 1 : 0).toInt() !=
            1;
        QSettings().setValue("appearance/theme", dark ? 1 : 0);
        qApp->setProperty("commanderDark", dark);
        applySavedAppearance();
        for (auto *panel : findChildren<FilePanel *>())
            panel->setActive(panel == active_);
    });
    commands_.add("cm_HelpIndex", "Help index", QKeySequence("F1"), [this] {
        QMessageBox::information(
            this, "Twin Commander help",
            "F3 View    F4 Edit    F5 Copy    F6 Move    F7 NewFolder    F8 Delete\n\nCtrl+D: directory "
            "hotlist\nCtrl+S: quick filter\nCtrl+L: calculate occupied space\nCtrl+Shift+P: additional Linux "
            "commands\n\nUnavailable reference commands are shown in their original menu positions. Existing "
            "file-operation limits still apply.");
    });
    commands_.add("cm_Keyboard", "Keyboard", {}, [this] { showCommandBrowser(); });
    commands_.add("cm_VisitHomepage", "Visit Totalcmd website", {},
                  [] { QDesktopServices::openUrl(QUrl("https://www.ghisler.com/")); });
    commands_.add("cm_About", "About Twin Commander", {}, [this] {
        QMessageBox::about(
            this, "About Twin Commander",
            "Twin Commander " + QCoreApplication::applicationVersion() +
                "\nNative Linux implementation\n\nUI structure audited against Total Commander 11.58.1 "
                "built-in English resources.\nIndependently implemented; not affiliated with Ghisler "
                "Software GmbH.\nWindows fonts, native window decoration and project-owned icons differ.");
    });

    registerFileCommands();
    registerExtensionCommands();
    registerNetworkCommands();
    const QHash<int, QString> adapters{{2031, "cm_SaveSelectionToFile"},
                                       {2032, "cm_LoadSelectionFromFile"},
                                       {534, "cm_HideSame"},
                                       {500, "cm_DirectoryTree"},
                                       {545, "cm_SearchSeparate"},
                                       {506, "cm_SystemInfo"},
                                       {511, "cm_Terminal"},
                                       {2121, "cm_Desktop"},
                                       {311, "cm_Programs"},
                                       {313, "cm_AllFiles"},
                                       {314, "cm_CustomFilter"},
                                       {2023, "cm_OnlySelected"},
                                       {325, "cm_Unsorted"},
                                       {581, "cm_EditSettings"},
                                       {2027, "cm_PrintList"},
                                       {2028, "cm_PrintListRecursive"},
                                       {504, "cm_PrintContents"},
                                       {564, "cm_CreateChecksumFile"},
                                       {565, "cm_VerifyChecksumFile"},
                                       {518, "cm_TestArchive"},
                                       {333, "cm_ViewModes"},
                                       {502, "cm_SetPermissions"},
                                       {508, "cm_PackFiles"},
                                       {509, "cm_OpenArchive"},
                                       {2022, "cm_CompareFiles"},
                                       {510, "cm_Properties"},
                                       {503, "cm_DirSize"},
                                       {2400, "cm_MultiRenameFiles"},
                                       {24340, "cm_Exit"},
                                       {521, "cm_SelectMask"},
                                       {522, "cm_UnselectMask"},
                                       {523, "cm_SelectAll"},
                                       {524, "cm_ClearAll"},
                                       {525, "cm_ExchangeSelection"},
                                       {527, "cm_SelectCurrentExtension"},
                                       {530, "cm_SaveSelection"},
                                       {529, "cm_RestoreSelection"},
                                       {2017, "cm_CopyNamesToClip"},
                                       {2018, "cm_CopyFullNamesToClip"},
                                       {2036, "cm_CopyDetailsToClip"},
                                       {2037, "cm_CopyPathDetailsToClip"},
                                       {533, "cm_CompareDirs"},
                                       {501, "cm_SearchFor"},
                                       {2020, "cm_FileSync"},
                                       {526, "cm_DirectoryHotlist"},
                                       {570, "cm_GoToPreviousDir"},
                                       {559, "cm_JobQueue"},
                                       {531, "cm_Exchange"},
                                       {532, "cm_MatchSource"},
                                       {301, "cm_SrcShort"},
                                       {302, "cm_SrcLong"},
                                       {305, "cm_VerticalPanels"},
                                       {3001, "cm_OpenNewTab"},
                                       {312, "cm_SrcAllFiles"},
                                       {321, "cm_SrcByName"},
                                       {322, "cm_SrcByExt"},
                                       {323, "cm_SrcBySize"},
                                       {324, "cm_SrcByDateTime"},
                                       {330, "cm_SrcNegOrder"},
                                       {540, "cm_RereadSource"},
                                       {490, "cm_Config"},
                                       {498, "cm_ButtonConfig"},
                                       {583, "cm_ButtonConfig2"},
                                       {493, "cm_ConfigSavePos"},
                                       {580, "cm_ConfigSaveSettings"},
                                       {2950, "cm_DarkNormal"},
                                       {610, "cm_HelpIndex"},
                                       {620, "cm_Keyboard"},
                                       {640, "cm_VisitHomepage"},
                                       {690, "cm_About"}};
    QHash<int, QAction *> actions;
    auto *views = new QActionGroup(this);
    views->setExclusive(true);
    auto *filters = new QActionGroup(this);
    filters->setExclusive(true);
    auto *sort = new QActionGroup(this);
    sort->setExclusive(true);
    std::function<QMenu *(const QJsonObject &, QMenu *)> createMenu;
    createMenu = [&](const QJsonObject &description, QMenu *parent) {
        auto *menu = new QMenu(description.value("caption").toString(),
                               parent ? static_cast<QWidget *>(parent) : this);
        menu->setObjectName("referenceMenu_" + description.value("name").toString());
        menu->setSeparatorsCollapsible(false);
        if (parent) menu->setIcon(commanderIcon(description.value("caption").toString()));
        for (const auto &value : description.value("items").toArray()) {
            const auto item = value.toObject();
            const auto caption = item.value("caption").toString();
            const int id = item.value("command").toInt();
            if (caption == "-") {
                menu->addSeparator();
                continue;
            }
            if (item.contains("items")) {
                menu->addMenu(createMenu(item, menu));
                continue;
            }
            const QString key = adapters.value(id, "tc_" + QString::number(id));
            auto *action = commands_.get(key);
            if (!action) {
                action = commands_.add(key, captionOnly(caption), {}, [] {});
                action->setEnabled(false);
                action->setToolTip("Not implemented in Twin Commander yet");
            } else
                action->setProperty("referenceAvailable", true);
            action->setProperty("tcCommandId", id);
            action->setProperty("referenceCaption", caption);
            // The tab text is retained to preserve reference shortcut spelling (Num +, Alt+Enter).
            action->setText(id == 690 ? "&About Twin Commander..." : caption);
            const auto shortcut = caption.section('\t', 1, 1);
            if (!shortcut.isEmpty())
                action->setShortcut(referenceShortcut(shortcut));
            if (id == 301 || id == 302 || id == 269 || id == 270 || id == 300 || id == 303) {
                action->setCheckable(true);
                views->addAction(action);
                action->setChecked(id == 302);
            }
            if (id >= 321 && id <= 325) {
                action->setCheckable(true);
                sort->addAction(action);
                action->setChecked(id == 321);
            }
            if (id == 311 || id == 312 || id == 313 || id == 314 || id == 2023) {
                action->setCheckable(true);
                filters->addAction(action);
                action->setChecked(id == 312);
            }
            if (id == 330 || id == 305 || id == 304)
                action->setCheckable(true);
            actions.insert(id, action);
            menu->addAction(action);
        }
        return menu;
    };
    menuBar()->setNativeMenuBar(false);
    menuBar()->setObjectName("referenceMenuBar");
    for (const auto &value : reference.value("menus").toArray()) {
        const auto description = value.toObject();
        auto *menu = createMenu(description, nullptr);
        menuBar()->addMenu(menu);
        if (description.value("name").toString() == "Start1") {
            auto *start = commands_.get("tc_700");
            start->setText("Change &Start Menu...");
            start->setProperty("tcCommandId", 700);
            start->setProperty("referenceAvailable", true);
            menu->addAction(start);
        }
        if (description.value("rightAligned").toBool()) {
            menu->menuAction()->setVisible(false);
            auto *help = new QToolButton(menuBar());
            help->setObjectName("rightAlignedHelp");
            help->setText("&Help");
            help->setAutoRaise(true);
            help->setMenu(menu);
            help->setPopupMode(QToolButton::InstantPopup);
            menuBar()->setCornerWidget(help, Qt::TopRightCorner);
        }
    }
    for (const auto &barName : {QString("toolbar"), QString("verticalToolbar")})
        for (const auto &value : reference.value(barName).toArray()) {
            const auto definition = value.toObject();
            if (auto *action = actions.value(definition.value("nativeId").toInt()))
                commands_.alias(definition.value("command").toString(), action);
        }
    const auto names = commanderCommandIds();
    for (auto it = names.begin(); it != names.end(); ++it)
        if (auto *action = actions.value(it.value().toInt()))
            commands_.alias(it.key(), action);
    rebuildStartMenu();
    reloadCustomMenu();
    commands_.get("cm_OpenArchive")
        ->setToolTip("Browse archive and unpack files into a new folder (Linux adaptation)");
    actions[503]->setToolTip("Calculate occupied space (Ctrl+L)");
    actions[540]->setShortcuts({QKeySequence("Ctrl+R"), QKeySequence("F2")});
    const QStringList functionActions{"cm_List",  "cm_Edit",   "cm_Copy", "cm_RenMov",
                                      "cm_MkDir", "cm_Delete", "cm_Exit"};
    const auto captions = reference.value("functionKeys").toArray();
    for (int i = 0; i < functionActions.size(); ++i) {
        auto *button = new QPushButton(captions[i].toString(), functionBar_);
        button->setObjectName("function_" + functionActions[i]);
        button->setProperty("iconCommand", functionActions[i]);
        button->setIcon(commanderIcon(functionActions[i], 16));
        button->setIconSize(QSize(16, 16));
        button->setFixedHeight(23);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setFlat(true);
        button->setAccessibleName(captions[i].toString());
        connect(button, &QPushButton::clicked, commands_.get(functionActions[i]), &QAction::trigger);
        keys->addWidget(button, 1);
    }
    buttonBar_ = addToolBar("Button bar");
    buttonBar_->setObjectName("referenceButtonBar");
    buttonBar_->setMovable(false);
    buttonBar_->setIconSize(QSize(24, 24));
    buttonBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    rebuildButtonBar(false);
    rebuildButtonBar(true);
    applyUserCommands();
    statusBar()->hide();
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &message) {
        commandHistory_->setToolTip(message);
        statusBar()->hide();
    });
}

void MainWindow::applyLayoutOptions() {
    QSettings settings;
    const auto defaults = commanderUiReference().value("layoutDefaults").toObject();
    const auto show = [&](const QString &key) {
        return settings.value("classic/layout/" + key, defaults.value(key).toInt(1)).toBool();
    };
    buttonBar_->setVisible(show("ButtonBar"));
    verticalBar_->setVisible(show("ButtonBarVertical"));
    commandRow_->setVisible(show("CmdLine"));
    functionBar_->setVisible(show("KeyButtons"));
    for (auto *host : {leftHost_, rightHost_}) {
        host->showDriveButtons(show("DriveBar1") && (ApplicationPreferences::flag("idTwoDriveBars") || (host==leftHost_ ? left_->indexOf(active_)>=0 : right_->indexOf(active_)>=0)));
        host->findChild<QComboBox *>("volumeSelector")->setVisible(show("DriveCombo"));
    }
    for (auto *panel : findChildren<FilePanel *>()) {
        if (auto *path = panel->findChild<QWidget *>("currentDirectoryBar"))
            path->setVisible(show("CurDir"));
        if (auto *status = panel->findChild<QLabel *>("panelStatus"))
            status->setVisible(show("StatusBar"));
        panel->table()->horizontalHeader()->setVisible(show("TabHeader"));
        for (const auto &name : {QString("directoryHotlistButton"), QString("directoryHistoryButton")})
            if (auto *button = panel->findChild<QToolButton *>(name))
                button->setVisible(show("HistoryHotlistButtons"));
    }
    for (auto *tabs : {left_, right_})
        tabs->tabBar()->setVisible(
            show("DirectoryTabs") &&
            (tabs->count() > 1 || settings.value("classic/alwaysShowTabs", false).toBool()));
    leftHost_->updateTabLayout(show("DirectoryTabs"));rightHost_->updateTabLayout(show("DirectoryTabs"));
}
void MainWindow::updateUiState() {
    if (!active_ || !leftHost_ || !commandPrompt_)
        return;
    const auto text = displayPath(active_->path()) + ">";
    commandPrompt_->setText(QFontMetrics(commandPrompt_->font()).elidedText(text, Qt::ElideLeft, 440));
    commandPrompt_->setToolTip(text);
    const auto mode = active_->property("displayMode").toString();
    const int viewId = mode == "brief"        ? 301
                       : mode == "tree"       ? 303
                       : mode == "thumbnails" ? 269
                       : mode == "comments"   ? 300
                       : mode == "custom"     ? 270
                                              : 302;
    for (const auto &entry : {std::pair<QString, int>{"cm_SrcShort", 301},
                              {"cm_SrcLong", 302},
                              {"tc_269", 269},
                              {"tc_270", 270},
                              {"tc_300", 300},
                              {"tc_303", 303}})
        if (auto *action = commands_.get(entry.first))
            action->setChecked(entry.second == viewId);
    const int column = active_->table()->horizontalHeader()->sortIndicatorSection();
    for (const auto &entry : {std::pair<QString, int>{"cm_SrcByName", 0},
                              {"cm_SrcByExt", 1},
                              {"cm_SrcBySize", 2},
                              {"cm_SrcByDateTime", 3},
                              {"cm_Unsorted", -1}})
        if (auto *action = commands_.get(entry.first))
            action->setChecked(entry.second == column);
    if (auto *action = commands_.get("cm_SrcNegOrder"))
        action->setChecked(active_->table()->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder);
    const int filter =
        active_->property("viewFilter").isValid() ? active_->property("viewFilter").toInt() : 312;
    for (auto *action : findChildren<QAction *>())
        if (action->isCheckable() && action->property("tcCommandId").isValid()) {
            const int id = action->property("tcCommandId").toInt();
            if (id == 311 || id == 312 || id == 313 || id == 314 || id == 2023)
                action->setChecked(id == filter);
        }
    applyLayoutOptions();
}
void MainWindow::showOptions() {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("referenceConfiguration");
    dialog->setWindowTitle("Configuration");
    dialog->setFixedSize(556, 420);
    const auto reference = referenceDialogMetadata("DLGCONFIGALL");
    const QStringList categories{"Layout",         "Display",
                                 "  Icons",        "  Font",
                                 "  Color",        "  Tabstops/Data Formats",
                                 "  Folder-Tabs",  "  Custom Columns",
                                 "  View Mode",    "    Auto Switch Mode",
                                 "  Language",     "Operation",
                                 "  Edit/View",    "  Copy/Delete",
                                 "  Refresh",      "  Quick Search",
                                 "  FTP",          "  Plugins",
                                 "  Thumbnails",   "  Log file",
                                 "  Ignore list",  "  History",
                                 "Packer",         "  ZIP Packer",
                                 "  7-ZIP Packer", "Misc."};
    auto *list = new QListWidget(dialog);
    list->setObjectName("configurationCategories");
    list->setIconSize(QSize(12, 12));
    list->setGeometry(1, 0, 132, 392);
    for (const auto &category : categories) {
        auto *item = new QListWidgetItem(commanderIcon(category, 16), category, list);
        item->setToolTip(category.trimmed());
        item->setSizeHint(QSize(158, 15));
    }
    auto *title = new QLabel("Layout", dialog);
    title->setGeometry(136, 1, 417, 20);
    title->setAlignment(Qt::AlignCenter);
    title->setAutoFillBackground(true);
    auto palette = title->palette();
    palette.setColor(QPalette::Window, palette.color(QPalette::Highlight));
    palette.setColor(QPalette::WindowText, Qt::white);
    title->setPalette(palette);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    auto *pages = new QStackedWidget(dialog);
    pages->setObjectName("configurationPages");
    pages->setGeometry(136, 21, 417, 371);
    const QMap<QString, QString> bindings{
        {"idShowButtonBar", "ButtonBar"},    {"idShowButtonBar2", "ButtonBarVertical"},
        {"idShowDriveButtons", "DriveBar1"}, {"idShowDriveCombo", "DriveCombo"},
        {"idShowCurDir", "CurDir"},          {"idShowTabheader", "TabHeader"},
        {"idShowStatusbar", "StatusBar"},    {"idShowCmdLine", "CmdLine"},
        {"idShowKeyButtons", "KeyButtons"},  {"idShowHistHotButtons", "HistoryHotlistButtons"},
        {"idShowTabs", "DirectoryTabs"}};
    QSet<QString> enabled;
    QMap<QString, QVariant> values;
    QSettings settings;
    const auto defaults = commanderUiReference().value("layoutDefaults").toObject();
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        enabled.insert(it.key());
        values.insert(it.key(),
                      settings.value("classic/layout/" + it.value(), defaults.value(it.value()).toInt(1)));
    }
    values.insert("idFlatDriveBar", true);
    values.insert("idFlatInterface", true);
    values.insert("idXPthemeBackground", true);
    values.insert("idShowBreadcrumb", true);
    for (const auto &child : reference.value("children").toArray())
        if (child.toObject().value("name") == "Notebook") {
            for (const auto &page : child.toObject().value("children").toArray()) {
                auto *widget = renderReferenceControls(page.toObject(), pages, enabled, values);
                pages->addWidget(widget);
            }
        }
    connect(list, &QListWidget::currentRowChanged, dialog, [pages, title, categories](int index) {
        pages->setCurrentIndex(index);
        title->setText(categories.value(index).trimmed());
    });
    list->setCurrentRow(0);
    configureAppearance(dialog);
    configurePreferenceEditors(dialog);
    configurePanelPreferences(dialog);
    InterfacePreferences::configure(dialog);
    ApplicationPreferences::configure(dialog);
    auto apply = [this, dialog, bindings] {
        QSettings settings;
        for (auto it = bindings.begin(); it != bindings.end(); ++it)
            if (auto *check = dialog->findChild<QCheckBox *>(it.key()))
                settings.setValue("classic/layout/" + it.value(), check->isChecked());
        const auto appearance = dialog->property("appearanceDraft").toMap();
        for (auto it = appearance.begin(); it != appearance.end(); ++it)
            settings.setValue(it.key(), it.value());
        if (dialog->property("viewModesDraft").isValid())
            settings.setValue("viewModes", dialog->property("viewModesDraft"));
        if (dialog->property("resetColumns").toBool())
            for (auto *panel : findChildren<FilePanel *>()) {
                panel->table()->setColumnWidth(1, 55);
                panel->table()->setColumnWidth(2, 90);
                panel->table()->setColumnWidth(3, 127);
                panel->table()->setColumnWidth(4, 42);
            }
        applySavedAppearance();
        applyPanelPreferences();
        ApplicationPreferences::apply();
        reloadCustomMenu();
        rebuildButtonBar(false);
        rebuildButtonBar(true);
        for (auto *panel : findChildren<FilePanel *>()) {
            panel->setActive(panel == active_);
            panel->model()->setShowHidden(settings.value("display/hidden").toBool());
            if (panel->property("displayMode") == "thumbnails")
                panel->setDisplayMode("thumbnails");
            else
                panel->refresh();
        }
        applyLayoutOptions();
    };
    const QStringList names{"OK", "Cancel", "Help", "Apply"};
    const QList<int> x{219, 303, 387, 471};
    for (int i = 0; i < names.size(); ++i) {
        auto *button = new QPushButton(names[i], dialog);
        button->setObjectName("configuration" + names[i]);
        button->setGeometry(x[i], 395, 80, 23);
        if (i == 0) {
            button->setDefault(true);
            connect(button, &QPushButton::clicked, dialog, [dialog, apply] {
                apply();
                dialog->accept();
            });
        }
        if (i == 1)
            connect(button, &QPushButton::clicked, dialog, &QDialog::reject);
        if (i == 2)
            connect(button, &QPushButton::clicked, this,
                    [this] { commands_.get("cm_HelpIndex")->trigger(); });
        if (i == 3)
            connect(button, &QPushButton::clicked, dialog, apply);
    }
    // Allow room for category icons without truncating the existing labels.
    list->setFixedWidth(164);
    for (auto *child : dialog->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        if (child != list && child->x() >= 132) child->move(child->x() + 32, child->y());
    dialog->setFixedSize(588, 420);
    InterfacePreferences::adaptDialog(dialog);
    dialog->show();
}
void MainWindow::showCommandBrowser() {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Choose command");
    dialog->resize(700, 520);
    auto *layout = new QVBoxLayout(dialog);
    auto *filter = new QLineEdit(dialog);
    filter->setPlaceholderText("Filter commands");
    layout->addWidget(filter);
    auto *list = new QListWidget(dialog);
    layout->addWidget(list);
    const auto actions = findChildren<QAction *>();
    const auto populate = [actions, list](const QString &text) {
        list->clear();
        for (auto *action : actions) {
            if (action->objectName().isEmpty() || action->isSeparator())
                continue;
            const auto label = action->objectName() + "  " + captionOnly(action->text()).remove('&');
            if (!label.contains(text, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, QVariant::fromValue(action));
            if (!action->isEnabled())
                item->setForeground(Qt::gray);
        }
    };
    connect(filter, &QLineEdit::textChanged, dialog, populate);
    populate({});
    auto *footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(footer);
    const auto run = [list, dialog] {
        if (auto *item = list->currentItem()) {
            auto *action = item->data(Qt::UserRole).value<QAction *>();
            if (action && action->isEnabled()) {
                dialog->accept();
                action->trigger();
            }
        }
    };
    connect(footer, &QDialogButtonBox::accepted, dialog, run);
    connect(footer, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, dialog, [run] { run(); });
    dialog->show();
}
