#pragma once
#include "search.h"
#include <QDialog>
#include <QByteArray>

class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QCheckBox;
class QTableWidget;
class QPushButton;
class QComboBox;
class QCloseEvent;
class QDateTimeEdit;

class FileWindow : public QDialog {
    Q_OBJECT
public:
    FileWindow(const std::filesystem::path &path, bool editable, QWidget *parent = nullptr);
    void reject() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void render();
    void save();
    bool confirmClose();
    std::filesystem::path path_;
    bool editable_;
    bool ready_ = false;
    bool saving_ = false;
    QByteArray original_;
    QPlainTextEdit *text_;
    QLabel *info_;
    QLabel *image_;
    QComboBox *mode_;
};

class SearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit SearchDialog(const std::filesystem::path &root, QWidget *parent = nullptr);
    ~SearchDialog() override;
signals:
    void revealRequested(const std::filesystem::path &path);
    void resultsRequested(const std::vector<std::filesystem::path> &paths);
private:
    void start();
    std::filesystem::path root_;
    std::vector<std::filesystem::path> paths_;
    std::shared_ptr<LocalFiles::Control> control_;
    QLineEdit *pattern_;
    QLineEdit *content_;
    QCheckBox *recursive_;
    QCheckBox *hidden_;
    QCheckBox *case_, *nameRegex_, *contentRegex_, *afterEnabled_, *beforeEnabled_;
    QLineEdit *minBytes_, *maxBytes_;
    QDateTimeEdit *after_, *before_;
    QTableWidget *results_;
    QLabel *status_;
    QPushButton *start_;
};
