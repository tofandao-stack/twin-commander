#pragma once
#include "archives/archives.h"
#include "operations/batchrename.h"
#include "filesystem/fileanalysis.h"
#include "operations/jobqueue.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class TaskReportDialog : public QDialog {
    Q_OBJECT
public:
    TaskReportDialog(const QString &title, std::function<QString(const std::shared_ptr<LocalFiles::Control> &)> task, QWidget *parent = nullptr);
    ~TaskReportDialog() override;
signals:
    void ready();
private:
    std::shared_ptr<LocalFiles::Control> control_;
};
class ArchiveBrowserDialog : public QDialog {
    Q_OBJECT
public:
    ArchiveBrowserDialog(const std::filesystem::path &path, const std::filesystem::path &destination, JobQueue *queue, QWidget *parent = nullptr);
    ~ArchiveBrowserDialog() override;
private:
    void populate();
    void extract(bool selected);
    std::filesystem::path path_, destination_;
    JobQueue *queue_;
    Archives::Listing listing_;
    std::shared_ptr<LocalFiles::Control> control_;
    QLineEdit *filter_;
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *all_, *selected_;
};
class BatchRenameDialog : public QDialog {
    Q_OBJECT
public:
    ~BatchRenameDialog() override;
    BatchRenameDialog(const std::vector<std::filesystem::path> &paths, JobQueue *queue, const QString &journalDirectory, QWidget *parent = nullptr);
private:
    void preview();
    std::vector<std::filesystem::path> paths_;
    JobQueue *queue_;
    QString journalDirectory_;
    BatchRename::Plan plan_;
    std::shared_ptr<BatchRename::Plan> inverse_;
    std::shared_ptr<std::atomic_bool> previewCancellation_;
    bool busy_ = false;
    quint64 generation_ = 0;
    QLineEdit *pattern_, *extension_, *find_, *replace_;
    QCheckBox *regex_;
    QComboBox *case_;
    QSpinBox *start_, *step_, *width_;
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *apply_;
    QPushButton *undo_ = nullptr, *next_ = nullptr;
};
class DirectoryComparisonDialog : public QDialog {
    Q_OBJECT
public:
    DirectoryComparisonDialog(const std::filesystem::path &left, const std::filesystem::path &right, JobQueue *queue,
                  const QString &journalDirectory, QWidget *parent = nullptr);
    ~DirectoryComparisonDialog() override;
private:
    void scan();
    void populate();
    void apply();
    std::filesystem::path left_, right_;
    JobQueue *queue_;
    QString journalDirectory_;
    FileAnalysis::DirectoryComparison plan_;
    std::vector<size_t> rows_;
    std::vector<bool> checked_;
    std::shared_ptr<LocalFiles::Control> control_;
    QCheckBox *contents_, *equal_;
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *scan_, *apply_;
};
