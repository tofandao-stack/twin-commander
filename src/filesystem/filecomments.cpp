#include "filesystem/filecomments.h"
#include "filesystem/nativefs.h"
#include "filesystem/pathencoding.h"
#include "settings/applicationpreferences.h"
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>
#include <iconv.h>
#include <sys/xattr.h>

namespace {
QByteArray recode(const QByteArray &data, bool encode) {
    iconv_t c = iconv_open(encode ? "CP437" : "UTF-8", encode ? "UTF-8" : "CP437");
    if (c == iconv_t(-1))
        throw std::runtime_error("CP437 converter unavailable");
    QByteArray out(data.size() * 4 + 16, 0);
    char *input = const_cast<char *>(data.constData()), *output = out.data();
    size_t left = data.size(), space = out.size();
    const auto result = iconv(c, &input, &left, &output, &space);
    iconv_close(c);
    if (result == size_t(-1))
        throw std::runtime_error("Comment text cannot be represented in the selected encoding");
    out.resize(out.size() - space);
    return out;
}
QByteArray small(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return {};
    NativeFs::FileDescriptor input(fd);
    const auto st = NativeFs::inspectFd(fd);
    if (!S_ISREG(st.st_mode) || st.st_size > 2 * 1024 * 1024)
        return {};
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return {};
    return file.read(2 * 1024 * 1024);
}
QMap<QString, QString> legacy(const std::filesystem::path &dir, const QString &file) {
    auto data = small(dir / nativePath(file));
    if (ApplicationPreferences::flag("IdConf_comments_doscharset"))
        data = recode(data, false);
    QMap<QString, QString> values;
    for (const auto &line : QString::fromUtf8(data).split('\n')) {
        if (values.size() >= 10000)
            break;
        QString name, value;
        if (line.startsWith('"')) {
            const auto end = line.indexOf('"', 1);
            if (end < 0)
                continue;
            name = line.mid(1, end - 1);
            value = line.mid(end + 1).trimmed();
        } else {
            const auto end = line.indexOf(QRegularExpression("\\s"));
            if (end < 0)
                continue;
            name = line.left(end);
            value = line.mid(end).trimmed();
        }
        if (!name.isEmpty())
            values[name] = value;
    }
    return values;
}
QString xattr(const std::filesystem::path &path, const char *name) {
    const auto size = ::lgetxattr(path.c_str(), name, nullptr, 0);
    if (size < 0 || size > 65536)
        return {};
    QByteArray value(size, 0);
    if (::lgetxattr(path.c_str(), name, value.data(), value.size()) < 0)
        return {};
    return QString::fromUtf8(value);
}
} // namespace
using ApplicationPreferences::flag;
using ApplicationPreferences::text;

QString FileComments::read(const std::filesystem::path &path) {
    const auto format = text("IdPreferredCommentFormat", "Native xattr");
    const auto native = xattr(path, "user.twincommander.comment");
    QString value = format == "Native xattr"
                        ? native
                        : legacy(path.parent_path(), format).value(displayPath(path.filename()));
    if ((flag("IdConf_DescriptComments") || format != "Native xattr") && value.isEmpty()) {
        value = legacy(path.parent_path(), format == "files.bbs" ? "files.bbs" : "descript.ion")
                    .value(displayPath(path.filename()));
        if (value.isEmpty() && flag("IdConf_comments_bothformats")) {
            value = legacy(path.parent_path(), format == "files.bbs" ? "descript.ion" : "files.bbs")
                        .value(displayPath(path.filename()));
            if (value.isEmpty())
                value = native;
        }
    }
    if (value.isEmpty() && flag("IdConf_ExplorerComments")) {
        value = xattr(path, "user.xdg.comment");
        if (value.isEmpty())
            value = xattr(path, "user.comment");
    }
    return value;
}
void FileComments::write(const std::filesystem::path &path, const QString &value) {
    if (value.toUtf8().size() > 65536)
        throw std::runtime_error("Comment limit is 64 KiB");
    const auto format = text("IdPreferredCommentFormat", "Native xattr");
    if (format == "Native xattr") {
        const auto data = value.toUtf8();
        NativeFs::check(::lsetxattr(path.c_str(), "user.twincommander.comment", data.data(), data.size(), 0));
        return;
    }
    const auto name = displayPath(path.filename());
    if (nativePath(name) != path.filename() || name.contains('"') || name.contains('\n') ||
        value.contains('\n') || value.contains('\r'))
        throw std::runtime_error("Legacy comments require single-line representable names and comments");
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    const auto sidecar = path.parent_path() / nativePath(format);
    struct stat st {};
    if (::lstat(sidecar.c_str(), &st) == 0 && (!S_ISREG(st.st_mode) || st.st_size > 2 * 1024 * 1024))
        throw std::runtime_error("Legacy comment sidecar is not a bounded regular file");
    auto raw = small(sidecar);
    if (st.st_size > 0 && raw.isEmpty())
        throw std::runtime_error("Legacy comment sidecar could not be read");
    if (flag("IdConf_comments_doscharset"))
        raw = recode(raw, false);
    const auto original = QString::fromUtf8(raw);
    if (original.toUtf8() != raw)
        throw std::runtime_error("Legacy sidecar contains invalid UTF-8");
    QStringList lines;
    const QString quoted = '"' + name + '"';
    for (const auto &line : original.split('\n')) {
        const bool quotedMatch =
            line.startsWith(quoted) && (line.size() == quoted.size() || line[quoted.size()].isSpace());
        const bool plainMatch = !name.contains(QRegularExpression("\\s")) && line.startsWith(name) &&
                                line.size() > name.size() && line[name.size()].isSpace();
        if (quotedMatch || plainMatch)
            continue;
        if (!line.isEmpty())
            lines << line;
    }
    if (!value.isEmpty())
        lines << quoted + " " + value;
    auto data = (lines.join('\n') + (lines.isEmpty() ? QString() : "\n")).toUtf8();
    if (flag("IdConf_comments_doscharset"))
        data = recode(data, true);
    if (data.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Legacy sidecar limit is 2 MiB");
    NativeFs::FileDescriptor parent(::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    const auto leaf = nativePath(format).native();
    const auto temporary = ".tc-comment-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    NativeFs::FileDescriptor out(::openat(parent.value, temporary.c_str(),
                                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    try {
        qsizetype offset = 0;
        while (offset < data.size()) {
            const auto n = ::write(out.value, data.data() + offset, data.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            NativeFs::check(n <= 0 ? -1 : 0);
            offset += n;
        }
        NativeFs::check(::fsync(out.value));
        struct stat current {};
        const bool exists = ::fstatat(parent.value, leaf.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0;
        if (st.st_ino) {
            if (!exists || !NativeFs::same(st, current))
                throw std::runtime_error("Legacy sidecar changed during editing");
            NativeFs::check(::renameat(parent.value, temporary.c_str(), parent.value, leaf.c_str()));
        } else
            NativeFs::renameExclusive(parent.value, temporary, leaf);
    } catch (...) {
        ::unlinkat(parent.value, temporary.c_str(), 0);
        throw;
    }
}
