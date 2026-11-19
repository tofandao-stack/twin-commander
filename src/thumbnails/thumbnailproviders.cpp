#include "filesystem/pathencoding.h"
#include "filesystem/nativefs.h"
#include "thumbnails/thumbnailproviders.h"
#include "settings/applicationpreferences.h"
#include "platform/nativeextensions.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUrl>
using ApplicationPreferences::flag;
using ApplicationPreferences::text;
namespace {
QImage rendered(const std::filesystem::path &path, const QSize &size, const QString &program,
                QStringList arguments, bool stdoutImage) {
    using namespace NativeFs;
    const auto before = inspect(path);
    if (!S_ISREG(before.st_mode) || before.st_size > 512LL * 1024 * 1024)
        return {};
    FileDescriptor input(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    verify(path, input.value, before);
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return {};
    const auto output = scratch.filePath("preview.png");
    const auto pinned = "/proc/self/fd/" + QString::number(input.value);
    for (auto &arg : arguments) {
        arg.replace("{path}", pinned)
            .replace("{output}", output)
            .replace("{size}", QString::number(qMax(size.width(), size.height())))
            .replace("%i", pinned)
            .replace("%u", QUrl::fromLocalFile(pinned).toString())
            .replace("%o", output)
            .replace("%s", QString::number(qMax(size.width(), size.height())));
    }
    QProcess process;
    process.setChildProcessModifier([fd = input.value] { ::fcntl(fd, F_SETFD, 0); });
    process.start(program, arguments);
    if (!process.waitForStarted(2000))
        return {};
    process.closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    QByteArray bytes, errors;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        bytes += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (timer.elapsed() > 5000 || bytes.size() > 8 * 1024 * 1024 || errors.size() > 65536) {
            process.kill();
            process.waitForFinished();
            return {};
        }
    }
    bytes += process.readAllStandardOutput();
    if (process.exitCode() != 0 || process.exitStatus() != QProcess::NormalExit ||
        bytes.size() > 8 * 1024 * 1024)
        return {};
    verify(path, input.value, before);
    if (!stdoutImage) {
        const auto st = inspect(nativePath(output));
        if (!S_ISREG(st.st_mode) || st.st_size > 8 * 1024 * 1024)
            return {};
        QFile f(output);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        bytes = f.readAll();
    }
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto original = reader.size();
    if (!original.isValid() || quint64(original.width()) * original.height() > 16 * 1024 * 1024)
        return {};
    reader.setScaledSize(original.scaled(size, Qt::KeepAspectRatio));
    return reader.read();
}
} // namespace
QImage ThumbnailProviders::render(const std::filesystem::path &path, const QSize &size) {
    const auto name = displayPath(path.filename());
    const auto matches = [&](const char *id) {
        return QDir::match(text(id, "*.*").split(';', Qt::SkipEmptyParts), name);
    };
    try {
        if (flag("ThumbsFromListerPluginsChk") && matches("ThumbsFromListerPluginsEdt"))
            for (const auto &entry : NativeExtensions::plugins("thumbnail")) {
                const auto record = entry.toMap();
                if (!QDir::match(record.value("mask", "*").toString(), name))
                    continue;
                auto image =
                    rendered(path, size, record.value("program").toString(),
                             QProcess::splitCommand(record.value("arguments", "{path}").toString()), true);
                if (!image.isNull())
                    return image;
            }
        if (flag("ThumbsFromExplorerChk") && matches("ThumbsFromExplorerEdt")) {
            const auto mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
            for (const auto &directory :
                 {QString("/usr/share/thumbnailers"), QDir::homePath() + "/.local/share/thumbnailers"})
                for (const auto &file : QDir(directory).entryList({"*.thumbnailer"}, QDir::Files)) {
                    QSettings desktop(QDir(directory).filePath(file), QSettings::IniFormat);
                    desktop.beginGroup("Thumbnailer Entry");
                    if (!desktop.value("MimeType").toString().split(';').contains(mime))
                        continue;
                    auto args = QProcess::splitCommand(desktop.value("Exec").toString());
                    if (args.isEmpty())
                        continue;
                    const auto program = args.takeFirst();
                    auto image = rendered(path, size, program, args, false);
                    if (!image.isNull())
                        return image;
                }
        }
        if (flag("ThumbsFromIrfanXnChk") && matches("ThumbsFromIrfanXnEdt")) {
            auto program = text("thumbnailConverter");
            if (program.isEmpty())
                program = QStandardPaths::findExecutable("magick");
            if (program.isEmpty())
                program = QStandardPaths::findExecutable("convert");
            if (!program.isEmpty())
                return rendered(
                    path, size, program,
                    {"{path}", "-thumbnail", QString("%1x%2").arg(size.width()).arg(size.height()), "png:-"},
                    true);
        }
    } catch (const std::exception &) {
    }
    return {};
}
void ThumbnailProviders::synchronizeCache(const std::filesystem::path &source,
                                          const std::filesystem::path &destination, bool removed) {
    if (!flag("ThumbsCopyDeleteChk"))
        return;
    const auto cache = QSettings().value("thumbnail/cache").toString();
    if (cache.isEmpty())
        return;
    size_t count = 0;
    for (const auto &name : QDir(cache).entryList({"tc-thumb-*.json"}, QDir::Files)) {
        if (++count > 10000)
            break;
        const auto metadata = nativePath(QDir(cache).filePath(name));
        int fd = ::open(metadata.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        NativeFs::FileDescriptor handle(fd);
        const auto st = NativeFs::inspectFd(fd);
        if (!S_ISREG(st.st_mode) || st.st_size > 8192)
            continue;
        QFile f;
        if (!f.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
            continue;
        auto record = QJsonDocument::fromJson(f.readAll()).object();
        if (record["format"] != "TwinCommander-thumbnail-1" ||
            QByteArray::fromBase64(record["path"].toString().toLatin1()) !=
                QByteArray::fromStdString(source.native()))
            continue;
        if (!destination.empty())
            try {
                const auto target = NativeFs::inspect(destination);
                if (!S_ISREG(target.st_mode))
                    continue;
                const int width = record["width"].toInt(QSettings().value("thumbnail/width", 128).toInt()),
                          height = record["height"].toInt(QSettings().value("thumbnail/height", 96).toInt());
                const auto identity = QByteArray::fromStdString(destination.native()) + '\0' +
                                      QByteArray::number(target.st_size) + ":" +
                                      QByteArray::number(qint64(target.st_mtim.tv_sec)) + ":" +
                                      QByteArray::number(qint64(target.st_mtim.tv_nsec)) + ":" +
                                      QByteArray::number(qint64(target.st_ctim.tv_sec)) + ":" +
                                      QByteArray::number(qint64(target.st_ctim.tv_nsec)) + ":" +
                                      QByteArray::number(width) + "x" + QByteArray::number(height);
                const auto key = QString::fromLatin1(
                    QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
                const auto png = QDir(cache).filePath(name.chopped(5) + ".png");
                QImageReader reader(png, "PNG");
                if (reader.size().width() > 512 || reader.size().height() > 512)
                    continue;
                const auto image = reader.read();
                if (image.isNull())
                    continue;
                QSaveFile copied(QDir(cache).filePath("tc-thumb-" + key + ".png"));
                if (copied.open(QIODevice::WriteOnly) && image.save(&copied, "PNG") && copied.commit()) {
                    record["path"] =
                        QString::fromLatin1(QByteArray::fromStdString(destination.native()).toBase64());
                    QSaveFile out(QDir(cache).filePath("tc-thumb-" + key + ".json"));
                    if (out.open(QIODevice::WriteOnly)) {
                        out.write(QJsonDocument(record).toJson());
                        out.commit();
                    }
                }
            } catch (const std::exception &) {
            }
        if (removed) {
            QFile::remove(QDir(cache).filePath(name));
            QFile::remove(QDir(cache).filePath(name.chopped(5) + ".png"));
        }
    }
}
void ThumbnailProviders::pruneUnmounted() {
    if (!flag("RemoveThumbsForCDs"))
        return;
    const auto cache = QSettings().value("thumbnail/cache").toString();
    int count = 0;
    for (const auto &name : QDir(cache).entryList({"tc-thumb-*.json"}, QDir::Files)) {
        if (++count > 10000)
            break;
        QFile f(QDir(cache).filePath(name));
        if (!f.open(QIODevice::ReadOnly) || f.size() > 8192)
            continue;
        const auto record = QJsonDocument::fromJson(f.readAll()).object();
        if (record["format"] != "TwinCommander-thumbnail-1")
            continue;
        const auto root = record["volume"].toString();
        if (!(root.startsWith("/media/") || root.startsWith("/run/media/") || root.startsWith("/mnt/")))
            continue;
        const QStorageInfo volume(root);
        if (volume.rootPath() == root && volume.isReady())
            continue;
        f.close();
        QFile::remove(QDir(cache).filePath(name));
        QFile::remove(QDir(cache).filePath(name.chopped(5) + ".png"));
    }
}
