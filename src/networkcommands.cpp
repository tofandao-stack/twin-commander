#include "advancedwindows.h"
#include "backgroundpool.h"
#include "mainwindow.h"
#include "nativesettings.h"
#include "productivitytools.h"
#include "remotetools.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

void MainWindow::registerNetworkCommands() {
    const auto add = [this](int id, const QString &name, std::function<void()> callback) {
        commands_.add("tc_" + QString::number(id), name, {}, std::move(callback));
    };
    const auto connectRemote = [this](bool saved) {
        QString initialUrl = "ftp://";
        if (saved) {
            const auto sites = QSettings().value("ftp/sites").toStringList();
            if (!sites.isEmpty()) {
                bool ok = false;
                const auto url =
                    QInputDialog::getItem(this, "FTP Connect", "Saved server:", sites, 0, false, &ok);
                if (!ok)
                    return;
                initialUrl = url;
            }
        }
        QDialog dialog(this);
        dialog.setWindowTitle("FTP / FTPS New Connection");
        auto *layout = new QFormLayout(&dialog);
        auto *url = new QLineEdit(initialUrl, &dialog), *user = new QLineEdit(&dialog),
             *password = new QLineEdit(&dialog);
        password->setEchoMode(QLineEdit::Password);
        layout->addRow("Server URL:", url);
        layout->addRow("Username (blank for anonymous):", user);
        layout->addRow("Password (not saved):", password);
        auto *save = new QCheckBox("Save server address", &dialog);
        layout->addRow(save);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QUrl address(url->text());
        if ((address.scheme() != "ftp" && address.scheme() != "ftps") || address.host().isEmpty() ||
            !address.userInfo().isEmpty()) {
            QMessageBox::warning(this, "FTP", "Use an ftp:// or ftps:// URL and separate credential fields.");
            return;
        }
        if (save->isChecked()) {
            auto sites = QSettings().value("ftp/sites").toStringList();
            if (!sites.contains(address.toString()))
                sites << address.toString();
            QSettings().setValue("ftp/sites", sites);
        }
        remoteBrowser();
        remote_->show();
        remote_->connectTo(address, {user->text(), password->text()});
    };
    add(550, "FTP Connect", [connectRemote] { connectRemote(true); });
    add(551, "FTP New Connection", [connectRemote] { connectRemote(false); });
    add(552, "FTP Disconnect", [this] {
        if (remote_)
            remote_->disconnectServer();
    });
    commands_.get("tc_552")->setEnabled(false);
    add(553, "FTP Hidden Files", [this] {
        if (remote_)
            remote_->toggleHidden();
    });
    commands_.get("tc_553")->setEnabled(false);
    commands_.get("tc_553")->setCheckable(true);
    add(558, "FTP download from list", [this] {
        const auto file =
            QFileDialog::getOpenFileName(this, "Download URL list", {}, "Text files (*.txt);;All files (*)");
        if (file.isEmpty())
            return;
        try {
            const auto urls =
                QString::fromUtf8(Productivity::readSmall(nativePath(file))).split('\n', Qt::SkipEmptyParts);
            if (urls.size() > 1000)
                throw NativeFs::Error("At most 1000 URLs per list");
            remoteBrowser();
            remote_->show();
            remote_->downloadList(urls);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Download list", QString::fromLocal8Bit(e.what()));
        }
    });
    const auto share = [this] {
        if (share_ && share_->isListening()) {
            QMessageBox::information(this, "Directory share", share_->address().toString());
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle("Share current directory — read-only HTTP");
        auto *layout = new QFormLayout(&dialog);
        layout->addRow(new QLabel(displayPath(active_->path()), &dialog));
        auto *network = new QCheckBox("Listen on network interfaces (otherwise this computer only)", &dialog);
        layout->addRow(network);
        auto *port = new QSpinBox(&dialog);
        port->setRange(0, 65535);
        port->setSpecialValueText("Automatic");
        layout->addRow("TCP port:", port);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        try {
            auto *server = new RemoteTools::ShareServer(active_->path(), this);
            if (!server->start(network->isChecked(), port->value())) {
                const auto error = server->errorString();
                delete server;
                QMessageBox::warning(this, "Share", error);
                return;
            }
            share_ = server;
            commands_.get("tc_515")->setEnabled(true);
            const auto url = server->address();
            QApplication::clipboard()->setText(url.toString());
            QString text = "Read-only share started. URL copied to clipboard:\n" + url.toString();
            if (network->isChecked()) {
                QStringList addresses;
                for (const auto &entry : QNetworkInterface::allAddresses())
                    if (entry.protocol() == QAbstractSocket::IPv4Protocol && !entry.isLoopback())
                        addresses << entry.toString();
                text += "\nFor another PC replace 127.0.0.1 with a network address: " + addresses.join(", ");
            }
            QMessageBox::information(this, "Directory share", text);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Share", QString::fromLocal8Bit(e.what()));
        }
    };
    add(514, "Share current directory", share);
    add(515, "Unshare directory", [this] {
        if (share_) {
            share_->close();
            delete share_;
            share_ = nullptr;
            commands_.get("tc_515")->setEnabled(false);
        }
    });
    commands_.get("tc_515")->setEnabled(false);
    add(2300, "Connection to other PC", [this, share] {
        const auto choice = QMessageBox::question(
            this, "Native PC link",
            "Host a read-only HTTP share? Choose No to download from another Twin Commander share. This uses "
            "HTTP, not the Total Commander PORT wire protocol.",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (choice == QMessageBox::Yes) {
            share();
            return;
        }
        if (choice != QMessageBox::No)
            return;
        bool ok = false;
        const auto url = QInputDialog::getText(
            this, "Receive file", "Full HTTP(S) URL including share token and filename:", QLineEdit::Normal,
            {}, &ok);
        if (!ok || url.isEmpty())
            return;
        const auto name = QFileInfo(QUrl(url).path()).fileName();
        const auto output = QFileDialog::getSaveFileName(this, "Save received file",
                                                         displayPath(active_->path()) + "/" + name);
        if (output.isEmpty())
            return;
        (new ReportDialog(
             "Receive file",
             [url, output](const auto &control) {
                 return RemoteTools::download(QUrl(url), {}, nativePath(output), control);
             },
             this))
            ->show();
    });
    add(512, "Network connections", [this] {
        bool ok = false;
        const auto uri = QInputDialog::getText(
            this, "Network connections",
            "SMB, SFTP, WebDAV or other desktop-supported URI:", QLineEdit::Normal, "smb://", &ok);
        if (!ok || uri.isEmpty())
            return;
        const QUrl url(uri);
        if (!url.isValid() || url.scheme().isEmpty() || !url.userInfo().isEmpty()) {
            QMessageBox::warning(this, "Network", "Invalid URI");
            return;
        }
        if (!NativeSettings::plugins("filesystem").isEmpty()) {
            QPointer<FilePanel> panel(active_);
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, panel, watcher, url] {
                const auto path = watcher->result();
                watcher->deleteLater();
                if (path.startsWith("ERROR:")) {
                    QMessageBox::warning(this, "Network", path);
                    return;
                }
                if (!path.isEmpty() && panel)
                    panel->navigate(nativePath(path));
                else if (!QDesktopServices::openUrl(url))
                    QMessageBox::warning(this, "Network", "No desktop handler could open this URL.");
            });
            watcher->setFuture(QtConcurrent::run(ioPool(), [uri] {
                try {
                    return NativeSettings::mountUrl(uri);
                } catch (const std::exception &e) {
                    return QString("ERROR: ") + QString::fromLocal8Bit(e.what());
                }
            }));
            return;
        }
        if (!QDesktopServices::openUrl(url))
            QMessageBox::warning(this, "Network",
                                 "The desktop could not open this network location. Install a GVfs/KIO "
                                 "backend for its protocol.");
    });
    add(513, "Disconnect network drives", [this] {
        QStringList roots;
        for (const auto &volume : QStorageInfo::mountedVolumes())
            if (volume.fileSystemType().contains("cifs") || volume.fileSystemType().contains("nfs") ||
                volume.fileSystemType().contains("sshfs") || volume.rootPath().contains("gvfs"))
                roots << volume.rootPath();
        bool ok = false;
        const auto target =
            QInputDialog::getItem(this, "Disconnect network drive",
                                  "Mounted path or URI (owned by your session):", roots, 0, true, &ok);
        if (!ok || target.isEmpty())
            return;
        auto *process = new QProcess(this);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int code, QProcess::ExitStatus) {
                    if (code)
                        QMessageBox::warning(this, "Disconnect",
                                             QString::fromUtf8(process->readAllStandardError()));
                    process->deleteLater();
                });
        process->start("gio", {"mount", "-u", target});
    });
    add(2204, "Show admin shares", [this] {
        bool ok = false;
        const auto host = QInputDialog::getText(
            this, "SMB admin share",
            "Windows/Samba host (authentication is handled by your desktop):", QLineEdit::Normal, {}, &ok);
        if (!ok || host.isEmpty())
            return;
        QUrl url;
        url.setScheme("smb");
        url.setHost(host);
        url.setPath("/C$");
        if (!url.isValid() || !QDesktopServices::openUrl(url))
            QMessageBox::warning(this, "SMB", "The desktop could not open this administrative share.");
    });
}

RemoteDialog *MainWindow::remoteBrowser() {
    if (!remote_) {
        remote_ = new RemoteDialog(active_->path(), this);
        commands_.get("tc_553")->setChecked(false);
        connect(remote_, &RemoteDialog::connectionStateChanged, this, [this](bool connected) {
            for (const auto &name : {QString("tc_552"), QString("tc_553")})
                if (auto *action = commands_.get(name))
                    action->setEnabled(connected);
        });
        connect(remote_, &QObject::destroyed, this, [this] {
            for (const auto &name : {QString("tc_552"), QString("tc_553")})
                if (auto *action = commands_.get(name))
                    action->setEnabled(false);
        });
    }
    return remote_;
}
