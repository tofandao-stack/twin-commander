#pragma once

#include "app/commandregistry.h"
#include "operations/fileoperations.h"
#include "ui/panels/filepanel.h"
#include "operations/jobqueue.h"
#include <QMainWindow>
#include <QPointer>

class QLineEdit;
class QSplitter;
class QTabWidget;
class QCloseEvent;
class QComboBox;
class QLabel;
class QToolBar;
class FilePanelHost;
class QDockWidget;
class FtpBrowserDialog;
namespace RemoteServices {
class DirectoryShareServer;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    ~MainWindow() override;
    explicit MainWindow(const QStringList &initialPaths = {}, QWidget *parent = nullptr);
    FilePanel *activePanel() const;
    void showSearchResults(const std::vector<std::filesystem::path> &paths);

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    void buildMainWindowUi();
    void registerFileCommands();
    void registerExtensionCommands();
    void registerNetworkCommands();
    FtpBrowserDialog *remoteBrowser();
    void configurePreferenceEditors(QDialog *dialog);
    void configurePanelPreferences(QDialog *dialog);
    void applyPanelPreferences(bool panels = true);
    void applyUserCommands();
    void applyViewMode(const QVariantMap &mode);
    void setSeparateTrees(int count);
    void updateQuickView();
    void showAssociations();
    void rebuildStartMenu();
    void reloadCustomMenu();
    void configureAppearance(QDialog *dialog);
    void rebuildButtonBar(bool vertical);
    void configureViewModes(QDialog *dialog);
    void showOptions();
    void showButtonBarEditor(bool vertical);
    void showCommandBrowser();
    void saveSession();
    void applyLayoutOptions();
    void updateUiState();
    void addTab(QTabWidget *tabs, const std::filesystem::path &path);
    void activate(FilePanel *panel);
    void switchPanel();
    void executeCommandLine();
    void invokeParameterized(QAction *action, const QString &parameters);
    void restoreSession(const QStringList &initialPaths);
    FilePanel *oppositePanel() const;
    void runOperation(LocalFiles::Operation operation);
    void startOperation(const LocalFiles::Request &request);
    void openCurrent(bool editable);
    void showProperties();
    void showChecksums();
    void updateBookmarks();
    void showQueue();
    void openArchive();
    void packArchive();
    void showStatistics();
    void findDuplicates();
    void compareCurrentFiles();
    void editPermissions();
    void createLink();
    void showHistory();
    QString journalDirectory() const;
    QList<QAction *> factoryMenuActions_;
    QList<QPointer<QMenu>> customMenus_;
    bool applyingMode_ = false;
    JobQueue *queue_;
    QPointer<JobQueueDialog> queueDialog_;
    QDockWidget *treeLeft_ = nullptr, *treeRight_ = nullptr, *quickView_ = nullptr;
    QPointer<FtpBrowserDialog> remote_;
    RemoteServices::DirectoryShareServer *share_ = nullptr;
    QMenu *bookmarks_ = nullptr;
    FilePanelHost *leftHost_ = nullptr, *rightHost_ = nullptr;
    QWidget *verticalBar_ = nullptr, *commandRow_ = nullptr, *functionBar_ = nullptr;
    QToolBar *buttonBar_ = nullptr;
    QComboBox *commandHistory_ = nullptr;
    QLabel *commandPrompt_ = nullptr;
    QTabWidget *left_;
    QTabWidget *right_;
    QSplitter *splitter_;
    QLineEdit *commandLine_;
    FilePanel *active_ = nullptr;
    CommandRegistry commands_;
};
