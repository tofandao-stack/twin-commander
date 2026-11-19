#pragma once

#include "directorymodel.h"
#include "fileoperations.h"
#include <QWidget>
#include <unordered_set>

class QLabel;
class QLineEdit;
class QTableView;
class QListView;
class QTreeView;
class QFileSystemWatcher;
class QTimer;

class FilePanel : public QWidget {
    Q_OBJECT

  public:
    ~FilePanel() override;
    explicit FilePanel(const std::filesystem::path &initialPath, QWidget *parent = nullptr);
    void navigate(const std::filesystem::path &path);
    void parentDirectory();
    void back();
    void refresh();
    void applyPreferences();
    void filterView(const QString &mask = {}, bool programs = false, bool selected = false);
    void storeHistory();
    void clearHistory();
    void toggleHidden();
    void focusFiles();
    void focusPath();
    void focusFilter(bool restoreLast = false);
    void setBrief(bool brief);
    bool isBrief() const;
    void setDisplayMode(const QString &mode);
    void saveSelection();
    void restoreSelection();
    void selectPaths(const std::vector<std::filesystem::path> &paths);
    void clearFilter();
    void showHistory();
    void forward();
    void setActive(bool active);
    void selectMask(const QString &mask, bool select = true);
    void invertSelection();
    std::vector<std::filesystem::path> selectedPaths() const;
    void reveal(const std::filesystem::path &path);
    std::filesystem::path path() const;
    QTableView *table() const;
    DirectoryModel *model() const;

  signals:
    void activated();
    void switchPanel();
    void pathChanged(const QString &path);
    void errorOccurred(const QString &message);
    void openFileRequested(const std::filesystem::path &path);
    void hotlistRequested();
    void executeCommandRequested();
    void filesDropped(const std::vector<std::filesystem::path> &paths, bool move);
    void treeDirectoryRequested(const std::filesystem::path &path);
    void contextMenuRequested(const QPoint &position);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void openCurrent();
    void updateBreadcrumbs();
    void countDirectory(const std::filesystem::path &path);
    void preloadTree();
    void updateStatus();
    QFileSystemWatcher *watcher_;
    QTimer *refreshTimer_;
    QWidget *breadcrumbs_ = nullptr;
    std::shared_ptr<LocalFiles::Control> treeControl_;
    QPoint dragStart_;
    bool dragMarked_ = false;
    QString quickText_;
    QTimer *rightMenuTimer_ = nullptr;
    qint64 quickTime_ = 0;
    int refreshCursor_ = 0;
    bool refreshPending_ = false;
    bool autoRefreshing_ = false;
    DirectoryModel *model_;
    QTableView *table_;
    QListView *briefList_;
    QTreeView *tree_ = nullptr;
    bool brief_ = false;
    QString lastFilter_;
    std::vector<std::filesystem::path> savedSelection_;
    std::filesystem::path savedDirectory_;
    std::vector<std::filesystem::path> forward_;
    QLineEdit *pathEdit_;
    QLabel *status_;
    QLineEdit *filter_;
    std::filesystem::path revealPath_;
    std::vector<std::filesystem::path> history_;
    bool backRequested_ = false;
    bool historyTraversal_ = false;
    std::filesystem::path previousPath_;
    std::filesystem::path previousCursor_;
    std::unordered_set<std::filesystem::path> previousSelection_;
    int previousRow_ = 0;
    int previousColumn_ = 0;
};
