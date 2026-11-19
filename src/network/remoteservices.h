#pragma once
#include "operations/fileoperations.h"
#include <QByteArray>
#include <QDialog>
#include <QTcpServer>
#include <QUrl>
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
namespace RemoteServices {
struct Credentials {
    QString user, password;
};
struct Result {
    QByteArray bytes;
    QString error;
    bool cancelled = false;
};
Result list(const QUrl &, const Credentials &, bool hidden, const std::shared_ptr<LocalFiles::Control> &);
QString download(const QUrl &, const Credentials &, const std::filesystem::path &,
                 const std::shared_ptr<LocalFiles::Control> &);
QString upload(const QUrl &, const Credentials &, const std::filesystem::path &,
               const std::shared_ptr<LocalFiles::Control> &);
class DirectoryShareServer : public QTcpServer {
  public:
    explicit DirectoryShareServer(const std::filesystem::path &root, QObject *parent = nullptr);
    QUrl address() const;
    bool start(bool publicInterface = false, quint16 port = 0);

  private:
    std::filesystem::path root_;
    QString token_;
};
} // namespace RemoteServices
class FtpBrowserDialog : public QDialog {
    Q_OBJECT
  public:
    FtpBrowserDialog(const std::filesystem::path &local, QWidget *parent = nullptr);
    ~FtpBrowserDialog() override;
    void connectTo(const QUrl &url, const RemoteServices::Credentials &credentials = {});
    void toggleHidden();
    void disconnectServer();
    void downloadList(const QStringList &urls);
  signals:
    void connectionStateChanged(bool connected);

  private:
    void refresh();
    void run(std::function<QString(const std::shared_ptr<LocalFiles::Control> &)> task);
    std::filesystem::path local_;
    QUrl url_;
    RemoteServices::Credentials credentials_;
    std::shared_ptr<LocalFiles::Control> control_;
    bool hidden_ = false, busy_ = false, pendingRefresh_ = false;
    QLineEdit *address_;
    QListWidget *files_;
    QLabel *status_;
};
