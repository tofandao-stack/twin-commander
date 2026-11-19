#include "filepanel.h"
#include "backgroundpool.h"
#include "classicui.h"
#include "finalsettings.h"
#include "gapsettings.h"
#include "preferences.h"
#include <QApplication>
#include <QCompleter>
#include <QDateTime>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QtConcurrent/QtConcurrentRun>
#include <unordered_set>

#include <QDir>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QStorageInfo>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>

namespace {
class CommanderDelegate : public QStyledItemDelegate {
  public:
    CommanderDelegate(QAbstractItemView *view, bool brief)
        : QStyledItemDelegate(view), view_(view), brief_(brief) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        if (brief_) {
            styled.text = index.data(Qt::UserRole).toString();
            if (view_->property("thumbnails").toBool() && !index.data(Qt::UserRole + 1).toString().isEmpty())
                styled.text += "\n" + index.data(Qt::UserRole + 1).toString();
        }
        const bool marked = view_->selectionModel()->isRowSelected(index.row(), {});
        const bool cursor = view_->currentIndex().row() == index.row() && view_->hasFocus();
        styled.state.setFlag(QStyle::State_Selected, cursor);
        styled.state.setFlag(QStyle::State_HasFocus, false);
        if (marked && !cursor)
            styled.palette.setColor(QPalette::Text,
                                    QColor(QSettings().value("appearance/markColor", "#be0000").toString()));
        if (cursor && QSettings().value("appearance/invertCursor").toBool()) {
            const auto text = styled.palette.color(QPalette::Text),
                       base = styled.palette.color(QPalette::Base);
            styled.palette.setColor(QPalette::Highlight, text);
            styled.palette.setColor(QPalette::HighlightedText, base);
        }
        if (marked && !cursor && QSettings().value("appearance/invertSelection").toBool()) {
            const auto base = styled.palette.color(QPalette::Base),
                       text = styled.palette.color(QPalette::Text);
            styled.backgroundBrush = text;
            styled.palette.setColor(QPalette::Text, base);
        }
        view_->style()->drawControl(QStyle::CE_ItemViewItem, &styled, painter, view_);
    }

  private:
    QAbstractItemView *view_;
    bool brief_;
};
} // namespace

FilePanel::FilePanel(const std::filesystem::path &initialPath, QWidget *parent)
    : QWidget(parent), model_(new DirectoryModel(this)), table_(new QTableView(this)),
      briefList_(new QListView(this)), pathEdit_(new QLineEdit(this)), status_(new QLabel(this)),
      filter_(new QLineEdit(this)) {
    watcher_ = new QFileSystemWatcher(this);
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (QSettings().value("refresh/names").toBool())
            refreshPending_ = true;
    });
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        QSettings s;
        if (GapSettings::matches(path(), "refresh/exclude") ||
            GapSettings::matches(path(), "refresh/watchExclude"))
            return;
        if (s.value("refresh/foregroundOnly").toBool() && !window()->isActiveWindow())
            return;
        if (s.value("refresh/metadata").toBool())
            for (int checked = 0; checked < qMin(64, model_->rowCount()); ++checked) {
                if (refreshCursor_ >= model_->rowCount())
                    refreshCursor_ = 0;
                const auto *entry = model_->entry(refreshCursor_++);
                if (entry->parent)
                    continue;
                std::error_code error;
                const auto modified = std::filesystem::last_write_time(entry->path, error);
                if (error) {
                    if (entry->modified.isValid())
                        refreshPending_ = true;
                    continue;
                }
                const auto time = QDateTime::fromMSecsSinceEpoch(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::file_clock::to_sys(modified).time_since_epoch())
                        .count());
                const auto permissions = std::filesystem::symlink_status(entry->path, error).permissions();
                const auto bytes = std::filesystem::is_regular_file(entry->path, error)
                                       ? std::filesystem::file_size(entry->path, error)
                                       : 0;
                if (time != entry->modified || permissions != entry->permissions ||
                    (!entry->directory && bytes != entry->size)) {
                    refreshPending_ = true;
                    break;
                }
            }
        if (refreshPending_ && !autoRefreshing_ && !property("listingBusy").toBool()) {
            refreshPending_ = false;
            autoRefreshing_ = true;
            setProperty("autoRefreshing", true);
            refresh();
        }
    });
    refreshTimer_->start();
    model_->setShowParent(true);
    model_->setShowHidden(QSettings().value("display/hidden").toBool());
    QFont font = qApp->font();
    if (QSettings().contains("appearance/fileFont")) {
        font.fromString(QSettings().value("appearance/fileFont").toString());
        table_->setFont(font);
        briefList_->setFont(font);
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *pathRow = new QWidget(this);
    pathRow->setObjectName("currentDirectoryBar");
    auto *navigation = new QHBoxLayout(pathRow);
    navigation->setContentsMargins(0, 0, 0, 0);
    navigation->setSpacing(0);
    auto *breadcrumb = new QToolButton(this);
    breadcrumb->setText(QString::fromUtf8("▸"));
    breadcrumb->setFixedSize(14, 20);
    breadcrumb->setAutoRaise(true);
    breadcrumb->setToolTip("Edit current directory");
    navigation->addWidget(breadcrumb);
    breadcrumbs_ = new QWidget(pathRow);
    breadcrumbs_->setObjectName("breadcrumbParts");
    auto *parts = new QHBoxLayout(breadcrumbs_);
    parts->setContentsMargins(0, 0, 0, 0);
    parts->setSpacing(0);
    navigation->addWidget(breadcrumbs_, 1);
    navigation->addWidget(pathEdit_, 1);
    for (const auto &text : {QString("*"), QString::fromUtf8("▾")}) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setAutoRaise(true);
        button->setFixedSize(22, 20);
        button->setObjectName(text == "*" ? "directoryHotlistButton" : "directoryHistoryButton");
        button->setToolTip(text == "*" ? "Directory hotlist (Ctrl+D)" : "Directory history (Alt+Down)");
        navigation->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, text] {
            emit activated();
            if (text == "*")
                emit hotlistRequested();
            else
                showHistory();
        });
    }
    pathEdit_->setFrame(false);
    pathEdit_->setFixedHeight(20);
    connect(breadcrumb, &QToolButton::clicked, this, &FilePanel::focusPath);
    layout->addWidget(pathRow);
    layout->addWidget(table_, 1);
    layout->addWidget(briefList_, 1);
    filter_->hide();
    filter_->setObjectName("quickFilter");
    filter_->setPlaceholderText("Quick filter (Ctrl+S) · Esc clears");
    filter_->setAccessibleName("Quick filter");
    filter_->setClearButtonEnabled(true);
    filter_->installEventFilter(this);
    layout->addWidget(filter_);
    auto *filterDelay = new QTimer(this);
    filterDelay->setSingleShot(true);
    filterDelay->setInterval(180);
    connect(filter_, &QLineEdit::textChanged, filterDelay, qOverload<>(&QTimer::start));
    connect(filterDelay, &QTimer::timeout, this, [this] {
        model_->setNameFilter(filter_->text());
        refresh();
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableView::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = table_->indexAt(position);
        if (index.isValid() && !table_->selectionModel()->isSelected(index))
            table_->setCurrentIndex(index);
        emit activated();
        emit contextMenuRequested(table_->viewport()->mapToGlobal(position));
    });
    status_->setObjectName("panelStatus");
    layout->addWidget(status_);
    pathEdit_->setAccessibleName("Directory path");
    table_->setAccessibleName("Files");
    table_->setIconSize(QSize(16, 16));
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->setItemDelegate(new CommanderDelegate(table_, false));
    table_->setSortingEnabled(true);
    table_->sortByColumn(0, Qt::AscendingOrder);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(19);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setFixedHeight(20);
    table_->setColumnWidth(1, 55);
    table_->setColumnWidth(2, 90);
    table_->setColumnWidth(3, 127);
    table_->setColumnWidth(4, 42);
    briefList_->setModel(model_);
    briefList_->setModelColumn(0);
    briefList_->setSelectionModel(table_->selectionModel());
    briefList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    briefList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    briefList_->setFlow(QListView::TopToBottom);
    briefList_->setWrapping(true);
    briefList_->setMovement(QListView::Static);
    briefList_->setResizeMode(QListView::Adjust);
    briefList_->setGridSize(QSize(190, 19));
    briefList_->setUniformItemSizes(true);
    briefList_->setIconSize(QSize(16, 16));
    briefList_->setItemDelegate(new CommanderDelegate(briefList_, true));
    briefList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(briefList_, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = briefList_->indexAt(position);
        if (index.isValid() && !briefList_->selectionModel()->isSelected(index))
            briefList_->setCurrentIndex(index);
        emit activated();
        emit contextMenuRequested(briefList_->viewport()->mapToGlobal(position));
    });
    briefList_->setAccessibleName("Files (brief)");
    briefList_->hide();
    briefList_->installEventFilter(this);
    connect(briefList_, &QListView::doubleClicked, this, [this] {
        if (!QSettings().value("mouse/singleClick").toBool())
            openCurrent();
    });
    table_->installEventFilter(this);
    table_->viewport()->installEventFilter(this);
    table_->viewport()->setAcceptDrops(true);
    briefList_->viewport()->setAcceptDrops(true);
    briefList_->viewport()->installEventFilter(this);
    rightMenuTimer_ = new QTimer(this);
    rightMenuTimer_->setSingleShot(true);
    rightMenuTimer_->setInterval(1000);
    connect(rightMenuTimer_, &QTimer::timeout, this, [this] { emit contextMenuRequested(QCursor::pos()); });
    pathEdit_->installEventFilter(this);
    connect(pathEdit_, &QLineEdit::returnPressed, this, [this] {
        navigate(nativePath(pathEdit_->text()));
        focusFiles();
    });
    connect(table_, &QTableView::doubleClicked, this, [this] {
        if (!QSettings().value("mouse/singleClick").toBool())
            openCurrent();
    });
    connect(model_, &DirectoryModel::loading, this, [this] {
        setProperty("listingBusy", true);
        if (!autoRefreshing_ || QSettings().value("refresh/footer", true).toBool())
            status_->setText("Loading…");
    });
    connect(model_, &DirectoryModel::batchReceived, this, [this](int, int total) {
        if (!autoRefreshing_ || QSettings().value("refresh/footer", true).toBool())
            status_->setText(QString("Loading… %1 items").arg(total));
    });
    connect(model_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        previousPath_ = path();
        previousSelection_.clear();
        for (const auto &index : table_->selectionModel()->selectedRows())
            previousSelection_.insert(model_->entry(index.row())->path);
        const auto current = table_->currentIndex();
        previousRow_ = current.isValid() ? current.row() : 0;
        previousColumn_ = current.isValid() ? current.column() : 0;
        const auto *entry = model_->entry(current.row());
        previousCursor_ = entry ? entry->path : std::filesystem::path{};
    });
    connect(model_, &DirectoryModel::loaded, this, [this] {
        setProperty("listingBusy", false);
        const auto currentPath = model_->path();
        if (backRequested_ && history_.size() >= 2 && history_[history_.size() - 2] == currentPath) {
            forward_.push_back(history_.back());
            history_.pop_back();
        }
        backRequested_ = false;
        if (history_.empty() || history_.back() != currentPath) {
            history_.push_back(currentPath);
            if (history_.size() > 1000)
                history_.erase(history_.begin());
        }
        pathEdit_->setText(displayPath(currentPath) +
                           (displayPath(currentPath).endsWith('/') ? "*.*" : "/*.*"));
        const bool sameDirectory = previousPath_ == currentPath;
        int cursorRow = sameDirectory ? std::min(previousRow_, model_->rowCount() - 1) : 0;
        QItemSelection selection;
        if (sameDirectory) {
            for (int row = 0; row < model_->rowCount(); ++row) {
                const auto &identity = model_->entry(row)->path;
                if (identity == previousCursor_)
                    cursorRow = row;
                if (previousSelection_.contains(identity))
                    selection.select(model_->index(row, 0), model_->index(row, model_->columnCount() - 1));
            }
        }
        table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (model_->rowCount())
            table_->selectionModel()->setCurrentIndex(
                model_->index(cursorRow, sameDirectory ? previousColumn_ : 0), QItemSelectionModel::NoUpdate);
        previousSelection_.clear();
        if (!revealPath_.empty()) {
            for (int row = 0; row < model_->rowCount(); ++row) {
                if (model_->entry(row)->path == revealPath_) {
                    table_->selectionModel()->setCurrentIndex(model_->index(row, 0),
                                                              QItemSelectionModel::ClearAndSelect |
                                                                  QItemSelectionModel::Rows);
                    table_->scrollTo(model_->index(row, 0));
                    break;
                }
            }
            revealPath_.clear();
        }
        const auto watches = watcher_->directories();
        if (!watches.isEmpty())
            watcher_->removePaths(watches);
        if (QSettings().value("refresh/names").toBool() &&
            nativePath(displayPath(currentPath)) == currentPath)
            watcher_->addPath(displayPath(currentPath));
        if (!autoRefreshing_ || QSettings().value("refresh/footer", true).toBool())
            updateStatus();
        updateBreadcrumbs();
        emit pathChanged(displayPath(currentPath));
        autoRefreshing_ = false;
        setProperty("autoRefreshing", false);
    });
    connect(model_, &QAbstractItemModel::columnsInserted, this, [this] { applyPreferences(); });
    connect(model_, &DirectoryModel::loadFailed, this, [this](const QString &message) {
        setProperty("listingBusy", false);
        autoRefreshing_ = false;
        setProperty("autoRefreshing", false);
        backRequested_ = false;
        pathEdit_->setText(displayPath(path()));
        updateStatus();
        status_->setText(message);
        emit errorOccurred(message);
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FilePanel::updateStatus);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index) {
                if (!FinalSettings::flag("id_scrollahead") || !table_->isVisible() || !index.isValid())
                    return;
                QTimer::singleShot(0, this, [this, index] {
                    const int extra = qBound(0, FinalSettings::number("id_extralines", 3), 100) *
                                      table_->verticalHeader()->defaultSectionSize();
                    const int bottom = table_->visualRect(index).bottom() + extra;
                    const int delta = bottom - table_->viewport()->height();
                    if (delta > 0)
                        table_->verticalScrollBar()->setValue(table_->verticalScrollBar()->value() + delta);
                });
            });
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    applyPreferences();
    navigate(initialPath);
}

void FilePanel::navigate(const std::filesystem::path &requested) {
    if (requested != model_->path()) {
        model_->setResultPaths(nullptr);
        model_->setViewFilter();
        setProperty("viewFilter", 312);
        setProperty("viewMask", QString());
        setProperty("viewPrograms", false);
    }

    if (!historyTraversal_)
        forward_.clear();
    backRequested_ = false;
    const auto base = path().empty() ? nativePath(QDir::currentPath()) : path();
    const auto target = requested.is_absolute() ? requested : base / requested;
    model_->load(target);
}

void FilePanel::parentDirectory() {
    if (!path().empty())
        navigate(path().parent_path());
}

void FilePanel::back() {
    if (history_.size() < 2)
        return;
    const auto previous = history_[history_.size() - 2];
    historyTraversal_ = true;
    navigate(previous);
    historyTraversal_ = false;
    backRequested_ = true;
}

void FilePanel::refresh() {
    if (!path().empty())
        model_->load(path());
}
void FilePanel::toggleHidden() {
    model_->setShowHidden(!model_->showHidden());
    refresh();
}
void FilePanel::focusFiles() {
    if (tree_ && property("displayMode") == "tree") {
        tree_->setFocus();
        return;
    }
    if (brief_)
        briefList_->setFocus();
    else
        table_->setFocus();
}
void FilePanel::focusPath() {
    breadcrumbs_->hide();
    pathEdit_->show();
    pathEdit_->setFocus();
    pathEdit_->setText(displayPath(path()));
    pathEdit_->selectAll();
}
std::filesystem::path FilePanel::path() const { return model_->path(); }
QTableView *FilePanel::table() const { return table_; }
DirectoryModel *FilePanel::model() const { return model_; }

void FilePanel::openCurrent() {
    const auto *item = model_->entry(table_->currentIndex().row());
    if (item && item->directory)
        navigate(item->path);
    else if (item)
        emit openFileRequested(item->path);
}

void FilePanel::setActive(bool active) {
    auto colors = pathEdit_->palette();
    colors.setColor(QPalette::Base, palette().color(active ? QPalette::Highlight : QPalette::Mid));
    colors.setColor(QPalette::Text, palette().color(active ? QPalette::HighlightedText : QPalette::Text));
    pathEdit_->setPalette(colors);
}
void FilePanel::focusFilter(bool restoreLast) {
    filter_->show();
    if (restoreLast)
        filter_->setText(lastFilter_);
    filter_->setFocus();
    filter_->selectAll();
}
void FilePanel::clearFilter() {
    lastFilter_ = filter_->text();
    filter_->clear();
    filter_->hide();
    focusFiles();
}
void FilePanel::setBrief(bool brief) {
    if (tree_)
        tree_->hide();
    briefList_->setProperty("thumbnails", false);
    model_->setThumbnails(false);
    briefList_->setViewMode(QListView::ListMode);
    briefList_->setFlow(QListView::TopToBottom);
    briefList_->setGridSize(QSize(190, 19));
    briefList_->setIconSize(QSize(16, 16));
    brief_ = brief;
    setProperty("displayMode", brief ? "brief" : "full");
    table_->setVisible(!brief);
    briefList_->setVisible(brief);
    applyPreferences();
    focusFiles();
}
bool FilePanel::isBrief() const { return brief_; }
void FilePanel::saveSelection() {
    savedDirectory_ = path();
    savedSelection_.clear();
    for (const auto &index : table_->selectionModel()->selectedRows())
        if (!model_->entry(index.row())->parent)
            savedSelection_.push_back(model_->entry(index.row())->path);
}
void FilePanel::restoreSelection() {
    if (savedDirectory_ == path())
        selectPaths(savedSelection_);
}
void FilePanel::selectPaths(const std::vector<std::filesystem::path> &paths) {
    const std::unordered_set<std::filesystem::path> selected(paths.begin(), paths.end());
    QItemSelection selection;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (!model_->entry(row)->parent && selected.contains(model_->entry(row)->path))
            selection.select(model_->index(row, 0), model_->index(row, model_->columnCount() - 1));
    table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}
void FilePanel::showHistory() {
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName("directoryHistoryMenu");
    QSettings settings;
    QFont font = menu->font();
    const int family = settings.value("history/font", 0).toInt();
    if (family)
        font.setFamily(QStringList{"", "Monospace", "Sans Serif", "Serif"}.value(family));
    menu->setFont(font);
    auto entries = history_;
    for (const auto &value : settings.value("directoryHistoryNative").toList()) {
        const auto entry = std::filesystem::path(value.toByteArray().toStdString());
        if (std::find(entries.begin(), entries.end(), entry) == entries.end())
            entries.insert(entries.begin(), entry);
    }
    if (settings.value("history/thin", true).toBool()) {
        std::vector<std::filesystem::path> thinned;
        for (const auto &entry : entries) {
            if (!thinned.empty() && thinned.back() == entry.parent_path())
                thinned.pop_back();
            thinned.push_back(entry);
        }
        entries = std::move(thinned);
    }
    std::reverse(entries.begin(), entries.end());
    entries.resize(
        qMin(entries.size(), size_t(qBound(1, settings.value("history/showLimit", 30).toInt(), 1000))));
    const int width =
        qBound(20, settings.value("history/width", 100).toInt(), 300) * QFontMetrics(font).averageCharWidth();
    QAction *recent = nullptr, *frequentAction = nullptr;
    QFileIconProvider icons;
    const auto add = [&](const std::filesystem::path &target) {
        const auto full = displayPath(target);
        auto *action = menu->addAction(QFontMetrics(font).elidedText(full, Qt::ElideMiddle, width));
        action->setToolTip(full);
        action->setData(QByteArray::fromStdString(target.native()));
        if (settings.value("history/icons").toBool())
            action->setIcon(icons.icon(QFileIconProvider::Folder));
        connect(action, &QAction::triggered, this, [this, target] {
            navigate(target);
            focusFiles();
        });
        return action;
    };
    if (settings.value("history/recentMiddle").toBool() && entries.size() > 1)
        std::rotate(entries.begin(), entries.begin() + 1, entries.begin() + 1 + entries.size() / 2);
    for (const auto &target : entries) {
        auto *action = add(target);
        if (target == path())
            recent = action;
    }
    const auto frequent = Preferences::frequent();
    if (!frequent.isEmpty()) {
        menu->addSeparator();
        menu->addSection("Frequently used");
        std::vector<std::pair<int, QString>> ranked;
        for (auto it = frequent.begin(); it != frequent.end(); ++it)
            ranked.emplace_back(it.value().toInt(), it.key());
        std::sort(ranked.rbegin(), ranked.rend());
        ranked.resize(
            qMin(ranked.size(), size_t(qBound(1, settings.value("frequent/showLimit", 20).toInt(), 1000))));
        const auto top = ranked.front().second;
        if (settings.value("history/frequentMiddle").toBool() && ranked.size() > 1)
            std::rotate(ranked.begin(), ranked.begin() + 1, ranked.begin() + 1 + ranked.size() / 2);
        for (const auto &[count, encoded] : ranked) {
            Q_UNUSED(count);
            const auto target =
                std::filesystem::path(QByteArray::fromBase64(encoded.toLatin1()).toStdString());
            auto *action = add(target);
            if (encoded == top)
                frequentAction = action;
        }
    }
    const int scroll = settings.value("history/scroll", 0).toInt();
    auto *at = scroll == 1 ? recent : scroll == 2 ? frequentAction : nullptr;
    menu->popup(pathEdit_->mapToGlobal(QPoint(0, pathEdit_->height())), at);
    if (at)
        menu->setActiveAction(at);
}
void FilePanel::forward() {
    if (!forward_.empty()) {
        const auto target = forward_.back();
        forward_.pop_back();
        historyTraversal_ = true;
        navigate(target);
        historyTraversal_ = false;
    }
}
void FilePanel::selectMask(const QString &mask, bool select) {
    QItemSelection selection;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (!model_->entry(row)->parent &&
            (QSettings().value("selection/directories", 1).toInt() != 0 || !model_->entry(row)->directory) &&
            QDir::match(mask, displayPath(model_->entry(row)->path.filename())))
            selection.select(model_->index(row, 0), model_->index(row, model_->columnCount() - 1));
    table_->selectionModel()->select(selection,
                                     select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}
void FilePanel::invertSelection() {
    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto *e = model_->entry(row);
        if (!e->parent && (!e->directory || QSettings().value("selection/directories", 1).toInt() != 0))
            table_->selectionModel()->select(model_->index(row, 0),
                                             QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
}

std::vector<std::filesystem::path> FilePanel::selectedPaths() const {
    if (property("displayMode") == "tree") {
        const auto selected =
            tree_ && tree_->currentIndex().isValid()
                ? nativePath(
                      qobject_cast<QFileSystemModel *>(tree_->model())->filePath(tree_->currentIndex()))
                : path();
        return selected.empty() || selected == selected.parent_path()
                   ? std::vector<std::filesystem::path>{}
                   : std::vector<std::filesystem::path>{selected};
    }
    std::vector<std::filesystem::path> paths;
    for (const auto &index : table_->selectionModel()->selectedRows())
        if (!model_->entry(index.row())->parent)
            paths.push_back(model_->entry(index.row())->path);
    if (paths.empty()) {
        if (const auto *entry = model_->entry(table_->currentIndex().row()); entry && !entry->parent)
            paths.push_back(entry->path);
    }
    return paths;
}
void FilePanel::reveal(const std::filesystem::path &path) {
    revealPath_ = path;
    model_->setViewFilter();
    model_->setResultPaths(nullptr);
    filter_->clear();
    model_->setNameFilter({});
    if (!path.filename().native().empty() && path.filename().native().front() == '.')
        model_->setShowHidden(true);
    navigate(path.parent_path());
}
void FilePanel::updateStatus() {
    if (autoRefreshing_ && !QSettings().value("refresh/footer", true).toBool())
        return;
    quint64 bytes = 0, total = 0;
    int files = 0, dirs = 0, selectedFiles = 0, selectedDirs = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto *entry = model_->entry(row);
        if (entry->parent)
            continue;
        if (entry->directory)
            ++dirs;
        else {
            ++files;
            total += entry->size;
        }
        if (table_->selectionModel()->isRowSelected(row, {})) {
            if (entry->directory)
                ++selectedDirs;
            else {
                ++selectedFiles;
                bytes += entry->size;
            }
        }
    }
    const int mode = QSettings().value("format/footerSize", 1).toInt();
    QString text = QString("%1 / %2 in %3 / %4 file(s)")
                       .arg(Preferences::sizeText(bytes, mode), Preferences::sizeText(total, mode))
                       .arg(selectedFiles)
                       .arg(files);
    if (QSettings().value("format/directoryCount", true).toBool())
        text += QString(", %1 / %2 dir(s)").arg(selectedDirs).arg(dirs);
    status_->setText(text);
    status_->setFixedHeight(19);
}

bool FilePanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Enter && watched->property("breadcrumbPath").isValid() &&
        QSettings().value("layout/breadcrumbHover").toBool()) {
        auto *button = qobject_cast<QToolButton *>(watched);
        if (button) {
            auto *menu = new QMenu(button);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            const auto base =
                std::filesystem::path(button->property("breadcrumbPath").toByteArray().toStdString());
            size_t count = 0;
            std::error_code error;
            for (std::filesystem::directory_iterator
                     it(base, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 !error && it != end && count < 1000; it.increment(error)) {
                if (!it->is_directory(error))
                    continue;
                const auto target = it->path();
                if (!model_->showHidden() && displayPath(target.filename()).startsWith('.'))
                    continue;
                ++count;
                connect(menu->addAction(displayPath(target.filename())), &QAction::triggered, this,
                        [this, target] {
                            navigate(target);
                            focusFiles();
                        });
            }
            if (!menu->isEmpty())
                menu->popup(button->mapToGlobal(QPoint(0, button->height())));
            else
                menu->deleteLater();
        }
    }
    if (watched == table_->viewport() || watched == briefList_->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDragMoveEvent *>(event);
            if (drag->mimeData()->hasFormat(FinalSettings::dragType())) {
                drag->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            const auto paths = FinalSettings::dropPaths(drop->mimeData());
            if (!paths.empty()) {
                emit filesDropped(paths, drop->proposedAction() == Qt::MoveAction);
                drop->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove &&
            static_cast<QMouseEvent *>(event)->buttons().testFlag(Qt::LeftButton) && dragMarked_ &&
            (static_cast<QMouseEvent *>(event)->position().toPoint() - dragStart_).manhattanLength() >=
                QApplication::startDragDistance()) {
            dragMarked_ = false;
            auto *drag = new QDrag(this);
            drag->setMimeData(FinalSettings::dragData(selectedPaths()));
            drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
            drag->deleteLater();
            return true;
        }

        auto *view = watched == table_->viewport() ? static_cast<QAbstractItemView *>(table_) : briefList_;
        if (event->type() == QEvent::ContextMenu && QSettings().value("mouse/selection", 1).toInt() == 0) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove && !QSettings().value("mouse/rubberband", true).toBool() &&
            static_cast<QMouseEvent *>(event)->buttons().testFlag(Qt::LeftButton))
            return true;
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const auto index = view->indexAt(mouse->position().toPoint());
            dragStart_ = mouse->position().toPoint();
            dragMarked_ = mouse->button() == Qt::LeftButton && index.isValid() &&
                          view->selectionModel()->isRowSelected(index.row(), {});
            const bool right =
                mouse->button() == Qt::RightButton && QSettings().value("mouse/selection", 1).toInt() == 0;
            const bool icon =
                mouse->button() == Qt::LeftButton && QSettings().value("mouse/iconSelect").toBool() &&
                (!QSettings().value("mouse/touchOnly").toBool() ||
                 mouse->source() != Qt::MouseEventNotSynthesized) &&
                index.isValid() &&
                mouse->position().x() < view->visualRect(index).left() + view->iconSize().width() + 6;
            if (right || icon) {
                emit activated();
                if (index.isValid() && !model_->entry(index.row())->parent) {
                    view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
                    view->selectionModel()->select(index,
                                                   QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                }
                setProperty("mouseMarking", true);
                if (right)
                    rightMenuTimer_->start();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick && QSettings().value("mouse/singleClick").toBool()) {
            setProperty("skipSingleRelease", true);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            if (property("skipSingleRelease").toBool()) {
                setProperty("skipSingleRelease", false);
                return true;
            }
            auto *mouse = static_cast<QMouseEvent *>(event);
            rightMenuTimer_->stop();
            if (property("mouseMarking").toBool()) {
                setProperty("mouseMarking", false);
                return true;
            }
            if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier &&
                QSettings().value("mouse/singleClick").toBool() &&
                view->indexAt(mouse->position().toPoint()).isValid()) {
                openCurrent();
                return true;
            }
        }
    }

    if (event->type() == QEvent::FocusIn)
        emit activated();
    if (watched == pathEdit_ && event->type() == QEvent::FocusIn)
        pathEdit_->setText(displayPath(path()));
    if (watched == pathEdit_ && event->type() == QEvent::FocusOut && !path().empty()) {
        pathEdit_->setText(displayPath(path()) + (displayPath(path()).endsWith('/') ? "*.*" : "/*.*"));
        QTimer::singleShot(0, this, [this] { updateBreadcrumbs(); });
    }
    if (watched == filter_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            clearFilter();
            return true;
        }
        if (key->key() == Qt::Key_Return) {
            focusFiles();
            return true;
        }
    }
    if ((watched == table_ || watched == briefList_) && event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int mode = QSettings().value("quick/mode", 2).toInt();
        const bool matches = (mode == 0 && key->modifiers() == (Qt::ControlModifier | Qt::AltModifier)) ||
                             (mode == 1 && key->modifiers() == Qt::AltModifier);
        if (matches && !key->text().isEmpty() && key->text().front().isPrint()) {
            key->accept();
            return true;
        }
    }
    if ((watched == table_ || watched == briefList_) && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_A && key->modifiers() == Qt::ControlModifier) {
            table_->clearSelection();
            selectMask("*");
            return true;
        }
        const int quickMode = QSettings().value("quick/mode", 2).toInt();
        const bool quickModifiers =
            (quickMode == 0 && key->modifiers() == (Qt::ControlModifier | Qt::AltModifier)) ||
            (quickMode == 1 && key->modifiers() == Qt::AltModifier) ||
            ((quickMode == 2 || quickMode == 3) &&
             (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier));
        if (quickMode != 4 && quickModifiers && !key->text().isEmpty() && key->text().front().isPrint() &&
            key->key() != Qt::Key_Space) {
            if (quickMode == 3) {
                focusFilter();
                filter_->setText(filter_->text() + key->text());
                return true;
            }
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (now - quickTime_ > 1200)
                quickText_.clear();
            quickTime_ = now;
            quickText_ += key->text();
            const auto match = [&](const QString &name) {
                const bool beginning = QSettings().value("quick/beginning", true).toBool();
                if (QSettings().value("quick/ending").toBool() && quickText_.contains('.')) {
                    const auto dot = quickText_.indexOf('.');
                    const auto nameDot = name.lastIndexOf('.');
                    if (nameDot < 0)
                        return false;
                    const auto stem = name.left(nameDot), typed = quickText_.left(dot);
                    if (!(beginning ? QString::compare(stem, typed, Qt::CaseInsensitive) == 0
                                    : stem.endsWith(typed, Qt::CaseInsensitive)))
                        return false;
                    return name.mid(nameDot + 1).startsWith(quickText_.mid(dot + 1), Qt::CaseInsensitive);
                }
                return beginning ? name.startsWith(quickText_, Qt::CaseInsensitive)
                                 : name.contains(quickText_, Qt::CaseInsensitive);
            };
            for (int row = 0; row < model_->rowCount(); ++row)
                if (!model_->entry(row)->parent && match(model_->entry(row)->name)) {
                    table_->selectionModel()->setCurrentIndex(model_->index(row, 0),
                                                              QItemSelectionModel::NoUpdate);
                    (brief_ ? static_cast<QAbstractItemView *>(briefList_) : table_)
                        ->scrollTo(model_->index(row, 0));
                    break;
                }
            return true;
        }
        if (quickMode == 4 && key->modifiers() == Qt::NoModifier && !key->text().isEmpty() &&
            key->text().front().isPrint() && key->key() != Qt::Key_Space)
            return true;
        if (key->modifiers() == Qt::NoModifier) {
            switch (key->key()) {
            case Qt::Key_Tab:
                emit switchPanel();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!FinalSettings::flag("idExecCmdLineFocused") &&
                    !property("commandLineText").toString().isEmpty())
                    emit executeCommandRequested();
                else
                    openCurrent();
                return true;
            case Qt::Key_Backspace:
                parentDirectory();
                return true;
            case Qt::Key_Insert:
            case Qt::Key_Space: {
                const auto current = table_->currentIndex();
                if (current.isValid() && !model_->entry(current.row())->parent) {
                    if (key->key() == Qt::Key_Space && FinalSettings::flag("IdConf_Countfiles") &&
                        model_->entry(current.row())->directory)
                        countDirectory(model_->entry(current.row())->path);
                    table_->selectionModel()->select(current,
                                                     QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                    if (key->key() == Qt::Key_Insert && current.row() + 1 < model_->rowCount())
                        table_->selectionModel()->setCurrentIndex(model_->index(current.row() + 1, 0),
                                                                  QItemSelectionModel::NoUpdate);
                }
                return true;
            }
            default:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FilePanel::filterView(const QString &mask, bool programs, bool selected) {
    setProperty("viewMask", mask);
    setProperty("viewPrograms", programs);
    setProperty("viewFilter", selected ? 2023 : programs ? 311 : mask.isEmpty() ? 312 : 314);
    std::vector<std::filesystem::path> paths;
    for (const auto &index : table_->selectionModel()->selectedRows())
        if (!model_->entry(index.row())->parent)
            paths.push_back(model_->entry(index.row())->path);
    model_->setViewFilter(mask, programs, selected ? &paths : nullptr);
    refresh();
}
void FilePanel::storeHistory() {
    auto merged = QSettings().value("directoryHistoryNative").toList();
    for (const auto &path : history_) {
        const auto value = QByteArray::fromStdString(path.native());
        merged.removeAll(value);
        merged.append(value);
    }
    const int limit = qBound(1, QSettings().value("history/saveLimit", 100).toInt(), 1000);
    while (merged.size() > limit)
        merged.removeFirst();
    QSettings().setValue("directoryHistoryNative", merged);
}
void FilePanel::clearHistory() {
    history_ = {path()};
    forward_.clear();
    QSettings().remove("directoryHistoryNative");
}

void FilePanel::setDisplayMode(const QString &mode) {
    if (mode == "tree") {
        if (!tree_) {
            auto *filesystem = new QFileSystemModel(this);
            filesystem->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
            filesystem->setResolveSymlinks(false);
            filesystem->setRootPath("/");
            tree_ = new QTreeView(this);
            tree_->setObjectName("panelTree");
            tree_->setModel(filesystem);
            tree_->installEventFilter(this);
            tree_->setHeaderHidden(true);
            connect(tree_, &QTreeView::activated, this, [this, filesystem](const QModelIndex &index) {
                if (FinalSettings::flag("IdTreeNoAutoChange") && index.isValid())
                    emit treeDirectoryRequested(nativePath(filesystem->filePath(index)));
            });
            for (int i = 1; i < 4; ++i)
                tree_->hideColumn(i);
            qobject_cast<QVBoxLayout *>(layout())->insertWidget(2, tree_, 1);
            connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
                    [this, filesystem](const QModelIndex &index) {
                        if (!FinalSettings::flag("IdTreeNoAutoChange") && index.isValid())
                            navigate(nativePath(filesystem->filePath(index)));
                    });
        }
        table_->hide();
        briefList_->hide();
        tree_->show();
        setProperty("displayMode", mode);
        tree_->setCurrentIndex(qobject_cast<QFileSystemModel *>(tree_->model())->index(displayPath(path())));
        tree_->setFocus();
        preloadTree();
        return;
    }
    setBrief(mode == "brief" || mode == "thumbnails");
    if (mode == "thumbnails") {
        briefList_->setProperty("thumbnails", true);
        model_->setThumbnails(true);
        briefList_->setViewMode(QListView::IconMode);
        briefList_->setFlow(QListView::LeftToRight);
        briefList_->setMovement(QListView::Static);
        const QSize size(qBound(16, QSettings().value("thumbnail/width", 128).toInt(), 512),
                         qBound(16, QSettings().value("thumbnail/height", 96).toInt(), 512));
        briefList_->setIconSize(size);
        briefList_->setGridSize(size + QSize(32, 44));
        setProperty("displayMode", mode);
        refresh();
    }
}

void FilePanel::applyPreferences() {
    QSettings s;
    if (s.contains("appearance/fileFont")) {
        QFont font;
        font.fromString(s.value("appearance/fileFont").toString());
        if (s.value("appearance/manualDpi").toBool()) {
            const double scale = qBound(72, s.value("appearance/dpi", 96).toInt(), 240) / 96.0;
            if (font.pixelSize() > 0)
                font.setPixelSize(qMax(6, qRound(font.pixelSize() * scale)));
            else
                font.setPointSizeF(qMax(4.0, font.pointSizeF() * scale));
        }
        table_->setFont(font);
        briefList_->setFont(font);
        if (tree_)
            tree_->setFont(font);
    }

    const auto frame = s.value("layout/flatInterface", true).toBool() ? QFrame::NoFrame : QFrame::StyledPanel;
    table_->setFrameShape(frame);
    briefList_->setFrameShape(frame);
    if (s.value("completion/enabled").toBool()) {
        if (!pathEdit_->completer()) {
            auto *files = new QFileSystemModel(pathEdit_);
            files->setRootPath("/");
            auto *completer = new QCompleter(files, pathEdit_);
            completer->setCaseSensitivity(Qt::CaseSensitive);
            pathEdit_->setCompleter(completer);
        }
        pathEdit_->completer()->setCompletionMode(s.value("completion/inline").toBool()
                                                      ? QCompleter::InlineCompletion
                                                      : QCompleter::PopupCompletion);
    } else
        pathEdit_->setCompleter(nullptr);
    briefList_->setSelectionRectVisible(s.value("mouse/rubberband", true).toBool());
    updateBreadcrumbs();
    const int sizes[]{16, 20, 24, 32, 48}, spaces[]{0, 2, 4, 8, 16};
    const int size = sizes[qBound(0, s.value("icons/size", 0).toInt(), 4)];
    const int x = spaces[qBound(0, s.value("icons/extraX", 0).toInt(), 4)],
              y = spaces[qBound(0, s.value("icons/extraY", 0).toInt(), 4)];
    table_->setIconSize(QSize(size, size));
    table_->verticalHeader()->setDefaultSectionSize(qMax(19, size + 3) + y);
    table_->setStyleSheet(QString("QTableView::item { padding-left: %1px; padding-right: %1px; }").arg(x));
    table_->horizontalHeader()->setSectionResizeMode(
        0, s.value("columns/stretch", true).toBool() ? QHeaderView::Stretch : QHeaderView::Interactive);
    table_->setColumnHidden(1, s.value("format/extension", 1).toInt() == 0);
    const char *keys[]{"columns/extension", "columns/size", "columns/date", "columns/attributes"};
    const int widths[]{55, 90, 127, 42};
    for (int c = 1; c < 5; ++c)
        table_->setColumnWidth(c, s.value(keys[c - 1], widths[c - 1]).toInt());
    for (int c = 5; c < model_->columnCount(); ++c)
        if (model_->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString() == "comment")
            table_->setColumnWidth(c, s.value("columns/comment", 180).toInt());
    if (!briefList_->property("thumbnails").toBool()) {
        briefList_->setIconSize(QSize(size, size));
        briefList_->setGridSize(
            QSize(s.value("display/limitBrief").toBool() ? s.value("display/briefWidth", 190).toInt() : 190,
                  qMax(19, size + 3) + y));
    }
    updateStatus();
}

void FilePanel::updateBreadcrumbs() {
    if (!breadcrumbs_)
        return;
    const bool show = QSettings().value("layout/breadcrumbs").toBool() && !pathEdit_->hasFocus();
    breadcrumbs_->setVisible(show);
    if (!pathEdit_->hasFocus())
        pathEdit_->setVisible(!show);
    if (!show)
        return;
    while (auto *item = breadcrumbs_->layout()->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    std::filesystem::path target;
    for (const auto &component : path()) {
        target /= component;
        auto *button = new QToolButton(breadcrumbs_);
        button->setAutoRaise(true);
        button->setText(displayPath(component));
        button->setToolTip(displayPath(target));
        button->setProperty("breadcrumbPath", QByteArray::fromStdString(target.native()));
        button->installEventFilter(this);
        breadcrumbs_->layout()->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, target] {
            navigate(target);
            focusFiles();
        });
    }
    qobject_cast<QHBoxLayout *>(breadcrumbs_->layout())->addStretch();
}

FilePanel::~FilePanel() {
    if (treeControl_)
        treeControl_->cancelled = true;
}
void FilePanel::countDirectory(const std::filesystem::path &path) {
    auto control = std::make_shared<LocalFiles::Control>();
    auto *watcher = new QFutureWatcher<FileTools::Statistics>(this);
    connect(this, &QObject::destroyed, watcher, [control] { control->cancelled = true; });
    connect(watcher, &QFutureWatcher<FileTools::Statistics>::finished, this, [this, watcher, path] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (result.errors.isEmpty() && !result.cancelled && !result.truncated) {
            model_->setDirectorySize(path, result.bytes);
            updateStatus();
        }
    });
    watcher->setFuture(
        QtConcurrent::run(ioPool(), FinalSettings::sizes, std::vector<std::filesystem::path>{path}, control));
}
void FilePanel::preloadTree() {
    if (!tree_ || !FinalSettings::flag("IdConfOldStyleTree"))
        return;
    if (treeControl_)
        treeControl_->cancelled = true;
    treeControl_ = std::make_shared<LocalFiles::Control>();
    const auto root = path();
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher] {
        const auto paths = watcher->result();
        watcher->deleteLater();
        if (!tree_)
            return;
        auto *model = qobject_cast<QFileSystemModel *>(tree_->model());
        for (const auto &path : paths) {
            const auto index = model->index(path);
            if (index.isValid()) {
                model->fetchMore(index);
                tree_->expand(index);
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [root, control = treeControl_] {
        QStringList result;
        try {
            std::error_code error;
            const auto rootDevice = NativeFs::inspect(root).st_dev;
            size_t scanned = 0;
            for (std::filesystem::recursive_directory_iterator
                     it(root, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 !error && it != end && !control->cancelled && scanned++ < 100000 && result.size() < 10000;
                 it.increment(error)) {
                const auto st = NativeFs::inspect(it->path());
                if (!S_ISDIR(st.st_mode) || st.st_dev != rootDevice || it.depth() > 32) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (nativePath(displayPath(it->path())) == it->path())
                    result << displayPath(it->path());
            }
        } catch (const std::exception &) {
        }
        return result;
    }));
}
