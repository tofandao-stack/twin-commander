#include "operations/operationlog.h"
#include "filesystem/nativefs.h"
#include "filesystem/pathencoding.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <mutex>
#include <sys/file.h>

void OperationLog::append(const QString &category, const QString &status, const QString &message) {
    QSettings s;
    if (!s.value("logging/enabled").toBool() || !s.value("logging/" + category, true).toBool() ||
        !s.value("logging/" + status, true).toBool())
        return;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    try {
        using namespace NativeFs;
        const auto path = nativePath(s.value("logging/file").toString());
        if (path.empty() || path.filename().empty())
            return;
        FileDescriptor parent(::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        const auto leaf = path.filename().native();
        FileDescriptor file(::openat(parent.value, leaf.c_str(),
                                     O_RDWR | O_APPEND | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                                     0600));
        const auto st = inspectFd(file.value);
        if (!S_ISREG(st.st_mode))
            return;
        if (::flock(file.value, LOCK_EX | LOCK_NB) < 0)
            return;
        const int rotation = s.value("logging/rotation", 0).toInt();
        const auto today = QDateTime::currentDateTimeUtc().date();
        const bool rotate =
            st.st_size > 0 &&
            ((rotation == 1 && QDateTime::fromSecsSinceEpoch(st.st_mtim.tv_sec, Qt::UTC).date() != today) ||
             (rotation == 2 &&
              st.st_size >= qBound(1, s.value("logging/sizeKiB", 1024).toInt(), 1048576) * 1024LL));
        const auto prefix = leaf + ".tc-log.";
        if (rotate) {
            const auto name = prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            renameExclusive(parent.value, leaf, name); // Reopen the new active log without recursion.
            const int next = ::openat(parent.value, leaf.c_str(),
                                      O_RDWR | O_APPEND | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            check(next);
            ::close(file.value);
            file.value = next;
        }
        if (inspectFd(file.value).st_size == 0) {
            const QByteArray header = "# TwinCommander log v1\n";
            check(::write(file.value, header.data(), header.size()) < 0 ? -1 : 0);
        }
        const auto line =
            QJsonDocument(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                      {"category", category},
                                      {"status", status},
                                      {"message", message.left(4096)}})
                .toJson(QJsonDocument::Compact) +
            '\n';
        const auto written = ::write(file.value, line.data(), line.size());
        if (written != line.size())
            return;
        if (s.value("logging/retention").toBool()) {
            std::vector<std::pair<qint64, std::filesystem::path>> owned;
            size_t checked = 0;
            for (const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
                if (++checked > 10000)
                    break;
                const auto name = entry.path().filename().native();
                if (name.rfind(prefix, 0) != 0)
                    continue;
                const int fd =
                    ::openat(parent.value, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0)
                    continue;
                FileDescriptor candidate(fd);
                const auto meta = inspectFd(fd);
                const QByteArray marker = "# TwinCommander log v1\n";
                char header[64]{};
                if (!S_ISREG(meta.st_mode) || ::read(fd, header, marker.size()) != marker.size() ||
                    QByteArray(header, marker.size()) != marker)
                    continue;
                owned.emplace_back(meta.st_mtim.tv_sec * 1000000000LL + meta.st_mtim.tv_nsec, entry.path());
            }
            std::sort(owned.rbegin(), owned.rend());
            const int keep = qBound(1, s.value("logging/keep", 5).toInt(), 100) - 1;
            for (size_t i = keep; i < owned.size(); ++i)
                ::unlinkat(parent.value, owned[i].second.filename().c_str(), 0);
        }
    } catch (const std::exception &) { /* A log error must not change a completed file operation's result. */
    }
}
