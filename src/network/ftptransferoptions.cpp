#include "network/ftptransferoptions.h"
#include "filesystem/nativefs.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

QStringList FtpTransferOptions::arguments(const QUrl &url, bool transfer, bool upload) {
    QSettings s;
    QStringList args;
    if (url.scheme() == "ftp" || url.scheme() == "ftps") {
        if (s.value("ftp/passive", true).toBool())
            args << "--ftp-pasv";
        else {
            QString address = s.value("ftp/externalIP").toBool() ? s.value("ftp/portAddress", "-").toString()
                                                                 : QString("-");
            if (address.isEmpty() || address.contains('\n') || address.contains('\r') ||
                address.contains(QChar::Null))
                throw NativeFs::Error("Invalid active FTP address");
            if (s.value("ftp/portRange").toBool()) {
                const int from = s.value("ftp/portFrom", 50000).toInt(),
                          to = s.value("ftp/portTo", 50100).toInt();
                if (from < 1024 || to > 65535 || from > to)
                    throw NativeFs::Error("Invalid active FTP port range");
                address += QString(":%1-%2").arg(from).arg(to);
            }
            args << "--ftp-port" << address;
        }
        if (transfer) {
            const int mode = s.value("ftp/mode", 0).toInt();
            if (mode == 1 || (mode == 2 && QDir::match({"*.txt", "*.csv", "*.html", "*.xml", "*.md"},
                                                       QFileInfo(url.path()).fileName())))
                args << "--use-ascii";
        }
    }
    // Retries are safe for listings and staged downloads. Never retry a mutating upload automatically.
    if (!upload) {
        args << "--retry"
             << QString::number(qBound(transfer && s.value("ftp/stallRetry").toBool() ? 1 : 0,
                                       s.value("ftp/retries", 0).toInt(), 5))
             << "--retry-delay" << QString::number(qBound(0, s.value("ftp/retryDelay", 1).toInt(), 60))
             << "--retry-max-time" << "240";
        if (transfer && s.value("ftp/stallRetry").toBool())
            args << "--speed-limit" << "1" << "--speed-time"
                 << QString::number(qBound(1, s.value("ftp/stallSeconds", 30).toInt(), 300))
                 << "--retry-all-errors";
    }
    return args;
}
