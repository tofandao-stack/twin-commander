#include "remotetools.h"
#include "backgroundpool.h"
#include "directorymodel.h"
#include "finalsettings.h"
#include "gapsettings.h"
#include "nativefs.h"
#include "productivitytools.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace RemoteTools {
using namespace NativeFs;
namespace {
QByteArray quoted(const QString &text) {
    if (text.contains('\r') || text.contains('\n') || text.contains(QChar::Null))
        throw Error("Invalid newline in connection settings");
    auto bytes = text.toUtf8();
    bytes.replace("\\", "\\\\");
    bytes.replace("\"", "\\\"");
    return '"' + bytes + '"';
}
Result request(QUrl url, const Credentials &credentials, const QStringList &options,
               const std::shared_ptr<LocalFiles::Control> &control, int inherited = -1) {
    Result result;
    if (!url.isValid() || !QStringList{"ftp", "ftps", "http", "https"}.contains(url.scheme()) ||
        url.host().isEmpty()) {
        result.error = "Unsupported or invalid URL";
        return result;
    }
    if (!url.userInfo().isEmpty()) {
        result.error = "Use the separate username/password fields";
        return result;
    }
    QByteArray configuration;
    try {
        configuration = "url = " + quoted(url.toString(QUrl::FullyEncoded)) + "\n";
        if (!credentials.user.isEmpty())
            configuration += "user = " + quoted(credentials.user + ":" + credentials.password) + "\n";
    } catch (const std::exception &error) {
        result.error = QString::fromLocal8Bit(error.what());
        return result;
    }
    QProcess process;
    if (inherited >= 0)
        process.setChildProcessModifier([inherited] { ::fcntl(inherited, F_SETFD, 0); });
    QStringList arguments{
        "--disable",           "--config",          "-",  "--silent",   "--show-error", "--fail",
        "--globoff",           "--connect-timeout", "15", "--max-time", "300",          "--proto",
        "=ftp,ftps,http,https"};
    try {
        arguments << GapSettings::curlOptions(
            url, options.contains("--output") || options.contains("--upload-file"),
            options.contains("--upload-file"));
    } catch (const std::exception &e) {
        return {{}, QString::fromLocal8Bit(e.what()), false};
    }
    arguments << options;
    QString program = "curl";
    if (FinalSettings::flag("FtpModeZ") && (url.scheme() == "ftp" || url.scheme() == "ftps")) {
        QFile script(":/transport/ftp_modez.py");
        if (!script.open(QIODevice::ReadOnly))
            return {{}, "Missing MODE Z transport", false};
        const bool upload = options.contains("--upload-file"), download = options.contains("--output");
        const int mode = QSettings().value("ftp/mode", 0).toInt();
        const bool ascii =
            mode == 1 || (mode == 2 && QDir::match({"*.txt", "*.csv", "*.html", "*.xml", "*.md"},
                                                   QFileInfo(url.path()).fileName()));
        const QJsonObject request{
            {"url", url.toString(QUrl::FullyEncoded)},
            {"user", credentials.user},
            {"password", credentials.password},
            {"method", upload     ? "upload"
                       : download ? "download"
                                  : "list"},
            {"hidden", options.contains("NLST -a")},
            {"output", download ? options.value(options.indexOf("--output") + 1) : QString()},
            {"fd", inherited},
            {"ascii", ascii},
            {"passive", QSettings().value("ftp/passive", true).toBool()},
            {"range", QSettings().value("ftp/portRange").toBool()},
            {"portFrom", QSettings().value("ftp/portFrom", 50000).toInt()},
            {"portTo", QSettings().value("ftp/portTo", 50100).toInt()},
            {"external", QSettings().value("ftp/externalIP").toBool()},
            {"address", QSettings().value("ftp/portAddress", "-").toString()},
            {"skipLAN", FinalSettings::flag("FtpModeZnotonLAN")},
            {"retries", qBound(0, QSettings().value("ftp/retries", 0).toInt(), 5)},
            {"delay", qBound(0, QSettings().value("ftp/retryDelay", 1).toInt(), 60)},
            {"stall", QSettings().value("ftp/stallRetry").toBool()}};
        configuration = QJsonDocument(request).toJson(QJsonDocument::Compact);
        program = "python3";
        arguments = {"-c", QString::fromUtf8(script.readAll())};
    }
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        result.error = "curl could not start: " + process.errorString();
        return result;
    }
    process.write(configuration);
    process.closeWriteChannel();
    configuration.fill('\0');
    QElapsedTimer timer;
    timer.start();
    QByteArray errors;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        result.bytes += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (control->cancelled || timer.elapsed() > 305000 || result.bytes.size() > 2 * 1024 * 1024 ||
            errors.size() > 65536) {
            result.cancelled = control->cancelled;
            process.kill();
            process.waitForFinished();
            result.error = result.cancelled ? "Cancelled" : "Remote operation limit exceeded";
            break;
        }
    }
    result.bytes += process.readAllStandardOutput();
    errors += process.readAllStandardError();
    if (result.error.isEmpty() && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0))
        result.error = QString::fromUtf8(errors).trimmed().isEmpty()
                           ? QString("curl failed with exit code %1").arg(process.exitCode())
                           : QString::fromUtf8(errors).trimmed();
    if (result.error.isEmpty() && (result.bytes.size() > 2 * 1024 * 1024 || errors.size() > 65536))
        result.error = "Remote output limit exceeded";
    if (!credentials.password.isEmpty())
        result.error.replace(credentials.password, "<redacted>");
    QSettings settings;
    if (settings.value("logging/ftpEnabled").toBool()) {
        const auto path = nativePath(settings.value("logging/ftpFile").toString());
        const int fd =
            ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (fd >= 0) {
            struct stat info {};
            if (::fstat(fd, &info) == 0 && S_ISREG(info.st_mode)) {
                const auto line =
                    (QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " +
                     url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment)
                         .toString() +
                     " " + (result.error.isEmpty() ? "OK" : "FAILED") + "\n")
                        .toUtf8();
                const auto written = ::write(fd, line.data(), line.size());
                Q_UNUSED(written);
            }
            ::close(fd);
        }
    }
    return result;
}
} // namespace
Result list(const QUrl &url, const Credentials &credentials, bool hidden,
            const std::shared_ptr<LocalFiles::Control> &control) {
    if (url.scheme() != "ftp" && url.scheme() != "ftps")
        return {{}, "Directory listing requires FTP/FTPS", false};
    return request(url, credentials,
                   hidden ? QStringList{"--list-only", "--request", "NLST -a"} : QStringList{"--list-only"},
                   control);
}
QString download(const QUrl &url, const Credentials &credentials, const fs::path &output,
                 const std::shared_ptr<LocalFiles::Control> &control) {
    const auto stage = output.parent_path() /
                       (".tc-download-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    check(::mkdir(stage.c_str(), 0700));
    try {
        const auto file = stage / "payload";
        const auto result =
            request(url, credentials,
                    {"--output", displayPath(file), "--max-filesize", "4294967296", "--location",
                     "--max-redirs", "5", "--proto-redir", "=http,https,ftp,ftps"},
                    control);
        if (!result.error.isEmpty())
            throw Error(result.error.toStdString());
        LocalFiles::checkpoint(control);
        const auto st = inspect(file);
        if (!S_ISREG(st.st_mode))
            throw Error("Download did not produce a regular file");
        if (static_cast<quint64>(st.st_size) > 4ULL * 1024 * 1024 * 1024)
            throw Error("Download size limit exceeded");
        Fd parent(::open(output.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        renameExclusive(parent.value, (stage.filename() / "payload").native(), output.filename().native());
        control->bytes += st.st_size;
        ++control->completed;
        std::error_code error;
        fs::remove(stage, error);
        return "Downloaded " + displayPath(output);
    } catch (...) {
        std::error_code error;
        fs::remove_all(stage, error);
        throw;
    }
}
QString upload(const QUrl &url, const Credentials &credentials, const fs::path &path,
               const std::shared_ptr<LocalFiles::Control> &control) {
    if (url.scheme() != "ftp" && url.scheme() != "ftps")
        throw Error("Uploads require FTP/FTPS");
    const auto before = inspect(path);
    if (!S_ISREG(before.st_mode))
        throw Error("Upload requires a regular file");
    Fd input(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (!same(before, inspectFd(input.value)))
        throw Error("File changed before upload");
    const auto result =
        request(url, credentials, {"--upload-file", "/proc/self/fd/" + QString::number(input.value)}, control,
                input.value);
    if (!result.error.isEmpty())
        throw Error((result.error + "; a remote partial file may remain").toStdString());
    verify(path, input.value, before);
    ++control->completed;
    return "Uploaded " + displayPath(path);
}
ShareServer::ShareServer(const fs::path &root, QObject *parent)
    : QTcpServer(parent), root_(fs::canonical(root)),
      token_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    setMaxPendingConnections(16);
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (hasPendingConnections()) {
            auto *socket = nextPendingConnection();
            if (findChildren<QTcpSocket *>().size() > 16) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            auto input = std::make_shared<QByteArray>();
            auto handled = std::make_shared<bool>(false);
            QTimer::singleShot(10000, socket, [socket, handled] {
                if (!*handled)
                    socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, input, handled] {
                if (*handled)
                    return;
                *input += socket->readAll();
                const auto end = input->indexOf("\r\n\r\n");
                if (input->size() > 8192) {
                    socket->abort();
                    return;
                }
                if (end < 0)
                    return;
                *handled = true;
                const auto reply = [socket](int code, const QByteArray &content,
                                            const QByteArray &type = "text/plain; charset=utf-8") {
                    socket->write("HTTP/1.1 " + QByteArray::number(code) +
                                  " Result\r\nConnection: close\r\nContent-Type: " + type +
                                  "\r\nX-Content-Type-Options: nosniff\r\nContent-Length: " +
                                  QByteArray::number(content.size()) + "\r\n\r\n" + content);
                    socket->disconnectFromHost();
                };
                const auto request = input->left(input->indexOf("\r\n")).split(' ');
                if (request.size() != 3 || request[0] != "GET") {
                    reply(405, "GET required");
                    return;
                }
                const auto target = QUrl::fromEncoded(request[1]);
                const auto relative = target.path(QUrl::FullyEncoded).toLatin1();
                if (!relative.startsWith("/" + token_.toLatin1() + "/")) {
                    reply(403, "Invalid share token");
                    return;
                }
                const auto leaf = relative.mid(token_.size() + 2);
                const auto raw = QByteArray::fromPercentEncoding(leaf).toStdString();
                fs::path route(raw);
                if (route.is_absolute() || raw.find('\0') != std::string::npos) {
                    reply(400, "Invalid path");
                    return;
                }
                try {
                    auto handle = std::make_shared<Fd>(
                        ::open(root_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                    fs::path current = root_;
                    for (auto i = route.begin(); i != route.end(); ++i) {
                        const auto part = i->native();
                        if (part == "..")
                            throw Error("Parent traversal refused");
                        if (part.empty() || part == ".")
                            continue;
                        const bool last = std::next(i) == route.end();
                        auto next = std::make_shared<Fd>(::openat(handle->value, part.c_str(),
                                                                  O_RDONLY | O_NOFOLLOW | O_NONBLOCK |
                                                                      O_CLOEXEC | (last ? 0 : O_DIRECTORY)));
                        handle = next;
                        current /= *i;
                    }
                    const auto st = inspectFd(handle->value);
                    if (S_ISDIR(st.st_mode)) {
                        QString html = "<!doctype html><meta charset=utf-8><title>Shared "
                                       "directory</title><h1>Shared directory</h1>";
                        int count = 0;
                        for (const auto &entry : fs::directory_iterator(fs::path("/proc/self/fd") /
                                                                        std::to_string(handle->value))) {
                            if (++count > 10000)
                                throw Error("Directory listing limit exceeded");
                            const auto info = entry.symlink_status();
                            if (!fs::is_regular_file(info) && !fs::is_directory(info))
                                continue;
                            const auto name = displayPath(entry.path().filename());
                            const auto suffix = fs::is_directory(info) ? "/" : "";
                            const auto encoded = QByteArray::fromStdString(entry.path().filename().native())
                                                     .toPercentEncoding();
                            const auto base =
                                target.path(QUrl::FullyEncoded) + (target.path().endsWith('/') ? "" : "/");
                            html += "<p><a href=\"" + base + QString::fromLatin1(encoded) + suffix + "\">" +
                                    name.toHtmlEscaped() + suffix + "</a></p>";
                        }
                        reply(200, html.toUtf8(), "text/html; charset=utf-8");
                        return;
                    }
                    if (!S_ISREG(st.st_mode))
                        throw Error("Only regular files are shared");
                    socket->write(
                        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
                        "application/octet-stream\r\nContent-Disposition: attachment\r\nContent-Length: " +
                        QByteArray::number(static_cast<qint64>(st.st_size)) + "\r\n\r\n");
                    auto remaining = std::make_shared<qint64>(st.st_size);
                    auto pump = std::make_shared<std::function<void()>>();
                    QPointer<QTcpSocket> guard(socket);
                    *pump = [guard, handle, remaining] {
                        if (!guard)
                            return;
                        while (*remaining && guard->bytesToWrite() < 256 * 1024) {
                            std::array<char, 65536> buffer{};
                            const auto size = ::read(handle->value, buffer.data(),
                                                     std::min<qint64>(buffer.size(), *remaining));
                            if (size < 0 && errno == EINTR)
                                continue;
                            if (size <= 0) {
                                guard->abort();
                                return;
                            }
                            guard->write(buffer.data(), size);
                            *remaining -= size;
                        }
                        if (!*remaining)
                            guard->disconnectFromHost();
                    };
                    connect(socket, &QTcpSocket::bytesWritten, socket, [pump](qint64) { (*pump)(); });
                    (*pump)();
                } catch (const std::exception &error) {
                    reply(403, QByteArray(error.what()));
                }
            });
        }
    });
}
bool ShareServer::start(bool publicInterface, quint16 port) {
    return listen(publicInterface ? QHostAddress::Any : QHostAddress::LocalHost, port);
}
QUrl ShareServer::address() const {
    QUrl url;
    url.setScheme("http");
    url.setHost(serverAddress() == QHostAddress::Any ? "127.0.0.1" : serverAddress().toString());
    url.setPort(serverPort());
    url.setPath("/" + token_ + "/");
    return url;
}
} // namespace RemoteTools
RemoteDialog::RemoteDialog(const std::filesystem::path &local, QWidget *parent)
    : QDialog(parent), local_(local), address_(new QLineEdit(this)), files_(new QListWidget(this)),
      status_(new QLabel(this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("remoteBrowser");
    setWindowTitle("FTP / FTPS");
    resize(740, 500);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(address_);
    layout->addWidget(files_);
    status_->setWordWrap(true);
    status_->setMaximumHeight(120);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto *changes = new QTimer(this);
    changes->setInterval(1500);
    connect(changes, &QTimer::timeout, this, [this] {
        if (!FinalSettings::flag("IdAutoReUpload") || busy_ || url_.isEmpty())
            return;
        auto tracked = property("trackedDownloads").toMap();
        for (const auto &entryKey : tracked.keys()) {
            auto record = tracked.value(entryKey).toMap();
            const auto path =
                std::filesystem::path(QByteArray::fromBase64(entryKey.toLatin1()).toStdString());
            try {
                const auto st = NativeFs::inspect(path);
                if (!S_ISREG(st.st_mode))
                    continue;
                const auto stamp = QString("%1:%2:%3:%4:%5")
                                       .arg(st.st_ino)
                                       .arg(st.st_size)
                                       .arg(st.st_mtim.tv_sec)
                                       .arg(st.st_mtim.tv_nsec)
                                       .arg(st.st_ctim.tv_nsec);
                if (stamp == record.value("stamp").toString())
                    continue;
                if (stamp != record.value("candidate").toString()) {
                    record["candidate"] = stamp;
                    tracked[entryKey] = record;
                    setProperty("trackedDownloads", tracked);
                    continue;
                }
                record["stamp"] = stamp;
                record.remove("candidate");
                tracked[entryKey] = record;
                setProperty("trackedDownloads", tracked);
                const auto target = record.value("url").toUrl();
                run([path, target, credentials = credentials_](const auto &control) {
                    return RemoteTools::upload(target, credentials, path, control);
                });
                return;
            } catch (const std::exception &) {
            }
        }
    });
    changes->start();
    auto *buttons = new QHBoxLayout;
    for (const auto &label : {QString("Open directory"), QString("Parent"), QString("Download"),
                              QString("Upload"), QString("Refresh"), QString("Stop")}) {
        auto *button = new QPushButton(label, this);
        buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, label] {
            if (label == "Stop") {
                setProperty("trackedDownloads", QVariantMap{});
                if (control_)
                    control_->cancelled = true;
                return;
            }
            if (busy_)
                return;
            if (label == "Parent") {
                auto path = url_.path();
                if (path.endsWith('/'))
                    path.chop(1);
                url_.setPath(path.left(path.lastIndexOf('/') + 1));
                refresh();
                return;
            }
            if (label == "Refresh") {
                refresh();
                return;
            }
            if (label == "Upload") {
                const auto path =
                    QFileDialog::getOpenFileName(this, "Upload regular file", displayPath(local_));
                if (path.isEmpty())
                    return;
                const auto name = QFileInfo(path).fileName();
                if (!files_->findItems(name, Qt::MatchExactly).isEmpty() &&
                    QMessageBox::question(this, "Overwrite remote file",
                                          name + " already exists. Replace it?") != QMessageBox::Yes)
                    return;
                if (name == "." || name == ".." || name.contains('/') || name.contains(QChar::Null)) {
                    status_->setText("Remote name is not a safe single filename");
                    return;
                }
                auto target = url_;
                target.setPath(url_.path() + name);
                run([target, credentials = credentials_, path](const auto &control) {
                    return RemoteTools::upload(target, credentials, nativePath(path), control);
                });
                return;
            }
            auto *item = files_->currentItem();
            if (!item)
                return;
            const auto name = item->text();
            if (name == "." || name == ".." || name.contains('/') || name.contains('\\'))
                return;
            auto target = url_;
            target.setPath(url_.path() + name);
            if (label == "Open directory") {
                target.setPath(target.path() + "/");
                url_ = target;
                refresh();
            } else {
                const auto output = QSettings().value("ftp/downloadTarget", 0).toInt() == 1
                                        ? displayPath(local_ / nativePath(name))
                                        : QFileDialog::getSaveFileName(this, "Download to a new file",
                                                                       displayPath(local_) + "/" + name);
                if (output.isEmpty())
                    return;
                setProperty("pendingDownloadPath", QFile::encodeName(output));
                setProperty("pendingDownloadUrl", target);
                run([target, credentials = credentials_, output](const auto &control) {
                    return RemoteTools::download(target, credentials, nativePath(output), control);
                });
            }
        });
    }
    layout->addLayout(buttons);
    connect(address_, &QLineEdit::returnPressed, this, [this] {
        const QUrl next(address_->text());
        if (next.host() != url_.host() || next.scheme() != url_.scheme()) {
            status_->setText("Use New Connection to change servers.");
            return;
        }
        url_ = next;
        refresh();
    });
}
RemoteDialog::~RemoteDialog() {
    emit connectionStateChanged(false);
    if (control_)
        control_->cancelled = true;
    credentials_.password.fill(QChar::Null);
}
void RemoteDialog::connectTo(const QUrl &url, const RemoteTools::Credentials &credentials) {
    if (busy_) {
        status_->setText("Stop the current operation before connecting to another server.");
        return;
    }
    setProperty("trackedDownloads", QVariantMap{});
    setProperty("lastGoodUrl", QUrl());
    setProperty("lastTransfer", QString());
    url_ = url;
    if (!url_.path().endsWith('/'))
        url_.setPath(url_.path() + "/");
    credentials_ = credentials;
    refresh();
}
void RemoteDialog::refresh() {
    if (busy_)
        return;
    busy_ = true;
    control_ = std::make_shared<LocalFiles::Control>();
    address_->setText(url_.toString());
    status_->setText("Connecting…");
    auto *watcher = new QFutureWatcher<RemoteTools::Result>(this);
    connect(watcher, &QFutureWatcher<RemoteTools::Result>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        if (url_.isEmpty()) {
            files_->clear();
            status_->setText("Disconnected");
            emit connectionStateChanged(false);
            return;
        }
        if (!result.error.isEmpty()) {
            const auto previous = property("lastGoodUrl").toUrl();
            if (previous.isValid() && !previous.isEmpty()) {
                url_ = previous;
                address_->setText(url_.toString());
            }
            emit connectionStateChanged(previous.isValid() && !previous.isEmpty());
            status_->setText(result.error.left(2048));
            return;
        }
        setProperty("lastGoodUrl", url_);
        emit connectionStateChanged(true);
        files_->clear();
        bool truncated = false;
        for (auto name : QString::fromUtf8(result.bytes).split('\n', Qt::SkipEmptyParts)) {
            if (name.endsWith('\r'))
                name.chop(1);
            if (name == "." || name == ".." || (!hidden_ && name.startsWith('.')))
                continue;
            if (files_->count() >= 10000) {
                truncated = true;
                break;
            }
            files_->addItem(name);
        }
        status_->setText(
            QString("%1 remote names%2").arg(files_->count()).arg(truncated ? " (listing truncated)" : "") +
            (property("lastTransfer").toString().isEmpty() ? QString()
                                                           : "\n" + property("lastTransfer").toString()));
        if (pendingRefresh_) {
            pendingRefresh_ = false;
            refresh();
        }
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), RemoteTools::list, url_, credentials_, hidden_, control_));
}
void RemoteDialog::run(std::function<QString(const std::shared_ptr<LocalFiles::Control> &)> task) {
    if (busy_)
        return;
    busy_ = true;
    control_ = std::make_shared<LocalFiles::Control>();
    status_->setText("Transferring…");
    QProgressDialog *progress = nullptr;
    if (!QSettings().value("ftp/background", true).toBool()) {
        progress = new QProgressDialog("Remote transfer", "Cancel", 0, 0, this);
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setMinimumDuration(0);
        connect(progress, &QProgressDialog::canceled, this,
                [control = control_] { control->cancelled = true; });
        progress->show();
    }
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, progress] {
        if (progress) {
            progress->close();
            progress->deleteLater();
        }
        if (watcher->result().startsWith("Downloaded ") && FinalSettings::flag("IdAutoReUpload") &&
            !property("pendingDownloadPath").toByteArray().isEmpty())
            try {
                const auto bytes = property("pendingDownloadPath").toByteArray();
                const auto st = NativeFs::inspect(std::filesystem::path(bytes.toStdString()));
                auto tracked = property("trackedDownloads").toMap();
                if (tracked.size() < 32) {
                    tracked[QString::fromLatin1(bytes.toBase64())] =
                        QVariantMap{{"url", property("pendingDownloadUrl")},
                                    {"stamp", QString("%1:%2:%3:%4:%5")
                                                  .arg(st.st_ino)
                                                  .arg(st.st_size)
                                                  .arg(st.st_mtim.tv_sec)
                                                  .arg(st.st_mtim.tv_nsec)
                                                  .arg(st.st_ctim.tv_nsec)}};
                    setProperty("trackedDownloads", tracked);
                }
            } catch (const std::exception &) {
            }
        setProperty("pendingDownloadPath", QByteArray());
        setProperty("pendingDownloadUrl", QUrl());
        setProperty("lastTransfer", watcher->result().left(2048));
        status_->setText(watcher->result().left(2048));
        watcher->deleteLater();
        busy_ = false;
        if (pendingRefresh_) {
            pendingRefresh_ = false;
            if (!url_.isEmpty())
                refresh();
        }
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [task, control = control_] {
        try {
            return task(control);
        } catch (const LocalFiles::Cancelled &) {
            return QString("Cancelled");
        } catch (const std::exception &e) {
            return QString::fromLocal8Bit(e.what());
        }
    }));
}
void RemoteDialog::toggleHidden() {
    if (url_.isEmpty())
        return;
    hidden_ = !hidden_;
    if (busy_) {
        pendingRefresh_ = true;
        return;
    }
    refresh();
}
void RemoteDialog::disconnectServer() {
    if (control_)
        control_->cancelled = true;
    credentials_.password.fill(QChar::Null);
    url_ = QUrl();
    pendingRefresh_ = false;
    setProperty("trackedDownloads", QVariantMap{});
    setProperty("lastGoodUrl", QUrl());
    files_->clear();
    status_->setText("Disconnected");
    emit connectionStateChanged(false);
}
void RemoteDialog::downloadList(const QStringList &urls) {
    const auto directory = local_;
    run([urls, directory](const auto &control) {
        QStringList result;
        for (const auto &line : urls) {
            const QUrl url(line.trimmed());
            if (url.isEmpty())
                continue;
            const auto name = QFileInfo(url.path()).fileName();
            if (name.isEmpty() || name == "." || name == "..")
                throw NativeFs::Error("URL has no safe filename");
            result << RemoteTools::download(url, {}, directory / nativePath(name), control);
        }
        return result.join('\n');
    });
}
