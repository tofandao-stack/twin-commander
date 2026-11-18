#include "advancedwindows.h"
#include "backgroundpool.h"
#include "classicui.h"
#include "extendedtools.h"
#include "mainwindow.h"
#include "nativesettings.h"
#include "search.h"
#include "toolwindows.h"
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <memory>

void MainWindow::registerExtendedCommands() {
    const auto add = [this](int id, const QString &name, std::function<void()> callback) {
        return commands_.add("tc_" + QString::number(id), name, {}, std::move(callback));
    };
    add(560, "Split file", [this] {
        const auto paths = active_->selectedPaths();
        if (paths.size() != 1)
            return;
        bool ok = false;
        const int kib = QInputDialog::getInt(this, "Split file", "Piece size (KiB):", 1440, 1, 65536, 1, &ok);
        if (!ok)
            return;
        const auto destination = QFileDialog::getSaveFileName(
            this, "New directory for split parts",
            displayPath(oppositePanel()->path() / paths[0].filename()) + ".parts");
        if (destination.isEmpty())
            return;
        (new ReportDialog(
             "Split file",
             [source = paths[0], destination, kib](const auto &control) {
                 ExtendedTools::split(source, nativePath(destination), static_cast<quint64>(kib) * 1024,
                                      control);
                 return "Parts and checksum manifest saved to " + destination;
             },
             this))
            ->show();
    });
    add(561, "Combine files", [this] {
        auto pieces = active_->selectedPaths();
        if (pieces.empty())
            return;
        std::sort(pieces.begin(), pieces.end());
        const auto output = QFileDialog::getSaveFileName(
            this, "New combined file", displayPath(oppositePanel()->path() / "combined.bin"));
        if (output.isEmpty())
            return;
        (new ReportDialog(
             "Combine files",
             [pieces, output](const auto &control) {
                 ExtendedTools::combine(pieces, nativePath(output), control);
                 return "Combined file saved to " + output;
             },
             this))
            ->show();
    });
    for (const bool decoding : {false, true})
        add(decoding ? 563 : 562, decoding ? "Decode file" : "Encode file", [this, decoding] {
            const auto paths = active_->selectedPaths();
            if (paths.size() != 1)
                return;
            bool ok = false;
            auto formats = QStringList{"MIME", "UUE", "XXE"};
            if (decoding)
                formats << "BinHex";
            const auto format = QInputDialog::getItem(this, decoding ? "Decode file" : "Encode file",
                                                      "Format:", formats, 0, false, &ok);
            if (!ok)
                return;
            const auto output =
                QFileDialog::getSaveFileName(this, "New output file",
                                             displayPath(oppositePanel()->path() / paths[0].filename()) +
                                                 (decoding ? ".decoded" : "." + format.toLower()));
            if (output.isEmpty())
                return;
            (new ReportDialog(
                 decoding ? "Decode file" : "Encode file",
                 [path = paths[0], format, output, decoding](const auto &control) {
                     LocalFiles::checkpoint(control);
                     const auto bytes = Productivity::readSmall(path, 64 * 1024 * 1024);
                     const auto result =
                         decoding ? ExtendedTools::decode(bytes, format)
                                  : ExtendedTools::encode(
                                        bytes, format, QByteArray::fromStdString(path.filename().native()));
                     LocalFiles::checkpoint(control);
                     Productivity::exportNew(nativePath(output), result);
                     return "Saved " + output;
                 },
                 this))
                ->show();
        });
    add(2700, "Edit comment", [this] {
        const auto paths = active_->selectedPaths();
        if (paths.empty())
            return;
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(
            this, "Edit comment",
            "Comment stored in the file's Linux user extended attribute:", ExtendedTools::comment(paths[0]),
            &ok);
        if (!ok)
            return;
        try {
            for (const auto &path : paths)
                ExtendedTools::setComment(path, text);
            active_->refresh();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Comment", QString::fromLocal8Bit(e.what()));
        }
    });
    add(519, "Internal associations", [this] { showAssociations(); });
    add(507, "Associate with", [this] {
        const auto paths = active_->selectedPaths();
        if (paths.empty())
            return;
        const auto desktop = QFileDialog::getOpenFileName(
            this, "Choose installed desktop application",
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
            "Desktop applications (*.desktop)");
        if (desktop.isEmpty())
            return;
        const auto mime = QMimeDatabase().mimeTypeForFile(displayPath(paths[0])).name();
        auto *process = new QProcess(this);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int code, QProcess::ExitStatus status) {
                    if (code || status != QProcess::NormalExit)
                        QMessageBox::warning(this, "Association",
                                             QString::fromUtf8(process->readAllStandardError()));
                    process->deleteLater();
                });
        process->start("xdg-mime", {"default", QFileInfo(desktop).fileName(), mime});
        connect(process, &QProcess::errorOccurred, this, [this, process] {
            QMessageBox::warning(this, "Association", process->errorString());
            process->deleteLater();
        });
    });
    add(300, "Comments", [this] {
        active_->model()->setColumns({"comment"});
        active_->setDisplayMode("full");
        active_->setProperty("displayMode", "comments");
        active_->refresh();
        updateUiState();
    });
    add(270, "Custom columns", [this] {
        const auto configurations = QSettings().value("customColumns").toList();
        if (configurations.isEmpty()) {
            commands_.get("cm_Config")->trigger();
            findChild<QDialog *>("referenceConfiguration")
                ->findChild<QListWidget *>("configurationCategories")
                ->setCurrentRow(7);
            updateUiState();
            return;
        }
        QStringList names;
        for (const auto &value : configurations)
            names << value.toMap().value("name").toString();
        bool ok = false;
        const auto name =
            QInputDialog::getItem(this, "Custom columns", "Configuration:", names, 0, false, &ok);
        if (ok) {
            const auto fields = configurations[names.indexOf(name)].toMap().value("fields").toString().split(
                ';', Qt::SkipEmptyParts);
            active_->model()->setColumns(fields);
            active_->setDisplayMode("full");
            active_->setProperty("displayMode", "custom");
            active_->refresh();
        }
    });
    add(303, "Tree", [this] {
        active_->setDisplayMode("tree");
        updateUiState();
    });
    add(269, "Thumbnails", [this] {
        active_->setDisplayMode("thumbnails");
        updateUiState();
    });
    add(2026, "Branch view", [this] {
        QPointer<FilePanel> panel(active_);
        const auto root = active_->path();
        auto control = std::make_shared<LocalFiles::Control>();
        auto *watcher = new QFutureWatcher<SearchResult>(this);
        connect(watcher, &QFutureWatcher<SearchResult>::finished, this, [this, panel, watcher] {
            auto result = watcher->result();
            watcher->deleteLater();
            if (!panel)
                return;
            if (result.cancelled || result.truncated || !result.errors.isEmpty()) {
                QMessageBox::warning(
                    this, "Branch view",
                    "The recursive listing was incomplete; refine the directory or use Find Files.");
                return;
            }
            activate(panel);
            showSearchResults(result.paths);
        });
        SearchRequest request;
        request.root = root;
        request.hidden = active_->model()->showHidden();
        request.limit = 100000;
        watcher->setFuture(QtConcurrent::run(ioPool(), searchFiles, request, control));
        connect(this, &QObject::destroyed, watcher, [control] { control->cancelled = true; });
    });
    for (int count = 0; count < 3; ++count)
        add(3200 + count, "Separate tree", [this, count] { setSeparateTrees(count); });
    commands_.add("cm_SwitchSeparateTree", "Separate tree", {}, [this] {
        const int current =
            treeLeft_ && treeLeft_->isVisible() ? (treeRight_ && treeRight_->isVisible() ? 2 : 1) : 0;
        setSeparateTrees((current + 1) % 3);
    });
    add(304, "Quick view", [this] {
        if (!quickView_) {
            quickView_ = new QDockWidget("Quick view", this);
            quickView_->setObjectName("quickViewDock");
            addDockWidget(Qt::RightDockWidgetArea, quickView_);
            quickView_->hide();
            connect(quickView_, &QDockWidget::visibilityChanged, this,
                    [this](bool visible) { commands_.get("tc_304")->setChecked(visible); });
        }
        quickView_->setVisible(!quickView_->isVisible());
        updateQuickView();
    });
    add(700, "Change Start menu", [this] {
        NativeSettings::editRecords(this, "Start menu — internal commands", "startMenu",
                                    {"title", "command", "shortcut"});
        rebuildStartMenu();
    });
    add(630, "Registration information", [this] {
        QMessageBox::information(
            this, "Registration information",
            "Twin Commander is an independently implemented Linux application. No Total Commander "
            "registration or license key is used by this application.\n\nTotal Commander registration "
            "information: https://www.ghisler.com/order.htm");
    });
    add(650, "Check for updates", [this] {
        bool ok = false;
        const auto address = QInputDialog::getText(
            this, "Update feed", "JSON release feed URL (version and url fields):", QLineEdit::Normal,
            QSettings().value("updates/url").toString(), &ok);
        if (!ok || address.isEmpty())
            return;
        const QUrl url(address);
        if (url.scheme() != "https" && !(url.scheme() == "http" && url.host() == "127.0.0.1")) {
            QMessageBox::warning(this, "Updates", "Use an HTTPS release feed.");
            return;
        }
        QSettings().setValue("updates/url", address);
        auto *manager = new QNetworkAccessManager(this);
        QNetworkRequest request(url);
        request.setTransferTimeout(10000);
        auto *reply = manager->get(request);
        auto payload = std::make_shared<QByteArray>();
        connect(reply, &QNetworkReply::readyRead, reply, [reply, payload] {
            *payload += reply->readAll();
            if (payload->size() > 65536)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, manager, payload] {
            payload->append(reply->readAll());
            const auto bytes = *payload;
            const auto data = QJsonDocument::fromJson(bytes).object();
            const auto version = QVersionNumber::fromString(data.value("version").toString());
            if (reply->error() != QNetworkReply::NoError || bytes.size() > 65536 || version.isNull())
                QMessageBox::warning(this, "Updates", "The release feed could not be read or is invalid.");
            else
                QMessageBox::information(
                    this, "Updates",
                    version > QVersionNumber::fromString(QCoreApplication::applicationVersion())
                        ? "Available version: " + version.toString() + "\n" + data.value("url").toString()
                        : "No newer version in the configured feed.");
            reply->deleteLater();
            manager->deleteLater();
        });
    });
    add(505, "Volume label", [this] {
        QStorageInfo volume(displayPath(active_->path()));
        const auto type = volume.fileSystemType();
        const QString utility = type.startsWith("ext")    ? "e2label"
                                : type == "vfat"          ? "fatlabel"
                                : type == "exfat"         ? "exfatlabel"
                                : type.startsWith("ntfs") ? "ntfslabel"
                                                          : QString();
        if (utility.isEmpty() || QStandardPaths::findExecutable(utility).isEmpty()) {
            QMessageBox::information(this, "Volume label",
                                     "No supported filesystem label utility is installed for " +
                                         QString::fromLatin1(type) +
                                         ". Device: " + QString::fromLocal8Bit(volume.device()));
            return;
        }
        bool ok = false;
        const auto label = QInputDialog::getText(
            this, "Volume label", "New filesystem label for " + QString::fromLocal8Bit(volume.device()) + ":",
            QLineEdit::Normal, volume.name(), &ok);
        if (!ok)
            return;
        auto *process = new QProcess(this);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int code, QProcess::ExitStatus) {
                    QMessageBox::information(this, "Volume label",
                                             code ? QString::fromUtf8(process->readAllStandardError())
                                                  : "Filesystem label updated.");
                    process->deleteLater();
                });
        const auto executable = QStandardPaths::findExecutable(utility);
        process->start(::geteuid() == 0 ? executable : "pkexec",
                       ::geteuid() == 0
                           ? QStringList{QString::fromLocal8Bit(volume.device()), label}
                           : QStringList{executable, QString::fromLocal8Bit(volume.device()), label});
    });
}
void MainWindow::showAssociations() {
    NativeSettings::editRecords(this, "Internal associations — arguments support {path}", "associations",
                                {"mask", "kind", "program", "arguments"});
}
void MainWindow::setSeparateTrees(int count) {
    const auto create = [this](const QString &name, Qt::DockWidgetArea area, bool right) {
        auto *dock = new QDockWidget("Directory tree", this);
        dock->setObjectName(name);
        auto *tree = new QTreeView(dock);
        auto *model = new QFileSystemModel(tree);
        model->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        model->setRootPath("/");
        tree->setModel(model);
        tree->setHeaderHidden(true);
        for (int i = 1; i < 4; ++i)
            tree->hideColumn(i);
        dock->setWidget(tree);
        addDockWidget(area, dock);
        connect(tree, &QTreeView::clicked, this, [this, model, right](const QModelIndex &index) {
            auto *panel = right ? qobject_cast<FilePanel *>(right_->currentWidget())
                          : treeRight_ && treeRight_->isVisible()
                              ? qobject_cast<FilePanel *>(left_->currentWidget())
                              : active_;
            if (panel) {
                activate(panel);
                panel->navigate(nativePath(model->filePath(index)));
            }
        });
        return dock;
    };
    if (count && !treeLeft_)
        treeLeft_ = create("sharedDirectoryTree", Qt::LeftDockWidgetArea, false);
    if (count == 2 && !treeRight_)
        treeRight_ = create("rightDirectoryTree", Qt::RightDockWidgetArea, true);
    if (treeLeft_)
        treeLeft_->setVisible(count > 0);
    if (treeRight_)
        treeRight_->setVisible(count == 2);
}
void MainWindow::updateQuickView() {
    if (!quickView_ || !quickView_->isVisible() || !active_)
        return;
    const auto paths = active_->selectedPaths();
    if (auto *old = quickView_->widget()) {
        quickView_->setWidget(nullptr);
        old->deleteLater();
    }
    if (paths.empty()) {
        quickView_->setWidget(new QLabel("Choose a file", quickView_));
        return;
    }
    auto *viewer = new FileWindow(paths[0], false, quickView_);
    viewer->setWindowFlags(Qt::Widget);
    quickView_->setWidget(viewer);
    viewer->show();
}
void MainWindow::rebuildStartMenu() {
    auto *menu = findChild<QMenu *>("referenceMenu_Start1");
    if (!menu)
        return;
    for (auto *action : menu->actions()) {
        menu->removeAction(action);
        if (action->parent() == menu)
            action->deleteLater();
    }
    for (const auto &entry : QSettings().value("startMenu").toList()) {
        const auto record = entry.toMap();
        auto *target = commands_.get(record.value("command").toString());
        auto *action = menu->addAction(record.value("title").toString());
        action->setEnabled(target && target->isEnabled());
        if (target)
            connect(action, &QAction::triggered, target, &QAction::trigger);
        action->setShortcut(QKeySequence(record.value("shortcut").toString()));
    }
    if (!menu->actions().isEmpty())
        menu->addSeparator();
    menu->addAction(commands_.get("tc_700"));
}

void MainWindow::applyViewMode(const QVariantMap &mode) {
    if (applyingMode_ || !active_)
        return;
    QScopedValueRollback<bool> guard(applyingMode_, true);
    active_->setBrief(mode.value("brief").toBool());
    active_->model()->setColumns(mode.value("fields").toString().split(';', Qt::SkipEmptyParts));
    if (!mode.value("brief").toBool() && !mode.value("fields").toString().isEmpty())
        active_->setProperty("displayMode", "custom");
    const auto secondary = mode.value("secondary").toString();
    active_->model()->setSecondarySort(secondary == "extension" ? 1
                                       : secondary == "size"    ? 2
                                       : secondary == "date"    ? 3
                                                                : 0);
    active_->table()->sortByColumn(mode.value("sort").toInt(),
                                   static_cast<Qt::SortOrder>(mode.value("order").toInt()));
    auto palette = active_->table()->palette();
    if (QColor(mode.value("background").toString()).isValid())
        palette.setColor(QPalette::Base, QColor(mode.value("background").toString()));
    if (QColor(mode.value("alternate").toString()).isValid()) {
        palette.setColor(QPalette::AlternateBase, QColor(mode.value("alternate").toString()));
        active_->table()->setAlternatingRowColors(true);
    }
    active_->table()->setPalette(palette);
    auto *tabs = left_->indexOf(active_) >= 0 ? left_ : right_;
    const auto index = tabs->indexOf(active_);
    if (QColor(mode.value("tabColor").toString()).isValid())
        tabs->tabBar()->setTabTextColor(index, QColor(mode.value("tabColor").toString()));
    tabs->setTabIcon(index, QIcon(mode.value("icon").toString()));
    active_->refresh();
    if (auto *command = commands_.get(mode.value("command").toString()))
        if (command->isEnabled())
            command->trigger();
}
void MainWindow::reloadCustomMenu() {
    if (factoryMenuActions_.isEmpty())
        factoryMenuActions_ = menuBar()->actions();
    const auto file = QSettings().value("menu/file").toString();
    QList<QPointer<QMenu>> created;
    if (!file.isEmpty())
        try {
            const auto object = QJsonDocument::fromJson(Productivity::readSmall(nativePath(file))).object();
            const auto roots = object.value("menus").toArray();
            if (roots.isEmpty())
                throw NativeFs::Error("Menu JSON must contain a non-empty menus array");
            int count = 0;
            std::function<QMenu *(QJsonObject, QWidget *, int)> build = [&](QJsonObject definition,
                                                                            QWidget *parent, int depth) {
                if (depth > 8 || ++count > 512)
                    throw NativeFs::Error("Custom menu nesting/item limit exceeded");
                std::unique_ptr<QMenu> menu(new QMenu(definition.value("caption").toString(), parent));
                menu->setObjectName("customMenu");
                for (const auto &value : definition.value("items").toArray()) {
                    if (++count > 512)
                        throw NativeFs::Error("Custom menu item limit exceeded");
                    const auto item = value.toObject();
                    const auto caption = item.value("caption").toString();
                    if (caption == "-") {
                        menu->addSeparator();
                        continue;
                    }
                    if (item.contains("items")) {
                        menu->addMenu(build(item, menu.get(), depth + 1));
                        continue;
                    }
                    QAction *target = nullptr;
                    if (item.value("command").isString())
                        target = commands_.get(item.value("command").toString());
                    else
                        for (auto *action : findChildren<QAction *>())
                            if (item.value("command").toInt() != 0 &&
                                action->property("tcCommandId").toInt() == item.value("command").toInt()) {
                                target = action;
                                break;
                            }
                    if (!target)
                        throw NativeFs::Error("Custom menu references an unknown command");
                    auto *action = menu->addAction(caption);
                    action->setEnabled(target->isEnabled());
                    connect(action, &QAction::triggered, target, &QAction::trigger);
                }
                return menu.release();
            };
            for (const auto &root : roots)
                created << build(root.toObject(), this, 0);
        } catch (const std::exception &e) {
            for (const auto &menu : created)
                delete menu.data();
            QMessageBox::warning(this, "Custom menu", QString::fromLocal8Bit(e.what()));
            return;
        }
    menuBar()->clear();
    for (const auto &menu : customMenus_)
        if (menu)
            delete menu.data();
    customMenus_ = created;
    if (created.isEmpty())
        for (auto *action : factoryMenuActions_)
            menuBar()->addAction(action);
    else
        for (const auto &menu : created)
            menuBar()->addMenu(menu);
}
