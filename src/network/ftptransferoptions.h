#pragma once
#include <QStringList>
#include <QUrl>
namespace FtpTransferOptions {
QStringList arguments(const QUrl &url, bool transfer, bool upload);
}
