#include "filesystem/pathencoding.h"
#include "thumbnails/thumbnailproviders.h"
#include "filesystem/filecomments.h"
#include "operations/fileoperations.h"
#include "ui/panels/directorymodel.h"
#include "settings/applicationpreferences.h"
#include <QCryptographicHash>
#include <QSettings>
#include <sys/xattr.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace LocalFiles {
namespace {
namespace fs = std::filesystem;
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct FileDescriptor {
    int value;
    explicit FileDescriptor(int fd) : value(fd) {
        if (fd < 0)
            throw Failure(std::strerror(errno));
    }
    ~FileDescriptor() { ::close(value); }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
};
void check(int result) {
    if (result < 0)
        throw Failure(std::strerror(errno));
}
void cancellation(const std::shared_ptr<Control> &control) { checkpoint(control); }
struct stat metadata(int parent, const std::string &name) {
    struct stat st {};
    check(::fstatat(parent, name.c_str(), &st, AT_SYMLINK_NOFOLLOW));
    return st;
}
bool same(const struct stat &a, const struct stat &b) { return a.st_dev == b.st_dev && a.st_ino == b.st_ino; }
bool unchanged(const struct stat &a, const struct stat &b) {
    return same(a, b) && a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
           a.st_mtim.tv_nsec == b.st_mtim.tv_nsec && a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
           a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
void removeEntry(int parent, const std::string &name, dev_t device, const std::shared_ptr<Control> &control,
                 unsigned depth = 0) {
    cancellation(control);
    if (depth > 256)
        throw Failure("Directory nesting exceeds the 256-level operation limit");
    const auto st = metadata(parent, name);
    if (!S_ISDIR(st.st_mode)) {
        if (ApplicationPreferences::flag("ChkExplorerDelete")) {
            const auto display = QFile::decodeName(QByteArray::fromStdString(name));
            if (QFile::encodeName(display).toStdString() != name)
                throw Failure("Qt deletion requires a decodable filename; use the POSIX method");
            if (!QFile::remove(QString("/proc/self/fd/%1/").arg(parent) + display))
                throw Failure("Qt compatibility deletion failed");
        } else
            check(::unlinkat(parent, name.c_str(), 0));
        return;
    }
    if (st.st_dev != device)
        throw Failure("Refusing to cross a filesystem boundary during deletion");
    FileDescriptor directory(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat opened {};
    check(::fstat(directory.value, &opened));
    if (!same(st, opened))
        throw Failure("Directory changed during deletion");
    const int duplicate = ::dup(directory.value);
    check(duplicate);
    auto *stream = ::fdopendir(duplicate);
    if (!stream) {
        const int error = errno;
        ::close(duplicate);
        throw Failure(std::strerror(error));
    }
    try {
        while (true) {
            errno = 0;
            auto *entry = ::readdir(stream);
            if (!entry) {
                if (errno)
                    throw Failure(std::strerror(errno));
                break;
            }
            const std::string child(entry->d_name);
            if (child == "." || child == "..")
                continue;
            removeEntry(directory.value, child, device, control, depth + 1);
        }
    } catch (...) {
        ::closedir(stream);
        throw;
    }
    ::closedir(stream);
    if (!same(st, metadata(parent, name)))
        throw Failure("Directory name changed during deletion");
    check(::unlinkat(parent, name.c_str(), AT_REMOVEDIR));
}
void copyEntry(int sourceParent, const std::string &source, int targetParent, const std::string &target,
               dev_t device, const std::shared_ptr<Control> &control, unsigned depth = 0) {
    cancellation(control);
    if (depth > 256)
        throw Failure("Directory nesting exceeds the 256-level operation limit");
    const auto before = metadata(sourceParent, source);
    if (S_ISLNK(before.st_mode)) {
        std::vector<char> bytes(4096);
        ssize_t size = 0;
        while (true) {
            size = ::readlinkat(sourceParent, source.c_str(), bytes.data(), bytes.size());
            check(static_cast<int>(size));
            if (static_cast<size_t>(size) < bytes.size())
                break;
            if (bytes.size() >= 1024 * 1024)
                throw Failure("Symlink target is too long");
            bytes.resize(bytes.size() * 2);
        }
        const std::string link(bytes.data(), static_cast<size_t>(size));
        check(::symlinkat(link.c_str(), targetParent, target.c_str()));
        if (!unchanged(before, metadata(sourceParent, source)))
            throw Failure("Source link changed during copy");
        return;
    }
    if (!S_ISREG(before.st_mode) && !S_ISDIR(before.st_mode))
        throw Failure("Only regular files, directories and symlinks can be copied");
    if (before.st_dev != device)
        throw Failure("Refusing to cross a source filesystem boundary");
    FileDescriptor input(
        ::openat(sourceParent, source.c_str(),
                 O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (S_ISDIR(before.st_mode) ? O_DIRECTORY : O_NONBLOCK)));
    struct stat opened {};
    check(::fstat(input.value, &opened));
    if (!unchanged(before, opened))
        throw Failure("Source changed before opening");
    if (S_ISDIR(before.st_mode)) {
        check(::mkdirat(targetParent, target.c_str(), 0700));
        FileDescriptor output(::openat(targetParent, target.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        const int duplicate = ::dup(input.value);
        check(duplicate);
        auto *stream = ::fdopendir(duplicate);
        if (!stream) {
            const int error = errno;
            ::close(duplicate);
            throw Failure(std::strerror(error));
        }
        try {
            while (true) {
                errno = 0;
                auto *entry = ::readdir(stream);
                if (!entry) {
                    if (errno)
                        throw Failure(std::strerror(errno));
                    break;
                }
                const std::string child(entry->d_name);
                if (child == "." || child == "..")
                    continue;
                copyEntry(input.value, child, output.value, child, device, control, depth + 1);
            }
        } catch (...) {
            ::closedir(stream);
            throw;
        }
        ::closedir(stream);
        check(::fchmod(output.value, before.st_mode & 0777));
        const timespec times[]{before.st_atim, before.st_mtim};
        if (ApplicationPreferences::flag("ChkCopyStamp"))
            check(::futimens(output.value, times));
    } else {
        FileDescriptor output(
            ::openat(targetParent, target.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        struct stat targetInfo {};
        check(::fstat(targetParent, &targetInfo));
        bool sameDisk = before.st_dev == targetInfo.st_dev;
        const auto fdPath = [](int fd) {
            char name[4096];
            const auto n = ::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), name, sizeof(name));
            return n > 0 ? QString::fromUtf8(name, n) : QString();
        };
        const auto sourcePath = fdPath(input.value), targetPath = fdPath(targetParent);
        for (const auto &group : ApplicationPreferences::text("EdPhysDisks").split(';', Qt::SkipEmptyParts)) {
            bool left = false, right = false;
            for (const auto &pattern : group.split(',', Qt::SkipEmptyParts)) {
                left |= QDir::match(pattern, sourcePath);
                right |= QDir::match(pattern, targetPath);
            }
            if (left && right)
                sameDisk = true;
        }
        const bool compatibility =
            ApplicationPreferences::flag("ChkCompatibility") &&
            (QDir::match(ApplicationPreferences::text("EdCompatLetters").split(';'), sourcePath) ||
             QDir::match(ApplicationPreferences::text("EdCompatLetters").split(';'), targetPath));
        const bool usb = ApplicationPreferences::flag("ChkUsbSticks") &&
                         (targetPath.startsWith("/media/") || targetPath.startsWith("/run/media/"));
        const bool big = ApplicationPreferences::flag("RdBigfileCopy");
        const int kib = compatibility ? 4
                        : usb
                            ? 64
                            : ApplicationPreferences::number(big ? (sameDisk ? "EdBigSame" : "EdBigOther")
                                                        : (sameDisk ? "EdSmallSame" : "EdSmallOther"),
                                                    big ? (sameDisk ? 1024 : 2048) : (sameDisk ? 128 : 256));
        std::vector<char> buffer(size_t(qBound(4, kib, 8192)) * 1024);
        if (big) {
            ::posix_fadvise(input.value, 0, 0, POSIX_FADV_SEQUENTIAL);
            ::posix_fadvise(output.value, 0, 0, POSIX_FADV_SEQUENTIAL);
        }
        QFile qtInput, qtOutput;
        const bool qt = ApplicationPreferences::flag("RdCopyPaste") && !compatibility;
        if (qt && (!qtInput.open(input.value, QIODevice::ReadOnly, QFileDevice::DontCloseHandle) ||
                   !qtOutput.open(output.value, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)))
            throw Failure("Qt copy descriptors could not open");
        bool accelerated = ApplicationPreferences::flag("id_StandardCopyMethod") && !big && !qt && !compatibility;
        while (true) {
            cancellation(control);
            ssize_t size;
            if (accelerated) {
                size = ::copy_file_range(input.value, nullptr, output.value, nullptr, buffer.size(), 0);
                if (size < 0 &&
                    (errno == EXDEV || errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP)) {
                    accelerated = false;
                    continue;
                }
                if (size < 0 && errno == EINTR && !qt)
                    continue;
                check(size < 0 ? -1 : 0);
                if (!size)
                    break;
                control->bytes += size;
                continue;
            }
            size = qt ? qtInput.read(buffer.data(), buffer.size())
                      : ::read(input.value, buffer.data(), buffer.size());
            if (size < 0 && errno == EINTR && !qt)
                continue;
            check(size < 0 ? -1 : 0);
            if (!size)
                break;
            ssize_t offset = 0;
            while (offset < size) {
                cancellation(control);
                const auto written = qt ? qtOutput.write(buffer.data() + offset, size - offset)
                                        : ::write(output.value, buffer.data() + offset, size - offset);
                if (written < 0 && errno == EINTR && !qt)
                    continue;
                check(written <= 0 ? -1 : 0);
                offset += written;
                control->bytes += written;
            }
            if (big) {
                const auto offset = ::lseek(input.value, 0, SEEK_CUR);
                ::posix_fadvise(input.value, qMax<off_t>(0, offset - size), size, POSIX_FADV_DONTNEED);
            }
        }
        if (qt)
            qtOutput.flush();
        if (ApplicationPreferences::flag("ChkVerifyAfterCopy")) {
            const auto digest = [&](int fd) {
                check(::lseek(fd, 0, SEEK_SET) < 0 ? -1 : 0);
                QCryptographicHash hash(QCryptographicHash::Md5);
                while (true) {
                    cancellation(control);
                    const auto n = ::read(fd, buffer.data(), buffer.size());
                    if (n < 0 && errno == EINTR)
                        continue;
                    check(n < 0 ? -1 : 0);
                    if (!n)
                        break;
                    hash.addData(QByteArrayView(buffer.data(), n));
                }
                return hash.result();
            };
            if (digest(input.value) != digest(output.value))
                throw Failure("Copied file verification failed; destination was not committed");
        }
        check(::fchmod(output.value, before.st_mode & 0777));
        const timespec times[]{before.st_atim, before.st_mtim};
        check(::futimens(output.value, times));
        check(::fsync(output.value));
    }
    if (ApplicationPreferences::flag("IdConf_comments_copy"))
        for (const auto *name : {"user.twincommander.comment", "user.xdg.comment", "user.comment"}) {
            const auto n = ::fgetxattr(input.value, name, nullptr, 0);
            if (n > 0 && n <= 65536) {
                QByteArray value(n, 0);
                if (::fgetxattr(input.value, name, value.data(), value.size()) == n) {
                    const int targetFd =
                        ::openat(targetParent, target.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
                    if (targetFd >= 0) {
                        ::fsetxattr(targetFd, name, value.data(), value.size(), 0);
                        ::close(targetFd);
                    }
                }
            }
        }
    struct stat after {};
    check(::fstat(input.value, &after));
    if (!unchanged(before, after) || !unchanged(before, metadata(sourceParent, source)))
        throw Failure("Source changed during copy; destination was not committed");
}
void noReplace(int sourceParent, const std::string &source, int targetParent, const std::string &target) {
    check(static_cast<int>(::syscall(SYS_renameat2, sourceParent, source.c_str(), targetParent,
                                     target.c_str(), 1 /* RENAME_NOREPLACE */)));
}
bool inside(const fs::path &child, const fs::path &parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
        if (c == child.end() || *c != *p)
            return false;
    return true;
}
QString encoded(const fs::path &path) {
    return QString::fromLatin1(QByteArray::fromStdString(path.native()).toBase64());
}
} // namespace

QString saveFile(const fs::path &path, const QByteArray &expected, const QByteArray &replacement) {
    try {
        if (!path.is_absolute() || path.filename().empty())
            throw Failure("Invalid file path");
        FileDescriptor parent(::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        const auto name = path.filename().native();
        const auto before = metadata(parent.value, name);
        if (!S_ISREG(before.st_mode))
            throw Failure("Only regular files can be saved");
        if (before.st_size > 8 * 1024 * 1024 || replacement.size() > 8 * 1024 * 1024)
            throw Failure("Editor limit is 8 MiB");
        FileDescriptor input(::openat(parent.value, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        struct stat opened {};
        check(::fstat(input.value, &opened));
        if (!unchanged(before, opened))
            throw Failure("File changed before opening");
        QByteArray contents;
        std::array<char, 64 * 1024> buffer{};
        while (contents.size() <= 8 * 1024 * 1024) {
            const auto count = ::read(input.value, buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR)
                continue;
            check(static_cast<int>(count));
            if (!count)
                break;
            contents.append(buffer.data(), static_cast<qsizetype>(count));
        }
        if (contents != expected)
            throw Failure("File changed on disk; save refused. Reopen to review changes.");
        const auto temporary = ".tc-edit-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        FileDescriptor output(::openat(parent.value, temporary.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        try {
            qsizetype offset = 0;
            while (offset < replacement.size()) {
                const auto written = ::write(output.value, replacement.constData() + offset,
                                             static_cast<size_t>(replacement.size() - offset));
                if (written < 0 && errno == EINTR)
                    continue;
                check(static_cast<int>(written));
                if (!written)
                    throw Failure("Destination stopped accepting data");
                offset += written;
            }
            check(::fchmod(output.value, before.st_mode & 0777));
            check(::fsync(output.value));
            if (!unchanged(before, metadata(parent.value, name)))
                throw Failure("File changed during save; replacement refused");
            check(::renameat(parent.value, temporary.c_str(), parent.value, name.c_str()));
        } catch (...) {
            ::unlinkat(parent.value, temporary.c_str(), 0);
            throw;
        }
        return {};
    } catch (const std::exception &error) {
        return QString::fromLocal8Bit(error.what());
    }
}

Result execute(const Request &request, const std::shared_ptr<Control> &control) {
    Result result;
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool initialJournal = true;
    size_t recorded = 0;
    QFile events;
    const auto journal = [&](bool finished) {
        if (request.journalDirectory.isEmpty())
            return;
        if (!initialJournal && !finished) {
            while (recorded < result.items.size()) {
                const auto &item = result.items[recorded++];
                const auto bytes = QJsonDocument(QJsonObject{{"sourceBase64", encoded(item.source)},
                                                             {"destinationBase64", encoded(item.destination)},
                                                             {"error", item.error},
                                                             {"skipped", item.skipped}})
                                       .toJson(QJsonDocument::Compact) +
                                   '\n';
                if (!events.isOpen() || events.write(bytes) != bytes.size() || !events.flush())
                    result.journalError = "Could not append operation outcome log";
            }
            return;
        }
        QJsonArray items;
        for (const auto &item : result.items)
            items.append(QJsonObject{{"sourceBase64", encoded(item.source)},
                                     {"destinationBase64", encoded(item.destination)},
                                     {"error", item.error},
                                     {"skipped", item.skipped}});
        QJsonArray sources;
        for (const auto &source : request.sources)
            sources.append(encoded(source));
        QJsonObject data{{"version", 1},
                         {"operation", static_cast<int>(request.operation)},
                         {"conflict", static_cast<int>(request.conflict)},
                         {"permissions", static_cast<int>(request.permissions)},
                         {"sourcesBase64", sources},
                         {"destinationBase64", encoded(request.destination)},
                         {"outcomeLog", jobId + ".jsonl"},
                         {"finished", finished},
                         {"cancelled", result.cancelled},
                         {"items", items}};
        QDir().mkpath(request.journalDirectory);
        QSaveFile file(request.journalDirectory + "/" + jobId + ".json");
        const auto bytes = QJsonDocument(data).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            result.journalError = "Could not save operation journal: " + file.errorString();
        if (initialJournal) {
            initialJournal = false;
            events.setFileName(request.journalDirectory + "/" + jobId + ".jsonl");
            if (!events.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                result.journalError = "Could not create operation outcome log";
        }
    };
    journal(false);
    const auto sources =
        (request.operation == Operation::MakeDirectory || request.operation == Operation::MakeFile)
            ? std::vector<fs::path>{request.destination}
            : request.sources;
    for (const auto &sourcePath : sources) {
        Outcome outcome{sourcePath, {}, {}, false};
        try {
            cancellation(control);
            const auto copiedComment =
                ApplicationPreferences::flag("IdConf_comments_copy") &&
                        (request.operation == Operation::Copy || request.operation == Operation::Move ||
                         request.operation == Operation::Rename)
                    ? FileComments::read(sourcePath)
                    : QString();
            if (!sourcePath.is_absolute() || sourcePath.filename().empty() || sourcePath.filename() == "." ||
                sourcePath.filename() == "..")
                throw Failure("Operation requires an absolute non-root entry path");
            FileDescriptor sourceParent(::open(sourcePath.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
            const auto source = sourcePath.filename().native();
            if (request.operation == Operation::MakeDirectory || request.operation == Operation::MakeFile) {
                if (request.operation == Operation::MakeDirectory)
                    check(::mkdirat(sourceParent.value, source.c_str(), 0777));
                else {
                    FileDescriptor file(::openat(sourceParent.value, source.c_str(),
                                     O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666));
                    check(::fsync(file.value));
                }
                outcome.destination = sourcePath;
            } else if (request.operation == Operation::SetPermissions) {
                if (request.permissions > 0777)
                    throw Failure("Only ordinary permission bits 0000 through 0777 are supported");
                const auto before = metadata(sourceParent.value, source);
                if (!S_ISREG(before.st_mode) && !S_ISDIR(before.st_mode))
                    throw Failure("Permission editing requires a regular file or directory, not a link");
                FileDescriptor descriptor(::openat(sourceParent.value, source.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC));
                struct stat opened {};
                check(::fstat(descriptor.value, &opened));
                if (!unchanged(before, opened))
                    throw Failure("Entry changed before permission update");
                check(::chmod(("/proc/self/fd/" + std::to_string(descriptor.value)).c_str(),
                              request.permissions));
                outcome.destination = sourcePath;
            } else if (request.operation == Operation::SymbolicLink) {
                if (!request.destination.is_absolute() || request.destination.filename().empty() ||
                    request.destination.filename() == "." || request.destination.filename() == "..")
                    throw Failure("Choose an absolute new symbolic-link path");
                FileDescriptor target(
                    ::open(request.destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
                check(::symlinkat(sourcePath.c_str(), target.value, request.destination.filename().c_str()));
                outcome.destination = request.destination;
            } else if (request.operation == Operation::Trash) {
                if (nativePath(displayPath(sourcePath)) != sourcePath)
                    throw Failure("Trash does not yet support undecodable paths; rename or use explicit "
                                  "permanent deletion");
                QString trashed;
                if (!QFile::moveToTrash(displayPath(sourcePath), &trashed))
                    throw Failure("Desktop trash operation failed");
                outcome.destination = nativePath(trashed);
            } else if (request.operation == Operation::Delete) {
                const auto st = metadata(sourceParent.value, source);
                removeEntry(sourceParent.value, source, st.st_dev, control);
            } else {
                const auto targetPath = request.operation == Operation::Rename
                                            ? request.destination
                                            : request.destination / sourcePath.filename();
                outcome.destination = targetPath;
                if (!targetPath.is_absolute() || targetPath.filename().empty() ||
                    targetPath.filename() == "." || targetPath.filename() == "..")
                    throw Failure("Invalid destination entry path");
                const auto before = metadata(sourceParent.value, source);
                const auto parent = fs::canonical(targetPath.parent_path());
                if (S_ISDIR(before.st_mode) && inside(parent, fs::canonical(sourcePath)))
                    throw Failure("Destination is inside the source directory");
                FileDescriptor targetParent(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
                const auto target = targetPath.filename().native();
                struct stat existing {};
                const bool exists =
                    ::fstatat(targetParent.value, target.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0;
                if (!exists && errno != ENOENT)
                    throw Failure(std::strerror(errno));
                if (exists && same(before, existing))
                    throw Failure("Source and destination are the same entry");
                if (exists && request.conflict == Conflict::Skip)
                    outcome.skipped = true;
                else if (request.operation == Operation::Move || request.operation == Operation::Rename) {
                    // No fallback to copy/delete: an interrupted cross-device move must retain its source.
                    noReplace(sourceParent.value, source, targetParent.value, target);
                } else {
                    if (exists && (request.conflict != Conflict::ReplaceFiles || !S_ISREG(existing.st_mode) ||
                                   !S_ISREG(before.st_mode)))
                        throw Failure("Destination exists; only regular-file replacement is supported");
                    const auto temporary =
                        ".tc-copy-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                    try {
                        copyEntry(sourceParent.value, source, targetParent.value, temporary, before.st_dev,
                                  control);
                        cancellation(control);
                        if (exists) {
                            if (!unchanged(existing, metadata(targetParent.value, target)))
                                throw Failure("Destination changed while copying; replacement refused");
                            check(::renameat(targetParent.value, temporary.c_str(), targetParent.value,
                                             target.c_str()));
                        } else
                            noReplace(targetParent.value, temporary, targetParent.value, target);
                    } catch (...) {
                        auto cleanup = std::make_shared<Control>();
                        try {
                            const auto st = metadata(targetParent.value, temporary);
                            // Copy may already have applied restrictive directory permissions.
                            if (S_ISDIR(st.st_mode))
                                ::fchmodat(targetParent.value, temporary.c_str(), 0700, 0);
                            removeEntry(targetParent.value, temporary, st.st_dev, cleanup);
                        } catch (const std::exception &error) {
                            if (errno != ENOENT)
                                outcome.error = QString("Temporary output %1 remains: %2. ")
                                                    .arg(displayPath(parent / temporary),
                                                         QString::fromLocal8Bit(error.what()));
                        }
                        throw;
                    }
                }
            }
            if (!outcome.skipped && !outcome.destination.empty() && !copiedComment.isEmpty())
                FileComments::write(outcome.destination, copiedComment);
            if (!outcome.skipped)
                ThumbnailProviders::synchronizeCache(
                    sourcePath,
                    (request.operation == Operation::Trash || request.operation == Operation::Delete)
                        ? fs::path{}
                        : outcome.destination,
                    request.operation == Operation::Delete || request.operation == Operation::Trash ||
                        request.operation == Operation::Move || request.operation == Operation::Rename);
        } catch (const Cancelled &) {
            result.cancelled = true;
            outcome.error += "Cancelled; earlier completed items remain applied";
        } catch (const std::exception &error) {
            outcome.error += QString::fromLocal8Bit(error.what());
        }
        result.items.push_back(std::move(outcome));
        ++control->completed;
        journal(false);
        if (result.cancelled)
            break;
    }
    result.cancelled = result.cancelled || control->cancelled.load();
    journal(true);
    return result;
}
} // namespace LocalFiles
