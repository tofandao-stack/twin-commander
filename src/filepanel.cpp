#include "filepanel.h"
#include "classicui.h"
#include <QApplication>
#include <QFileSystemModel>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTreeView>
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
    model_->setShowParent(true);
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
    connect(briefList_, &QListView::doubleClicked, this, [this] { openCurrent(); });
    table_->installEventFilter(this);
    pathEdit_->installEventFilter(this);
    connect(pathEdit_, &QLineEdit::returnPressed, this, [this] {
        navigate(nativePath(pathEdit_->text()));
        focusFiles();
    });
    connect(table_, &QTableView::doubleClicked, this, [this] { openCurrent(); });
    connect(model_, &DirectoryModel::loading, this, [this] { status_->setText("Loading…"); });
    connect(model_, &DirectoryModel::batchReceived, this,
            [this](int, int total) { status_->setText(QString("Loading… %1 items").arg(total)); });
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
        const auto currentPath = model_->path();
        if (backRequested_ && history_.size() >= 2 && history_[history_.size() - 2] == currentPath) {
            forward_.push_back(history_.back());
            history_.pop_back();
        }
        backRequested_ = false;
        if (history_.empty() || history_.back() != currentPath)
            history_.push_back(currentPath);
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
        updateStatus();
        emit pathChanged(displayPath(currentPath));
    });
    connect(model_, &DirectoryModel::loadFailed, this, [this](const QString &message) {
        backRequested_ = false;
        pathEdit_->setText(displayPath(path()));
        updateStatus();
        status_->setText(message);
        emit errorOccurred(message);
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FilePanel::updateStatus);
    navigate(initialPath);
}

void FilePanel::navigate(const std::filesystem::path &requested) {
    if (requested != model_->path()) {
        model_->setResultPaths(nullptr);
        model_->setViewFilter();
        setProperty("viewFilter", 312);
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
    auto entries = history_;
    for (const auto &value : QSettings().value("directoryHistoryNative").toList()) {
        const auto entry = std::filesystem::path(value.toByteArray().toStdString());
        if (std::find(entries.begin(), entries.end(), entry) == entries.end())
            entries.insert(entries.begin(), entry);
    }
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const auto target = *it;
        auto *action = menu->addAction(displayPath(target));
        connect(action, &QAction::triggered, this, [this, target] {
            navigate(target);
            focusFiles();
        });
    }
    const auto frequent = QSettings().value("frequent/saved").toMap();
    if (!frequent.isEmpty()) {
        menu->addSeparator();
        menu->addSection("Frequently used");
        std::vector<std::pair<int, QString>> ranked;
        for (auto it = frequent.begin(); it != frequent.end(); ++it)
            ranked.emplace_back(it.value().toInt(), it.key());
        std::sort(ranked.rbegin(), ranked.rend());
        int shown = 0;
        for (const auto &[count, encoded] : ranked) {
            Q_UNUSED(count);
            if (++shown > 20)
                break;
            const auto target =
                std::filesystem::path(QByteArray::fromBase64(encoded.toLatin1()).toStdString());
            auto *action = menu->addAction(displayPath(target));
            connect(action, &QAction::triggered, this, [this, target] { navigate(target); });
        }
    }
    menu->popup(pathEdit_->mapToGlobal(QPoint(0, pathEdit_->height())));
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
            QDir::match(mask, displayPath(model_->entry(row)->path.filename())))
            selection.select(model_->index(row, 0), model_->index(row, model_->columnCount() - 1));
    table_->selectionModel()->select(selection,
                                     select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}
void FilePanel::invertSelection() {
    const int first = model_->rowCount() && model_->entry(0)->parent ? 1 : 0;
    if (model_->rowCount() > first)
        table_->selectionModel()->select(
            QItemSelection(model_->index(first, 0),
                           model_->index(model_->rowCount() - 1, model_->columnCount() - 1)),
            QItemSelectionModel::Toggle);
}

std::vector<std::filesystem::path> FilePanel::selectedPaths() const {
    if (property("displayMode") == "tree")
        return path().empty() || path() == path().parent_path() ? std::vector<std::filesystem::path>{}
                                                                : std::vector<std::filesystem::path>{path()};
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
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    status_->setText(QString("%1 / %2 k in %3 / %4 file(s), %5 / %6 dir(s)")
                         .arg(locale.toString(bytes / 1024), locale.toString(total / 1024))
                         .arg(selectedFiles)
                         .arg(files)
                         .arg(selectedDirs)
                         .arg(dirs));
    status_->setFixedHeight(19);
}

bool FilePanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusIn)
        emit activated();
    if (watched == pathEdit_ && event->type() == QEvent::FocusIn)
        pathEdit_->setText(displayPath(path()));
    if (watched == pathEdit_ && event->type() == QEvent::FocusOut && !path().empty())
        pathEdit_->setText(displayPath(path()) + (displayPath(path()).endsWith('/') ? "*.*" : "/*.*"));
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
    if ((watched == table_ || watched == briefList_) && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_A && key->modifiers() == Qt::ControlModifier) {
            table_->clearSelection();
            selectMask("*");
            return true;
        }
        if (key->modifiers() == Qt::NoModifier) {
            switch (key->key()) {
            case Qt::Key_Tab:
                emit switchPanel();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                openCurrent();
                return true;
            case Qt::Key_Backspace:
                parentDirectory();
                return true;
            case Qt::Key_Insert:
            case Qt::Key_Space: {
                const auto current = table_->currentIndex();
                if (current.isValid() && !model_->entry(current.row())->parent) {
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
    setProperty("viewFilter", selected ? 2023 : programs ? 311 : mask.isEmpty() ? 312 : 314);
    std::vector<std::filesystem::path> paths;
    for (const auto &index : table_->selectionModel()->selectedRows())
        if (!model_->entry(index.row())->parent)
            paths.push_back(model_->entry(index.row())->path);
    model_->setViewFilter(mask, programs, selected ? &paths : nullptr);
    refresh();
}
void FilePanel::storeHistory() {
    QVariantList paths;
    for (const auto &path : history_)
        paths.append(QByteArray::fromStdString(path.native()));
    QSettings().setValue("directoryHistoryNative", paths);
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
            filesystem->setRootPath("/");
            tree_ = new QTreeView(this);
            tree_->setObjectName("panelTree");
            tree_->setModel(filesystem);
            tree_->installEventFilter(this);
            tree_->setHeaderHidden(true);
            for (int i = 1; i < 4; ++i)
                tree_->hideColumn(i);
            qobject_cast<QVBoxLayout *>(layout())->insertWidget(2, tree_, 1);
            connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
                    [this, filesystem](const QModelIndex &index) {
                        navigate(nativePath(filesystem->filePath(index)));
                    });
        }
        table_->hide();
        briefList_->hide();
        tree_->show();
        setProperty("displayMode", mode);
        tree_->setCurrentIndex(qobject_cast<QFileSystemModel *>(tree_->model())->index(displayPath(path())));
        tree_->setFocus();
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
