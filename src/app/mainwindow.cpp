#include "ui/icons/commandicons.h"
#include "filesystem/pathencoding.h"
#include "filesystem/directorysizes.h"
#include "ui/panels/foldertabpreferences.h"
#include "ui/panels/nativepathmime.h"
#include "operations/operationlog.h"
#include "app/mainwindow.h"
#include "ui/dialogs/fileoperationdialogs.h"
#include "operations/iothreadpool.h"
#include "ui/panels/filepanellayout.h"
#include "filesystem/filetransforms.h"
#include "archives/externalarchivebackend.h"
#include "settings/applicationpreferences.h"
#include "settings/interfacepreferences.h"
#include "platform/nativeextensions.h"
#include "settings/panelpreferences.h"
#include "network/remoteservices.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QShortcut>
#include <QTabBar>
#include <QUuid>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

MainWindow::MainWindow(const QStringList &initialPaths, QWidget *parent)
    : QMainWindow(parent), left_(new QTabWidget(this)), right_(new QTabWidget(this)),
      splitter_(new QSplitter(this)), commandLine_(new QLineEdit(this)), commands_(this) {
    applyCommanderAppearance();
    CommandIcons::install();
    setWindowIcon(CommandIcons::applicationIcon());
    setProperty("nativeCommanderWindow", true);
    queue_ = new JobQueue(this);
    queue_->setJournalDirectory(journalDirectory());
    setWindowTitle("Twin Commander");
    connect(queue_, &JobQueue::jobFinished, this, [this](quint64 id) {
        for (auto *panel : findChildren<FilePanel *>())
            panel->refresh();
        QSettings settings;
        const auto sound = settings.value("sound/file").toString();
        const auto job = std::find_if(queue_->jobs().begin(), queue_->jobs().end(),
                                      [id](const auto &job) { return job.id == id; });
        const auto elapsed =
            job == queue_->jobs().end() ? 0 : QDateTime::currentMSecsSinceEpoch() - job->startedAt;
        if (!sound.isEmpty() &&
            (!settings.value("sound/onlyLong").toBool() ||
             elapsed >= qBound(0, settings.value("sound/minSeconds", 10).toInt(), 3600) * 1000LL))
            for (const auto &player : {QString("paplay"), QString("aplay")}) {
                const auto program = QStandardPaths::findExecutable(player);
                if (!program.isEmpty()) {
                    QProcess::startDetached(program, {sound});
                    break;
                }
            }
        if (job != queue_->jobs().end()) {
            const auto label = job->label.toLower();
            const QString category = (label.contains("archive") || label.contains("pack"))   ? "archive"
                                     : (label.contains("delete") || label.contains("trash")) ? "delete"
                                     : (label.contains("folder") || label.contains("mkdir")) ? "folders"
                                                                                             : "transfer";
            if (job->result.cancelled)
                OperationLog::append(category, "error", job->label + ": cancelled");
            for (const auto &item : job->result.items)
                OperationLog::append(category,
                                 item.skipped           ? "skipped"
                                 : item.error.isEmpty() ? "success"
                                                        : "error",
                                 job->label + ": " + displayPath(item.source) + " → " +
                                     displayPath(item.destination) +
                                     (item.error.isEmpty() ? QString() : " — " + item.error));
        }
    });
    resize(1200, 740);
    QFont mainFont = font();
    if (QSettings().contains("appearance/mainFont")) {
        mainFont.fromString(QSettings().value("appearance/mainFont").toString());
        if (QSettings().value("appearance/manualDpi").toBool()) {
            const double scale = qBound(72, QSettings().value("appearance/dpi", 96).toInt(), 240) / 96.0;
            if (mainFont.pixelSize() > 0)
                mainFont.setPixelSize(qMax(6, qRound(mainFont.pixelSize() * scale)));
            else
                mainFont.setPointSizeF(qMax(4.0, mainFont.pointSizeF() * scale));
        }
        setFont(mainFont);
    }
    commands_.add("cm_GoToParent", "Parent directory", QKeySequence("Alt+Up"),
                  [this] { active_->parentDirectory(); });
    commands_.add("cm_GoToPreviousDir", "Back", QKeySequence("Alt+Left"), [this] { active_->back(); });
    commands_.add("cm_RereadSource", "Refresh", QKeySequence("Ctrl+R"), [this] { active_->refresh(); });
    commands_.add("cm_EditPath", "Edit path", QKeySequence(), [this] { active_->focusPath(); });
    commands_.add("cm_SwitchHidSys", "Toggle hidden files", QKeySequence("Ctrl+H"),
                  [this] { active_->toggleHidden(); });
    commands_.add("cm_OpenNewTab", "New tab", QKeySequence("Ctrl+T"), [this] {
        auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
        addTab(tabs, active_->path());
    });
    commands_.add("cm_CloseCurrentTab", "Close tab", QKeySequence("Ctrl+W"), [this] {
        auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
        if (tabs->count() > 1) {
            auto *panel = tabs->currentWidget();
            tabs->removeTab(tabs->currentIndex());
            panel->deleteLater();
        }
    });
    commands_.add("cm_FocusCommandLine", "Directory command", QKeySequence("Ctrl+Shift+L"),
                  [this] { commandLine_->setFocus(); });
    commands_.add("cm_Exit", "Exit", QKeySequence("Alt+F4"), [this] { close(); });

    commands_.add("cm_List", "View", QKeySequence("F3"), [this] { openCurrent(false); });
    commands_.add("cm_Edit", "Edit UTF-8 text", QKeySequence("F4"), [this] { openCurrent(true); });
    commands_.add("cm_Copy", "Copy", QKeySequence("F5"),
                  [this] { runOperation(LocalFiles::Operation::Copy); });
    commands_.add("cm_RenMov", "Move", QKeySequence("F6"),
                  [this] { runOperation(LocalFiles::Operation::Move); });
    commands_.add("cm_MkDir", "New folder", QKeySequence("F7"),
                  [this] { runOperation(LocalFiles::Operation::MakeDirectory); });
    commands_.add("cm_CreateFile", "New text file", QKeySequence("Shift+F4"),
                  [this] { runOperation(LocalFiles::Operation::MakeFile); });
    commands_.add("cm_Checksum", "SHA-256 checksums", QKeySequence(), [this] { showChecksums(); });
    commands_.add("cm_Delete", "Move to trash", QKeySequence("F8"), [this] {
        runOperation(ApplicationPreferences::flag("IdConf_trashcan") ? LocalFiles::Operation::Trash
                                                            : LocalFiles::Operation::Delete);
    });
    commands_.get("cm_Delete")->setShortcuts({QKeySequence("F8"), QKeySequence("Delete")});
    commands_.add("cm_DeletePermanently", "Delete permanently", QKeySequence("Shift+Delete"),
                  [this] { runOperation(LocalFiles::Operation::Delete); });
    commands_.add("cm_RenameOnly", "Rename", QKeySequence("Shift+F6"),
                  [this] { runOperation(LocalFiles::Operation::Rename); });
    commands_.add("cm_SearchFor", "Find files", QKeySequence("Alt+F7"), [this] {
        auto *dialog = new SearchDialog(active_->path(), this);
        QPointer<FilePanel> panel(active_);
        connect(dialog, &SearchDialog::revealRequested, this,
                [this, panel](const std::filesystem::path &path) {
                    if (panel) {
                        panel->reveal(path);
                        activateWindow();
                        panel->focusFiles();
                    }
                });
        connect(dialog, &SearchDialog::resultsRequested, this, &MainWindow::showSearchResults);
        dialog->show();
    });
    commands_.add("cm_QuickFilter", "Quick filter", QKeySequence("Ctrl+S"),
                  [this] { active_->focusFilter(); });
    commands_.add("cm_SelectAll", "Select all", QKeySequence(), [this] { active_->selectMask("*"); });
    commands_.add("cm_ClearAll", "Clear selection", QKeySequence(),
                  [this] { active_->table()->clearSelection(); });
    commands_.add("cm_ExchangeSelection", "Invert selection", QKeySequence(),
                  [this] { active_->invertSelection(); });
    commands_.add("cm_SelectMask", "Select by mask", QKeySequence(), [this] {
        bool ok = false;
        const auto mask = QInputDialog::getText(
            this, "Select by mask", "Filename mask (for example *.cpp):", QLineEdit::Normal, "*", &ok);
        if (ok)
            active_->selectMask(mask);
    });
    commands_.add("cm_UnselectMask", "Deselect by mask", QKeySequence(), [this] {
        bool ok = false;
        const auto mask =
            QInputDialog::getText(this, "Deselect by mask", "Filename mask:", QLineEdit::Normal, "*", &ok);
        if (ok)
            active_->selectMask(mask, false);
    });
    commands_.add("cm_CopyFullNamesToClip", "Copy selected paths", QKeySequence("Ctrl+Shift+C"), [this] {
        QStringList paths;
        for (const auto &path : active_->selectedPaths())
            paths << displayPath(path);
        QApplication::clipboard()->setText(paths.join('\n'));
    });
    commands_.add("cm_Properties", "Properties", QKeySequence("Alt+Return"), [this] { showProperties(); });
    commands_.add("cm_MatchSource", "Open source directory in opposite panel", QKeySequence("Ctrl+Right"),
                  [this] { oppositePanel()->navigate(active_->path()); });
    commands_.add("cm_Exchange", "Swap panel directories", QKeySequence("Ctrl+U"), [this] {
        auto *left = qobject_cast<FilePanel *>(left_->currentWidget());
        auto *right = qobject_cast<FilePanel *>(right_->currentWidget());
        const auto a = left->path(), b = right->path();
        if (a.empty() || b.empty())
            return;
        left->navigate(b);
        right->navigate(a);
        active_->focusFiles();
    });
    commands_.add("cm_GoHome", "Home", QKeySequence("Alt+Home"),
                  [this] { active_->navigate(nativePath(QDir::homePath())); });
    commands_.add("cm_GoRoot", "Filesystem root", QKeySequence(), [this] { active_->navigate("/"); });
    commands_.add("cm_AddBookmark", "Add current dir", QKeySequence(), [this] {
        QSettings settings;
        auto bookmarks = settings.value("bookmarksNative").toList();
        const auto encoded = QByteArray::fromStdString(active_->path().native());
        if (!bookmarks.contains(encoded))
            bookmarks.append(encoded);
        settings.setValue("bookmarksNative", bookmarks);
        updateBookmarks();
    });
    commands_.add("cm_MultiRenameFiles", "Multi-rename preview", QKeySequence("Ctrl+M"), [this] {
        const auto paths = active_->selectedPaths();
        if (!paths.empty())
            (new BatchRenameDialog(paths, queue_, journalDirectory(), this))->show();
    });
    commands_.add("cm_OpenArchive", "Browse archive", QKeySequence(), [this] { openArchive(); });
    commands_.add("cm_PackFiles", "Create archive", QKeySequence("Alt+F5"), [this] { packArchive(); });
    commands_.add("cm_FileSync", "Compare directories / synchronize", QKeySequence(), [this] {
        (new DirectoryComparisonDialog(qobject_cast<FilePanel *>(left_->currentWidget())->path(),
                           qobject_cast<FilePanel *>(right_->currentWidget())->path(), queue_,
                           journalDirectory(), this))
            ->show();
    });
    commands_.add("cm_CompareFiles", "Compare selected files by content", QKeySequence(),
                  [this] { compareCurrentFiles(); });
    commands_.add("cm_DirSize", "Calculate selected folder sizes", QKeySequence("Ctrl+L"),
                  [this] { showStatistics(); });
    commands_.add("cm_FindDuplicates", "Find duplicate candidates", QKeySequence(),
                  [this] { findDuplicates(); });
    commands_.add("cm_SetPermissions", "Change permissions", QKeySequence(), [this] { editPermissions(); });
    commands_.add("cm_CreateSymlink", "Create symbolic link", QKeySequence(), [this] { createLink(); });
    commands_.add("cm_JobQueue", "Background operations", QKeySequence("Ctrl+J"), [this] { showQueue(); });
    commands_.add("cm_OperationHistory", "Previous job reports", QKeySequence(), [this] { showHistory(); });
    buildMainWindowUi();
    commands_.add("cm_CloseOtherTabs", "Close all other tabs", QKeySequence("Ctrl+Shift+W"), [this] {
        auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
        if (tabs->count() <= 1)
            return;
        if (QSettings().value("tabs/confirmClose").toBool() &&
            QMessageBox::question(this, "Close other tabs", "Close all tabs except the current tab?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        auto *keep = tabs->currentWidget();
        for (int i = tabs->count() - 1; i >= 0; --i)
            if (tabs->widget(i) != keep) {
                auto *w = tabs->widget(i);
                tabs->removeTab(i);
                w->deleteLater();
            }
    });

    for (auto *tabs : {left_, right_}) {
        tabs->setDocumentMode(true);
        tabs->setTabsClosable(false);
        tabs->setTabBarAutoHide(true);
        tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tabs->tabBar(), &QWidget::customContextMenuRequested, this, [tabs](const QPoint &point) {
            FolderTabPreferences::showContextMenu(tabs, tabs->tabBar()->tabAt(point), tabs->tabBar()->mapToGlobal(point));
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int index) {
            if (index >= 0) {
                auto *panel = qobject_cast<FilePanel *>(tabs->widget(index));
                if (!QSettings().value("tabs/separateFilters", true).toBool())
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->widget(i)->property("tabIdentity") == tabs->property("lastTab")) {
                            auto *previous = qobject_cast<FilePanel *>(tabs->widget(i));
                            if (previous != panel)
                                panel->filterView(previous->property("viewMask").toString(),
                                                  previous->property("viewPrograms").toBool());
                            break;
                        }
                if (ApplicationPreferences::flag("ChkTabsRevertToRoot") &&
                    !panel->property("lockedRoot").toByteArray().isEmpty()) {
                    const auto root =
                        std::filesystem::path(panel->property("lockedRoot").toByteArray().toStdString());
                    if (panel->path() != root)
                        panel->navigate(root);
                }
                tabs->setProperty("previousTab", tabs->property("lastTab"));
                tabs->setProperty("lastTab", panel->property("tabIdentity"));
                if (QSettings().value("tabs/activate", true).toBool() || tabs->indexOf(active_) >= 0) {
                    activate(panel);
                    panel->focusFiles();
                }
            }
        });
        connect(tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, [tabs](int index) {
            if (index < 0 || tabs->count() <= 1 || !QSettings().value("tabs/doubleClose").toBool())
                return;
            auto *panel = tabs->widget(index);
            tabs->removeTab(index);
            panel->deleteLater();
        });
        connect(tabs, &QTabWidget::tabCloseRequested, this, [tabs](int index) {
            if (tabs->count() <= 1)
                return;
            auto *panel = tabs->widget(index);
            tabs->removeTab(index);
            panel->deleteLater();
        });
    }
    connect(commandLine_, &QLineEdit::returnPressed, this, &MainWindow::executeCommandLine);
    restoreSession(initialPaths);
    activate(qobject_cast<FilePanel *>(left_->currentWidget()));
    active_->focusFiles();
    applyLayoutOptions();
    updateUiState();
    applyPanelPreferences();
    CommandIcons::decorate(this);
    ApplicationPreferences::apply();
    for (auto *button : functionBar_->findChildren<QPushButton *>())
        if (button->objectName() == "function_cm_Delete") {
            button->setAcceptDrops(true);
            button->setProperty("deleteDropTarget", true);
            button->installEventFilter(this);
        }
    connect(commandLine_, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (auto *panel : findChildren<FilePanel *>())
            panel->setProperty("commandLineText", text);
    });
    auto *cycle = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(cycle, &QShortcut::activated, this, [this] {
        auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
        if (QSettings().value("tabs/list").toBool()) {
            auto *menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            for (int i = 0; i < tabs->count(); ++i) {
                auto *target = tabs->widget(i);
                connect(menu->addAction(tabs->tabText(i)), &QAction::triggered, tabs,
                        [tabs, target] { tabs->setCurrentWidget(target); });
            }
            menu->popup(tabs->mapToGlobal(QPoint(0, 20)));
            return;
        }
        int before = -1;
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i)->property("tabIdentity") == tabs->property("previousTab"))
                before = i;
        tabs->setCurrentIndex(QSettings().value("tabs/mru").toBool() && before >= 0
                                  ? before
                                  : (tabs->currentIndex() + 1) % tabs->count());
    });
    auto *open = new QShortcut(QKeySequence("Ctrl+Up"), this);
    connect(open, &QShortcut::activated, this, [this] {
        auto target = active_->path();
        const auto *entry = active_->model()->entry(active_->table()->currentIndex().row());
        if (entry && entry->directory)
            target = entry->path;
        addTab(left_->indexOf(active_) >= 0 ? left_ : right_, target);
    });
}

void MainWindow::addTab(QTabWidget *tabs, const std::filesystem::path &path) {
    auto *panel = new FilePanel(path.empty() ? nativePath(QDir::homePath()) : path, tabs);
    panel->setProperty("tabIdentity", QUuid::createUuid().toString());
    connect(panel, &FilePanel::activated, this, [this, panel] { activate(panel); });
    panel->setProperty("commandLineText", commandLine_->text());
    connect(panel, &FilePanel::executeCommandRequested, this, &MainWindow::executeCommandLine);
    connect(panel, &FilePanel::treeDirectoryRequested, this, [this, panel](const auto &path) {
        activate(panel);
        oppositePanel()->navigate(path);
    });
    connect(panel, &FilePanel::filesDropped, this, [this, panel](const auto &paths, bool move) {
        QString summary;
        if (ApplicationPreferences::flag("IdConf_CountfilesCopy") &&
            !DirectorySizes::confirmPreflight(paths, this, summary))
            return;
        if (ApplicationPreferences::flag("confirm_mouse") &&
            QMessageBox::question(
                this, move ? "Move dropped files" : "Copy dropped files",
                QString("%1 items to %2?\n%3").arg(paths.size()).arg(displayPath(panel->path()), summary),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        LocalFiles::Request request;
        request.operation = move ? LocalFiles::Operation::Move : LocalFiles::Operation::Copy;
        request.sources = paths;
        request.destination = panel->path();
        startOperation(request);
    });
    connect(panel, &FilePanel::switchPanel, this, &MainWindow::switchPanel);
    connect(panel->table()->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { updateQuickView(); });
    connect(panel, &FilePanel::hotlistRequested, this,
            [this] { commands_.get("cm_DirectoryHotlist")->trigger(); });
    connect(panel->table()->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this] { updateUiState(); });
    connect(panel, &FilePanel::openFileRequested, this, [this](const std::filesystem::path &path) {
        try {
            if (NativeExtensions::launchAssociated(path, "open"))
                return;
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "Open file", QString::fromLocal8Bit(error.what()));
            return;
        }
        const auto lower = displayPath(path.filename()).toLower();
        if (ApplicationPreferences::flag("zip_likedirs") &&
            (lower.endsWith(".zip") || lower.endsWith(".tar") || lower.endsWith(".tar.gz") ||
             lower.endsWith(".tgz") || lower.endsWith(".tar.xz") || lower.endsWith(".tar.bz2") ||
             lower.endsWith(".7z") || lower.endsWith(".arj") || lower.endsWith(".lzh") ||
             lower.endsWith(".rar") || lower.endsWith(".ace"))) {
            (new ArchiveBrowserDialog(path, oppositePanel()->path(), queue_, this))->show();
            return;
        }
        if (nativePath(displayPath(path)) != path) {
            statusBar()->showMessage("Desktop launch requires a decodable path; use F3 to view", 10000);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(displayPath(path))))
            statusBar()->showMessage("No application could open the file", 10000);
    });
    connect(panel, &FilePanel::contextMenuRequested, this, [this](const QPoint &position) {
        QMenu menu(this);
        for (const QString id : {"cm_List", "cm_Edit", "cm_Copy", "cm_RenMov", "cm_RenameOnly", "cm_Delete",
                                 "cm_Properties", "cm_CopyFullNamesToClip"})
            menu.addAction(commands_.get(id));
        menu.exec(position);
    });
    connect(panel, &FilePanel::errorOccurred, this,
            [this](const QString &message) { statusBar()->showMessage(message, 10000); });
    connect(panel, &FilePanel::pathChanged, this, [this, tabs, panel](const QString &pathText) {
        const auto native = QByteArray::fromStdString(panel->path().native());
        const bool changed = panel->property("lastUsagePath").toByteArray() != native;
        if (changed) {
            panel->setProperty("lastUsagePath", native);
            auto usage = qApp->property("frequentUsage").toMap();
            const auto encoded = QString::fromLatin1(native.toBase64());
            usage[encoded] = usage.value(encoded).toInt() + 1;
            qApp->setProperty("frequentUsage", usage);
            if (QSettings().value("autoView/enabled", true).toBool())
                for (const auto &entry : QSettings().value("autoViewRules").toList()) {
                    const auto rule = entry.toMap();
                    if (!QDir::match(rule.value("path", "*").toString(), displayPath(panel->path())))
                        continue;
                    bool matches = true;
                    for (const auto &mask : rule.value("masks").toString().split(';', Qt::SkipEmptyParts)) {
                        bool found = false;
                        for (int row = 0; row < panel->model()->rowCount(); ++row)
                            if (!panel->model()->entry(row)->parent &&
                                QDir::match(mask, displayPath(panel->model()->entry(row)->path.filename())))
                                found = true;
                        if (!found)
                            matches = false;
                    }
                    if (matches) {
                        const auto mode = QSettings()
                                              .value("viewModes")
                                              .toMap()
                                              .value(rule.value("mode").toString())
                                              .toMap();
                        if (!mode.isEmpty()) {
                            if (panel->property("viewModePriority").toBool() &&
                                !mode.value("priority").toBool())
                                continue;
                            if (panel == active_)
                                applyViewMode(mode);
                            else
                                panel->setProperty("pendingViewMode", mode);
                        }
                        break;
                    }
                }
        }
        if (!panel->property("autoRefreshing").toBool() ||
            QSettings().value("refresh/header", true).toBool()) {
            leftHost_->refresh();
            rightHost_->refresh();
        }
        updateUiState();
        const int index = tabs->indexOf(panel);
        if (index >= 0) {
            const QString name = QDir(pathText).dirName();
            QString title = name.isEmpty() ? pathText : name;
            if (QSettings().value("tabs/limit").toBool())
                title = title.left(qBound(3, QSettings().value("tabs/titleLength", 20).toInt(), 100));
            tabs->setTabText(index, title);
            tabs->setTabToolTip(index, pathText);
            applyPanelPreferences(false);
        }
    });
    auto *old = tabs->currentWidget();
    const int position =
        QSettings().value("tabs/nearCurrent").toBool() ? tabs->currentIndex() + 1 : tabs->count();
    const int index = tabs->insertTab(position, panel, "Loading…");
    if (QSettings().value("tabs/foreground", true).toBool() || !old)
        tabs->setCurrentIndex(index);
    applyPanelPreferences(false);
    if (QSettings().value("tabs/foreground", true).toBool() || !old)
        panel->focusFiles();
}

void MainWindow::activate(FilePanel *panel) {
    if (!panel)
        return;
    active_ = panel;
    const auto pending = panel->property("pendingViewMode").toMap();
    if (!pending.isEmpty() && !applyingMode_) {
        panel->setProperty("pendingViewMode", QVariant());
        applyViewMode(pending);
    }
    for (auto *candidate : findChildren<FilePanel *>())
        candidate->setActive(candidate == panel);
    updateUiState();
    updateQuickView();
}

FilePanel *MainWindow::activePanel() const { return active_; }

void MainWindow::switchPanel() {
    auto *other = left_->indexOf(active_) >= 0 ? right_ : left_;
    auto *panel = qobject_cast<FilePanel *>(other->currentWidget());
    activate(panel);
    panel->focusFiles();
}

void MainWindow::executeCommandLine() {
    const QString text = commandLine_->text().trimmed();
    if (commands_.get(text) || text.startsWith("cm_", Qt::CaseInsensitive) ||
        text.startsWith("tc_", Qt::CaseInsensitive)) {
        auto *action = commands_.get(text);
        if (!action || !action->isEnabled()) {
            statusBar()->showMessage("Command is unavailable", 5000);
            return;
        }
        commandLine_->clear();
        action->trigger();
        return;
    }
    if (text == "cd")
        active_->navigate(nativePath(QDir::homePath()));
    else if (text.startsWith("cd "))
        active_->navigate(nativePath(text.mid(3).trimmed()));
    else {
        statusBar()->showMessage("Supported command: cd <path>. Program launching is not implemented.",
                                 10000);
        return;
    }
    if (!text.isEmpty() && commandHistory_->findText(text) < 0)
        commandHistory_->insertItem(0, text);
    commandLine_->clear();
    active_->focusFiles();
}

void MainWindow::restoreSession(const QStringList &initialPaths) {
    QSettings settings;
    for (int side = 0; side < 2; ++side) {
        auto *tabs = side == 0 ? left_ : right_;
        const QString key = side == 0 ? "leftPaths" : "rightPaths";
        const auto nativePaths = settings.value("session/directories", true).toBool()
                                     ? settings.value(key + "/native").toList()
                                     : QVariantList{};
        if (side < initialPaths.size())
            addTab(tabs, nativePath(initialPaths[side]));
        else if (!nativePaths.isEmpty()) {
            for (const auto &value : nativePaths)
                addTab(tabs, std::filesystem::path(value.toByteArray().toStdString()));
        } else {
            auto paths = settings.value("session/directories", true).toBool()
                             ? settings.value(key).toStringList()
                             : QStringList{};
            if (paths.isEmpty())
                paths = {QDir::homePath()};
            for (const auto &path : paths)
                addTab(tabs, nativePath(path));
        }
        tabs->setCurrentIndex(qBound(0, settings.value(key + "/current", 0).toInt(), tabs->count() - 1));
        if (settings.value("session/panels", true).toBool()) {
            const auto modes = settings.value(key + "/modes").toStringList();
            for (int i = 0; i < tabs->count() && i < modes.size(); ++i)
                qobject_cast<FilePanel *>(tabs->widget(i))->setDisplayMode(modes[i]);
        }
    }
    restoreGeometry(settings.value("geometry").toByteArray());
    splitter_->restoreState(settings.value("classic/splitter").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (queue_->busy()) {
        statusBar()->showMessage("Wait for the operation to finish, or cancel it, before closing", 10000);
        event->ignore();
        return;
    }
    for (auto *window : findChildren<FileContentDialog *>()) {
        if (!window->close()) {
            event->ignore();
            return;
        }
    }
    saveSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSession() {
    QSettings settings;
    for (auto *tabs : {left_, right_}) {
        QStringList paths;
        QVariantList nativePaths;
        for (int index = 0; index < tabs->count(); ++index) {
            const auto path = qobject_cast<FilePanel *>(tabs->widget(index))->path();
            if (!path.empty() && !InterfacePreferences::matches(path, "session/exclude")) {
                paths.push_back(displayPath(path));
                nativePaths.append(QByteArray::fromStdString(path.native()));
            }
        }
        const QString key = tabs == left_ ? "leftPaths" : "rightPaths";
        if (!settings.value("session/directories", true).toBool()) {
            paths.clear();
            nativePaths.clear();
        }
        settings.setValue(key, paths);
        settings.setValue(key + "/native", nativePaths);
        settings.setValue(key + "/current", tabs->currentIndex());
        QStringList modes;
        for (int i = 0; i < tabs->count(); ++i)
            modes << tabs->widget(i)->property("displayMode").toString();
        settings.setValue(key + "/modes",
                          settings.value("session/panels", true).toBool() ? modes : QStringList{});
    }
    settings.setValue("geometry", saveGeometry());
    settings.setValue("classic/splitter", splitter_->saveState());
    QStringList history;
    for (int i = 0; i < commandHistory_->count(); ++i)
        history << commandHistory_->itemText(i);
    settings.setValue("classic/commandHistory",
                      settings.value("session/commands", true).toBool() ? history.mid(0, 50) : QStringList{});
    if (settings.value("history/saveOnExit").toBool())
        for (auto *panel : findChildren<FilePanel *>())
            panel->storeHistory();
    if (settings.value("frequent/saveOnExit").toBool())
        PanelPreferences::saveFrequent();
}

FilePanel *MainWindow::oppositePanel() const {
    return qobject_cast<FilePanel *>((left_->indexOf(active_) >= 0 ? right_ : left_)->currentWidget());
}
void MainWindow::openCurrent(bool editable) {
    const auto paths = active_->selectedPaths();
    if (!paths.empty()) {
        try {
            bool external = editable || ApplicationPreferences::text("ViewerHotkeyCombo", "Configured viewer first") !=
                                            "Internal viewer";
            if (!editable && ApplicationPreferences::text("ViewerHotkeyCombo") == "Ask each time") {
                const auto answer =
                    QMessageBox::question(this, "Viewer", "Use the internal viewer?",
                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
                if (answer == QMessageBox::Cancel)
                    return;
                external = answer == QMessageBox::No;
            }
            if (external && NativeExtensions::launchAssociated(paths.front(), editable ? "editor" : "viewer"))
                return;
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "Open file", QString::fromLocal8Bit(error.what()));
            return;
        }
        (new FileContentDialog(paths.front(), editable, this))->show();
    }
}
void MainWindow::runOperation(LocalFiles::Operation operation) {
    using namespace LocalFiles;
    Request request;
    request.operation = operation;
    request.sources = active_->selectedPaths();
    if (operation == Operation::MakeDirectory || operation == Operation::MakeFile)
        request.sources.clear();
    request.destination = oppositePanel()->path();
    if (operation == Operation::MakeDirectory || operation == Operation::MakeFile ||
        operation == Operation::Rename) {
        if (operation == Operation::Rename && request.sources.size() != 1) {
            statusBar()->showMessage("Select one item to rename", 10000);
            return;
        }
        bool ok = false;
        const bool rename = operation == Operation::Rename;
        QInputDialog input(this);
        input.setWindowTitle(rename                             ? "Rename"
                             : operation == Operation::MakeFile ? "New text file"
                                                                : "New folder");
        input.setLabelText("Name (one path component):");
        input.setTextValue(rename ? displayPath(request.sources.front().filename()) : QString());
        if (rename && QSettings().value("rename/nameOnly").toBool())
            QTimer::singleShot(0, &input, [&input] {
                auto *edit = input.findChild<QLineEdit *>();
                const auto name = edit->text();
                const int dot = name.lastIndexOf('.');
                edit->setSelection(0, dot > 0 ? dot : name.size());
            });
        ok = input.exec() == QDialog::Accepted;
        const auto name = input.textValue();
        if (!ok)
            return;
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
            name.contains(QChar::Null)) {
            QMessageBox::warning(this, "Invalid name", "Enter a single non-empty filename.");
            return;
        }
        request.destination =
            (rename ? request.sources.front().parent_path() : active_->path()) / nativePath(name);
    } else {
        if (request.sources.empty())
            return;
        const bool transfer = operation == Operation::Copy || operation == Operation::Move;
        QString counted;
        if (ApplicationPreferences::flag("IdConf_CountfilesCopy") &&
            !DirectorySizes::confirmPreflight(request.sources, this, counted))
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(operation == Operation::Copy    ? "Copy to opposite panel"
                              : operation == Operation::Move  ? "Move to opposite panel"
                              : operation == Operation::Trash ? "Move to trash"
                                                              : "Delete permanently");
        auto *layout = new QVBoxLayout(&dialog);
        auto *summary = new QPlainTextEdit(&dialog);
        summary->setReadOnly(true);
        QStringList names;
        for (const auto &path : request.sources)
            names << displayPath(path);
        summary->setPlainText(names.join('\n'));
        layout->addWidget(
            new QLabel(QString("%1 selected item(s), including directory contents").arg(names.size()) +
                           (counted.isEmpty() ? QString() : "\n" + counted),
                       &dialog));
        layout->addWidget(summary);
        auto *destination = new QLineEdit(displayPath(request.destination), &dialog);
        auto *conflict = new QComboBox(&dialog);
        conflict->addItems({"Report existing destinations as errors", "Skip existing destinations"});
        if (operation == Operation::Copy)
            conflict->addItem("Replace existing regular files");
        if (transfer) {
            auto *form = new QFormLayout;
            destination->setObjectName("operationDestination");
            form->addRow("Destination directory:", destination);
            form->addRow("When names exist:", conflict);
            layout->addLayout(form);
        }
        if (operation == Operation::Move)
            layout->addWidget(new QLabel(
                "Moves across filesystems are refused; copy first and review before deleting.", &dialog));
        if (operation == Operation::Delete)
            layout->addWidget(new QLabel("Permanent deletion cannot be undone. Cancel stops remaining work; "
                                         "deleted entries stay deleted.",
                                         &dialog));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)
            ->setText(transfer                        ? "Start"
                      : operation == Operation::Trash ? "Move to trash"
                                                      : "Delete permanently");
        buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.resize(660, 360);
        if (dialog.exec() != QDialog::Accepted) {
            QTimer::singleShot(0, this, [this] {
                activateWindow();
                active_->focusFiles();
            });
            return;
        }
        if (transfer) {
            // Preserve native bytes when the displayed destination was not edited.
            if (destination->text() != displayPath(request.destination))
                request.destination = nativePath(destination->text());
            if (!request.destination.is_absolute())
                request.destination = active_->path() / request.destination;
            request.conflict = static_cast<Conflict>(conflict->currentIndex());
        }
    }
    request.journalDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/jobs";
    bool special = false;
    for (const auto &p : request.sources) {
        std::error_code error;
        const auto st = std::filesystem::symlink_status(p, error);
        if (error)
            continue;
        const bool readonly =
            (st.permissions() & (std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                 std::filesystem::perms::others_write)) == std::filesystem::perms::none;
        special |=
            (ApplicationPreferences::flag("confirm_readonly") && readonly && request.operation != Operation::Copy) ||
            (ApplicationPreferences::flag("confirm_hidsys") && displayPath(p.filename()).startsWith('.') &&
             request.operation != Operation::Copy) ||
            (ApplicationPreferences::flag("confirm_dirs") && std::filesystem::is_directory(st) &&
             (request.operation == Operation::Delete));
    }
    if (request.conflict == Conflict::ReplaceFiles && ApplicationPreferences::flag("confirm_overwrite"))
        for (const auto &p : request.sources)
            special |= std::filesystem::exists(request.destination / p.filename());
    if (special &&
        QMessageBox::question(this, "Confirm selected entries",
                              "Proceed with the selected overwrite, protected-file or directory operation?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    startOperation(request);
}
void MainWindow::startOperation(const LocalFiles::Request &request) {
    QPointer<FilePanel> source(active_);
    QString verb;
    switch (request.operation) {
    case LocalFiles::Operation::Copy:
        verb = "Copy";
        break;
    case LocalFiles::Operation::Move:
        verb = "Move";
        break;
    case LocalFiles::Operation::Rename:
        verb = "Rename";
        break;
    case LocalFiles::Operation::Trash:
        verb = "Move to trash";
        break;
    case LocalFiles::Operation::Delete:
        verb = "Delete permanently";
        break;
    case LocalFiles::Operation::MakeDirectory:
        verb = "Create folder";
        break;
    case LocalFiles::Operation::MakeFile:
        verb = "Create file";
        break;
    case LocalFiles::Operation::SetPermissions:
        verb = "Change permissions";
        break;
    case LocalFiles::Operation::SymbolicLink:
        verb = "Create symbolic link";
        break;
    }
    const auto label =
        verb + (request.destination.empty() ? QString(" · %1 entries").arg(request.sources.size())
                                            : " → " + displayPath(request.destination));
    queue_->enqueue(
        label, [request](const auto &control) { return LocalFiles::execute(request, control); },
        [this, source, request](const LocalFiles::Result &result) {
            if (source && active_ == source && !QApplication::activeModalWidget())
                source->focusFiles();
            if (request.operation == LocalFiles::Operation::MakeFile && !result.items.empty() &&
                result.items.front().error.isEmpty()) {
                if (source)
                    source->reveal(request.destination);
                (new FileContentDialog(request.destination, true, this))->show();
            }
            int errors = 0, skipped = 0, completed = 0;
            QStringList details;
            for (const auto &item : result.items) {
                if (!item.error.isEmpty())
                    ++errors;
                else if (item.skipped)
                    ++skipped;
                else
                    ++completed;
                details << QString("%1: %2%3%4")
                               .arg(item.error.isEmpty() ? item.skipped ? "SKIPPED" : "DONE" : "ERROR",
                                    displayPath(item.source),
                                    item.destination.empty() ? "" : " → " + displayPath(item.destination),
                                    item.error.isEmpty() ? "" : "\n  " + item.error);
            }
            const auto summary = QString("%1 completed · %2 skipped · %3 errors%4")
                                     .arg(completed)
                                     .arg(skipped)
                                     .arg(errors)
                                     .arg(result.cancelled ? " · cancelled" : "");
            statusBar()->showMessage(summary, 20000);
            if (errors || skipped || result.cancelled || !result.journalError.isEmpty()) {
                auto *report = new QMessageBox(
                    errors ? QMessageBox::Warning : QMessageBox::Information, "Operation results",
                    summary + (result.journalError.isEmpty() ? "" : "\n" + result.journalError),
                    QMessageBox::Ok, this);
                report->setAttribute(Qt::WA_DeleteOnClose);
                report->setDetailedText(details.join('\n'));
                report->show();
            }
        });
    statusBar()->showMessage("Operation queued · Ctrl+J opens background operations", 10000);
}
void MainWindow::updateBookmarks() {
    if (!bookmarks_ || !active_)
        return;
    bookmarks_->clear();
    const auto values = QSettings().value("bookmarksNative").toList();
    for (const auto &value : values) {
        const auto path = std::filesystem::path(value.toByteArray().toStdString());
        auto *action = bookmarks_->addAction(displayPath(path));
        connect(action, &QAction::triggered, this, [this, path] {
            active_->navigate(path);
            active_->focusFiles();
        });
    }
    if (!values.empty())
        bookmarks_->addSeparator();
    const auto current = QByteArray::fromStdString(active_->path().native());
    if (values.contains(current))
        bookmarks_->addAction("&Remove current dir", this, [this, current] {
            QSettings settings;
            auto paths = settings.value("bookmarksNative").toList();
            paths.removeAll(current);
            settings.setValue("bookmarksNative", paths);
        });
    else
        bookmarks_->addAction(commands_.get("cm_AddBookmark"));
    auto *configure = bookmarks_->addAction("&Configure...");
    configure->setObjectName("configureBookmarks");
    connect(configure, &QAction::triggered, this, [this] {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("bookmarkEditor");
        dialog->setWindowTitle("Directory hotlist");
        auto *layout = new QVBoxLayout(dialog);
        auto *list = new QListWidget(dialog);
        list->setObjectName("bookmarkList");
        layout->addWidget(list);
        for (const auto &value : QSettings().value("bookmarksNative").toList()) {
            auto *item = new QListWidgetItem(
                displayPath(std::filesystem::path(value.toByteArray().toStdString())), list);
            item->setData(Qt::UserRole, value);
        }
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        auto *add = new QPushButton("Add directory…", dialog), *remove = new QPushButton("Remove", dialog),
             *up = new QPushButton("Up", dialog), *down = new QPushButton("Down", dialog);
        for (auto *button : {add, remove, up, down})
            row->addWidget(button);
        connect(add, &QPushButton::clicked, dialog, [dialog, list] {
            const auto path = QFileDialog::getExistingDirectory(dialog, "Add directory");
            if (path.isEmpty())
                return;
            const auto bytes = QFile::encodeName(path);
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toByteArray() == bytes)
                    return;
            auto *item = new QListWidgetItem(path, list);
            item->setData(Qt::UserRole, bytes);
            list->setCurrentItem(item);
        });
        connect(remove, &QPushButton::clicked, dialog, [list] { delete list->takeItem(list->currentRow()); });
        for (const auto &pair : {std::pair{up, -1}, std::pair{down, 1}})
            connect(pair.first, &QPushButton::clicked, dialog, [list, offset = pair.second] {
                const int row = list->currentRow(), target = row + offset;
                if (row < 0 || target < 0 || target >= list->count())
                    return;
                auto *item = list->takeItem(row);
                list->insertItem(target, item);
                list->setCurrentItem(item);
            });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, list] {
            QVariantList paths;
            for (int i = 0; i < list->count(); ++i)
                paths << list->item(i)->data(Qt::UserRole);
            QSettings().setValue("bookmarksNative", paths);
            dialog->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        dialog->resize(520, 340);
        dialog->show();
    });
}

void MainWindow::showProperties() {
    const auto paths = active_->selectedPaths();
    if (paths.empty())
        return;
    const auto path = paths.front();
    auto *dialog =
        new QMessageBox(QMessageBox::Information, "Properties", "Reading metadata…", QMessageBox::Ok, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *watcher = new QFutureWatcher<QString>(dialog);
    connect(watcher, &QFutureWatcher<QString>::finished, dialog, [watcher, dialog] {
        dialog->setText(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [path] {
        struct stat st {};
        if (::lstat(path.c_str(), &st))
            return QString::fromLocal8Bit(std::strerror(errno));
        QString target;
        if (S_ISLNK(st.st_mode)) {
            std::error_code error;
            const auto link = std::filesystem::read_symlink(path, error);
            target = "\nTarget: " + (error ? QString::fromStdString(error.message()) : displayPath(link));
        }
        return QString("%1\n\nType: %2\nSize: %3 bytes\nModified: %4\nPermissions (octal): %5\nOwner UID: "
                       "%6\nGroup GID: %7%8")
            .arg(displayPath(path), S_ISLNK(st.st_mode)   ? "Symbolic link"
                                    : S_ISDIR(st.st_mode) ? "Directory"
                                                          : "File")
            .arg(st.st_size)
            .arg(QDateTime::fromSecsSinceEpoch(st.st_mtime).toString(Qt::ISODate))
            .arg(st.st_mode & 07777, 0, 8)
            .arg(st.st_uid)
            .arg(st.st_gid)
            .arg(target);
    }));
    dialog->show();
}

void MainWindow::showChecksums() {
    const auto paths = active_->selectedPaths();
    if (paths.empty())
        return;
    auto control = std::make_shared<LocalFiles::Control>();
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("SHA-256 checksums");
    dialog->resize(900, 420);
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QPlainTextEdit("Calculating…", dialog);
    text->setReadOnly(true);
    layout->addWidget(text);
    auto *stop = new QPushButton("Stop", dialog);
    layout->addWidget(stop);
    connect(stop, &QPushButton::clicked, dialog, [control] { control->cancelled = true; });
    connect(dialog, &QObject::destroyed, this, [control] { control->cancelled = true; });
    auto *watcher = new QFutureWatcher<QString>(dialog);
    connect(watcher, &QFutureWatcher<QString>::finished, dialog, [watcher, text, stop] {
        text->setPlainText(watcher->result());
        stop->setEnabled(false);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [paths, control] {
        QStringList lines;
        for (const auto &path : paths) {
            if (control->cancelled)
                break;
            const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
            struct stat before {};
            if (fd < 0 || ::fstat(fd, &before) || !S_ISREG(before.st_mode)) {
                if (fd >= 0)
                    ::close(fd);
                lines << "ERROR (requires a readable regular file): " + displayPath(path);
                continue;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            std::array<char, 256 * 1024> buffer{};
            bool error = false;
            while (!control->cancelled) {
                const auto count = ::read(fd, buffer.data(), buffer.size());
                if (count < 0 && errno == EINTR)
                    continue;
                if (count < 0) {
                    error = true;
                    break;
                }
                if (!count)
                    break;
                hash.addData(QByteArrayView(buffer.data(), count));
            }
            struct stat after {
            }, named{};
            if (::lstat(path.c_str(), &named) || !S_ISREG(named.st_mode) || named.st_dev != before.st_dev ||
                named.st_ino != before.st_ino || ::fstat(fd, &after) || before.st_size != after.st_size ||
                before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
                before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
                before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
                error = true;
            ::close(fd);
            if (control->cancelled)
                break;
            lines << (error ? "ERROR (read failure or file changed)"
                            : QString::fromLatin1(hash.result().toHex())) +
                         "  " + displayPath(path);
        }
        if (control->cancelled)
            lines << "Cancelled; unprocessed files have no checksum.";
        return lines.join('\n');
    }));
    dialog->show();
}

QString MainWindow::journalDirectory() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/jobs";
}
void MainWindow::showQueue() {
    if (!queueDialog_)
        queueDialog_ = new JobQueueDialog(queue_, this);
    queueDialog_->show();
    queueDialog_->raise();
    queueDialog_->activateWindow();
}
void MainWindow::openArchive() {
    const auto paths = active_->selectedPaths();
    if (!paths.empty())
        (new ArchiveBrowserDialog(paths.front(), oppositePanel()->path(), queue_, this))->show();
}
void MainWindow::packArchive() {
    const auto sources = active_->selectedPaths();
    if (sources.empty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Create archive");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto destination = oppositePanel()->path() / (sources.front().filename().native() + ".zip");
    auto *path = new QLineEdit(displayPath(destination), &dialog);
    path->setObjectName("archiveDestination");
    auto *format = new QComboBox(&dialog);
    format->addItems({"ZIP", "TAR + gzip", "TAR"});
    format->addItem("7-Zip", QString("native7z"));
    for (const auto &key :
         {QString("zip_packer"), QString("z7_packer"), QString("rar_packer"), QString("arj_packer"),
          QString("lha_packer"), QString("uc2_packer"), QString("ace_packer")})
        if (!QSettings().value("tools/" + key).toString().isEmpty())
            format->addItem(key.section('_', 0, 0).toUpper() + " (external)", key);
    for (const auto &entry : NativeExtensions::plugins("packer"))
        format->addItem(entry.toMap().value("name").toString() + " (extension)", entry);
    form->addRow("New archive path:", path);
    form->addRow("Format:", format);
    layout->addLayout(form);
    path->setProperty("archiveSuffix", ".zip");
    connect(format, &QComboBox::currentIndexChanged, &dialog, [path, format](int index) {
        auto text = path->text();
        const auto previous = path->property("archiveSuffix").toString();
        if (!previous.isEmpty() && text.endsWith(previous))
            text.chop(previous.size());
        QString suffix = index == 0 ? ".zip" : index == 1 ? ".tar.gz" : index == 2 ? ".tar" : QString();
        if (index >= 3) {
            const auto provider = format->itemData(index);
            if (provider.metaType().id() == QMetaType::QVariantMap) {
                const auto mask = provider.toMap().value("mask").toString().section(';', 0, 0);
                suffix = QRegularExpression("^\\*\\.[A-Za-z0-9.]+$").match(mask).hasMatch() ? mask.mid(1)
                                                                                            : ".archive";
            } else
                suffix = provider.toString() == "native7z"
                             ? ".7z"
                             : "." + provider.toString().section('_', 0, 0).replace("z7", "7z");
        }
        path->setText(text + suffix);
        path->setProperty("archiveSuffix", suffix);
    });
    auto *summary = new QPlainTextEdit(&dialog);
    summary->setReadOnly(true);
    QStringList names;
    for (const auto &source : sources)
        names << displayPath(source);
    summary->setPlainText(names.join('\n'));
    layout->addWidget(summary);
    layout->addWidget(new QLabel("Existing archives are retained. Links and special files are refused. "
                                 "Maximum expanded data: 4 GiB; one file: 512 MiB.",
                                 &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(800, 420);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (path->text() != displayPath(destination))
        destination = nativePath(path->text());
    if (!destination.is_absolute())
        destination = active_->path() / destination;
    if (format->currentData().toString() == "native7z" ||
        (format->currentIndex() == 0 &&
         (!ApplicationPreferences::flag("zip_internalpack") || ApplicationPreferences::flag("zip_zipnt") ||
          ApplicationPreferences::flag("zip_use7z")))) {
        const bool zip = format->currentIndex() == 0;
        auto password = std::make_shared<QByteArray>();
        if (!zip && ApplicationPreferences::flag("z7_encryptnames")) {
            bool ok = false;
            QString secret = QInputDialog::getText(this, "Encrypted 7-Zip",
                                                   "Password (not saved):", QLineEdit::Password, {}, &ok);
            if (!ok || secret.isEmpty())
                return;
            *password = secret.toUtf8();
            secret.fill(QChar::Null);
        }
        queue_->enqueue("Create native archive " + displayPath(destination.filename()),
                        [sources, destination, zip, password](const auto &control) {
                            auto result = ExternalArchiveBackend::pack(sources, destination, zip, *password, control);
                            password->fill('\0');
                            return result;
                        });
        return;
    }
    if (format->currentIndex() >= 3) {
        const auto provider = format->currentData();
        QString program;
        QStringList arguments;
        if (provider.metaType().id() == QMetaType::QVariantMap) {
            const auto record = provider.toMap();
            program = record.value("program").toString();
            arguments = QProcess::splitCommand(record.value("arguments").toString());
        } else {
            const auto key = provider.toString();
            program = QSettings().value("tools/" + key).toString();
            arguments = key == "zip_packer" ? QStringList{"-r"} : QStringList{"a"};
        }
        queue_->enqueue("External archive " + displayPath(destination.filename()),
                        [sources, destination, program, arguments](const auto &control) {
                            return FileTransforms::packExternal(sources, destination, program, arguments,
                                                               control);
                        });
        return;
    }
    const auto kind = static_cast<Archives::Format>(format->currentIndex());
    queue_->enqueue("Create archive " + displayPath(destination.filename()),
                    [sources, destination, kind](const auto &control) {
                        return Archives::create(sources, destination, kind, control);
                    });
    statusBar()->showMessage("Archive creation queued · Ctrl+J for results", 10000);
}
void MainWindow::showStatistics() {
    const auto paths = active_->selectedPaths();
    if (paths.empty())
        return;
    (new TaskReportDialog(
         "Selected folder statistics",
         [paths](const auto &control) {
             const auto result = FileAnalysis::statistics(paths, control);
             return QString("%1 files\n%2 directories (including selected roots)\n%3 symbolic links (not "
                            "followed)\n\nLogical file bytes: %4\nAllocated file bytes: %5\n\n%6%7\n%8 "
                            "errors\n%9")
                 .arg(result.files)
                 .arg(result.directories)
                 .arg(result.links)
                 .arg(result.bytes)
                 .arg(result.allocated)
                 .arg(result.cancelled ? "Cancelled; counts are partial. " : "")
                 .arg(result.truncated ? "Scan limit reached; counts are partial." : "")
                 .arg(result.errors.size())
                 .arg(result.errors.join('\n'));
         },
         this))
        ->show();
}
void MainWindow::findDuplicates() {
    const auto root = active_->path();
    (new TaskReportDialog(
         "Duplicate candidates — " + displayPath(root),
         [root](const auto &control) {
             const auto result = FileAnalysis::duplicates(root, control);
             QStringList lines{"Candidates grouped by size and SHA-256. Hard-linked aliases and symlinks are "
                               "excluded. No files are deleted."};
             size_t shown = 0;
             for (const auto &group : result.groups) {
                 lines << QString("\n%1 bytes · SHA-256 %2")
                              .arg(group.size)
                              .arg(QString::fromLatin1(group.sha256.toHex()));
                 for (const auto &path : group.paths) {
                     if (++shown > 10000)
                         break;
                     lines << displayPath(path);
                 }
                 if (shown > 10000) {
                     lines << "Display limited to 10000 paths";
                     break;
                 }
             }
             lines << QString("\n%1 groups%2%3 · %4 errors")
                          .arg(result.groups.size())
                          .arg(result.cancelled ? " · cancelled" : "")
                          .arg(result.truncated ? " · truncated" : "")
                          .arg(result.errors.size());
             lines << result.errors;
             return lines.join('\n');
         },
         this))
        ->show();
}
void MainWindow::compareCurrentFiles() {
    const auto left = active_->selectedPaths(), right = oppositePanel()->selectedPaths();
    if (left.empty() || right.empty())
        return;
    const auto a = left.front(), b = right.front();
    (new TaskReportDialog(
         "Compare files by content",
         [a, b](const auto &control) {
             const auto result = FileAnalysis::compareFiles(a, b, control);
             return displayPath(a) + "\n" + displayPath(b) + "\n\n" +
                    (!result.error.isEmpty() ? result.error
                     : result.cancelled      ? "Cancelled"
                     : result.equal          ? "Files have identical bytes."
                                             : QString("Files differ. First different byte offset: %1 (0x%2)")
                                          .arg(result.offset)
                                          .arg(result.offset, 0, 16));
         },
         this))
        ->show();
}
void MainWindow::editPermissions() {
    const auto paths = active_->selectedPaths();
    if (paths.empty())
        return;
    bool ok = false;
    const auto text = QInputDialog::getText(
        this, "Change permissions",
        "Octal mode 000–777. Applies only to selected entries, without recursion; links are refused:",
        QLineEdit::Normal, "644", &ok);
    if (!ok)
        return;
    const auto mode = text.toUInt(&ok, 8);
    if (!ok || mode > 0777 || text.isEmpty()) {
        QMessageBox::warning(this, "Invalid permissions", "Enter an octal mode between 000 and 777.");
        return;
    }
    LocalFiles::Request request{LocalFiles::Operation::SetPermissions,
                                paths,
                                {},
                                LocalFiles::Conflict::Fail,
                                journalDirectory(),
                                mode};
    startOperation(request);
}
void MainWindow::createLink() {
    const auto paths = active_->selectedPaths();
    if (paths.size() != 1) {
        statusBar()->showMessage("Select one entry to link", 10000);
        return;
    }
    auto destination = oppositePanel()->path() / (paths.front().filename().native() + ".link");
    bool ok = false;
    const auto text = QInputDialog::getText(this, "Create symbolic link",
                                            "New link path (target is the selected absolute native path):",
                                            QLineEdit::Normal, displayPath(destination), &ok);
    if (!ok)
        return;
    if (text != displayPath(destination))
        destination = nativePath(text);
    if (!destination.is_absolute())
        destination = oppositePanel()->path() / destination;
    startOperation({LocalFiles::Operation::SymbolicLink, paths, destination, LocalFiles::Conflict::Fail,
                    journalDirectory()});
}
void MainWindow::showHistory() {
    const auto directory = journalDirectory();
    (new TaskReportDialog(
         "Previous job reports",
         [directory](const auto &control) {
             const auto reports = QDir(directory).entryInfoList({"*.json"}, QDir::Files, QDir::Time);
             QStringList lines{"Diagnostic reports only. Interrupted jobs are not automatically retried. "
                               "Inspect named temporary entries before manual recovery."};
             int count = 0;
             const auto pathText = [](const QJsonValue &value) {
                 return displayPath(std::filesystem::path(
                     QByteArray::fromBase64(value.toString().toLatin1()).toStdString()));
             };
             for (const auto &report : reports) {
                 LocalFiles::checkpoint(control);
                 if (++count > 100) {
                     lines << "Latest 100 reports shown";
                     break;
                 }
                 QFile file(report.absoluteFilePath());
                 if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
                     lines << "Unreadable or oversized report: " + report.fileName();
                     continue;
                 }
                 QJsonParseError error;
                 const auto data = QJsonDocument::fromJson(file.readAll(), &error).object();
                 if (error.error != QJsonParseError::NoError) {
                     lines << "Invalid report: " + report.fileName();
                     continue;
                 }
                 lines << "\n" + report.fileName() + " · " +
                              (data.value("finished").toBool() ? "Finished" : "INTERRUPTED / not finalized");
                 lines << data.value("label").toString() + " " + data.value("state").toString();
                 lines << data.value("error").toString();
                 const auto entries = data.value("items").toArray();
                 int shown = 0;
                 for (const auto &value : entries) {
                     if (++shown > 1000) {
                         lines << "Further outcomes omitted";
                         break;
                     }
                     const auto item = value.toObject();
                     lines << pathText(item.value("sourceBase64")) + " → " +
                                  pathText(item.value("destinationBase64"));
                     if (item.contains("temporaryBase64"))
                         lines << "Temporary: " + pathText(item.value("temporaryBase64")) + " · phase " +
                                      QString::number(item.value("phase").toInt());
                     if (!item.value("error").toString().isEmpty())
                         lines << item.value("error").toString();
                 }
                 const auto eventName = data.value("outcomeLog").toString();
                 if (!data.value("finished").toBool() && !eventName.isEmpty() && !eventName.contains('/') &&
                     !eventName.contains('\\')) {
                     QFile events(QDir(directory).filePath(eventName));
                     if (events.open(QIODevice::ReadOnly)) {
                         lines << "Incremental outcomes / rename phases (inspect actual entries before "
                                  "recovery):";
                         int records = 0;
                         while (!events.atEnd() && records++ < 1000) {
                             LocalFiles::checkpoint(control);
                             const auto record = QJsonDocument::fromJson(events.readLine(64 * 1024)).object();
                             lines << pathText(record.value("sourceBase64")) + " → " +
                                          pathText(record.value("destinationBase64"));
                             if (record.contains("phase"))
                                 lines << "Temporary: " + pathText(record.value("temporaryBase64")) +
                                              " · phase " + QString::number(record.value("phase").toInt());
                             if (!record.value("error").toString().isEmpty())
                                 lines << record.value("error").toString();
                         }
                         if (!events.atEnd())
                             lines << "Further incremental records omitted";
                     }
                 }
             }
             if (reports.empty())
                 lines << "No previous reports.";
             return lines.join('\n');
         },
         this))
        ->show();
}

void MainWindow::showSearchResults(const std::vector<std::filesystem::path> &paths) {
    if (paths.empty())
        return;
    auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
    const auto root = active_->path();
    addTab(tabs, root);
    auto *panel = qobject_cast<FilePanel *>(tabs->currentWidget());
    panel->model()->setResultPaths(&paths);
    panel->model()->load(root);
    tabs->setTabText(tabs->currentIndex(), "Search results");
}

MainWindow::~MainWindow() {
    for (auto *child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    if (remote_) {
        delete remote_.data();
        remote_.clear();
    }
    if (share_) {
        share_->close();
        delete share_;
        share_ = nullptr;
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (object->property("deleteDropTarget").toBool()) {
        if (event->type() == QEvent::DragEnter) {
            auto *drag = static_cast<QDragEnterEvent *>(event);
            if (drag->mimeData()->hasFormat(NativePathMime::mimeType())) {
                drag->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            const auto paths = NativePathMime::decode(drop->mimeData());
            if (paths.empty())
                return true;
            const bool trash = ApplicationPreferences::flag("IdConf_trashcan");
            if ((ApplicationPreferences::flag("confirm_mouse_delete") || !trash) &&
                QMessageBox::question(this, "Dropped deletion",
                                      QString("%1 %2 selected entries?")
                                          .arg(trash ? "Trash" : "Permanently delete")
                                          .arg(paths.size()),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes)
                return true;
            LocalFiles::Request request;
            request.sources = paths;
            request.operation = trash ? LocalFiles::Operation::Trash : LocalFiles::Operation::Delete;
            startOperation(request);
            drop->acceptProposedAction();
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::invokeParameterized(QAction *action, const QString &parameters) {
    if (!action || !action->isEnabled())
        return;
    if (parameters.trimmed().isEmpty()) {
        action->trigger();
        return;
    }
    const auto name = action->objectName();
    if (name == "cm_OpenNewTab") {
        addTab(left_->indexOf(active_) >= 0 ? left_ : right_, nativePath(parameters));
        return;
    }
    if (name == "cm_SrcUserDef" || name == "cm_SrcAllFiles") {
        active_->filterView(parameters);
        return;
    }
    if (name == "cm_GoHome") {
        active_->navigate(nativePath(parameters));
        return;
    }
    QMessageBox::information(this, "Command parameters",
                             "Parameters are supported for New tab (directory), Home (directory), and "
                             "custom/all file view (mask). Other commands require an empty parameter field.");
}
