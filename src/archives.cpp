#include "archives.h"
#include "finalarchives.h"
#include "finalsettings.h"
#include "nativefs.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <functional>
#include <map>
#include <set>

namespace Archives {
namespace {
using namespace NativeFs;
using LocalFiles::checkpoint;
struct Reader {
    archive *value = archive_read_new();
    int input = -1;
    std::array<char, 128 * 1024> buffer{};
    QStringList warnings;
    bool streamZip = false;
    Reader() {
        if (!value)
            throw Error("Cannot allocate archive reader");
        archive_read_support_filter_all(value);
        archive_read_support_format_all(value);
    }
    ~Reader() { archive_read_free(value); }
};
struct Writer {
    archive *value = archive_write_new();
    ~Writer() {
        if (value)
            archive_write_free(value);
    }
};
void archiveCheck(int code, archive *handle) {
    if (code < ARCHIVE_OK)
        throw Error(archive_error_string(handle) ? archive_error_string(handle) : "Archive error");
}
std::string safeName(const char *name) {
    if (!name || !*name)
        throw Error("Empty archive pathname");
    const std::string raw(name);
    if (raw.size() > 4096 || raw.front() == '/' || raw.find('\\') != std::string::npos)
        throw Error("Unsafe absolute or backslash archive path");
    fs::path result;
    size_t depth = 0;
    for (const auto &component : fs::path(raw)) {
        if (component == "..")
            throw Error("Parent traversal in archive path");
        if (component == "." || component.empty())
            continue;
        if (component.native().size() > 255 || ++depth > 128 ||
            (depth == 1 && component.native().find(':') != std::string::npos))
            throw Error("Unsupported archive path component");
        result /= component;
    }
    return result.native();
}
Entry describe(archive_entry *entry) {
    Entry result;
    result.directory = archive_entry_filetype(entry) == AE_IFDIR;
    result.size = archive_entry_size(entry) < 0 ? 0 : static_cast<quint64>(archive_entry_size(entry));
    try {
        result.name = safeName(archive_entry_pathname(entry));
        if (result.name.empty() && !result.directory)
            throw Error("Empty file name");
        if (archive_entry_hardlink(entry) || archive_entry_symlink(entry) ||
            (!result.directory && archive_entry_filetype(entry) != AE_IFREG))
            throw Error("Links and special entries cannot be extracted");
        if (archive_entry_size(entry) < 0)
            throw Error("Negative entry size");
        result.size = static_cast<quint64>(archive_entry_size(entry));
        if (result.directory && result.size)
            throw Error("Directory entry contains unexpected data");
    } catch (const std::exception &error) {
        if (result.name.empty())
            result.name = archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "<invalid>";
        result.unsafe = QString::fromLocal8Bit(error.what());
    }
    return result;
}
bool crcWarning(Reader &reader) {
    const auto message =
        QString::fromUtf8(archive_error_string(reader.value) ? archive_error_string(reader.value) : "");
    if (FinalSettings::flag("zip_keepdamaged") &&
        (archive_format(reader.value) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_ZIP &&
        (message.contains("CRC", Qt::CaseInsensitive) || message.contains("checksum", Qt::CaseInsensitive))) {
        reader.warnings << message;
        return true;
    }
    return false;
}
void drain(Reader &reader, quint64 declared, quint64 &total,
           const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits) {
    if (declared > limits.fileBytes || declared > limits.bytes - total)
        throw Error("Archive expanded-size limit exceeded");
    std::vector<char> buffer(128 * 1024);
    quint64 bytes = 0;
    while (true) {
        checkpoint(control);
        const auto count = archive_read_data(reader.value, buffer.data(), buffer.size());
        if (count < 0) {
            if (bytes == declared && crcWarning(reader))
                break;
            archiveCheck(static_cast<int>(count), reader.value);
        }
        if (!count)
            break;
        if (static_cast<quint64>(count) > limits.fileBytes - bytes ||
            static_cast<quint64>(count) > limits.bytes - total)
            throw Error("Archive expanded-size limit exceeded");
        bytes += static_cast<quint64>(count);
        total += static_cast<quint64>(count);
        control->bytes += static_cast<uint64_t>(count);
    }
    if (bytes != declared)
        throw Error("Archive entry size does not match its data");
}
void openReader(Reader &reader, int fd) {
    char magic[4]{};
    if (FinalSettings::flag("zip_keepdamaged") && ::pread(fd, magic, 4, 0) == 4 &&
        magic[0] == 'P' && magic[1] == 'K') {
        archiveCheck(archive_read_set_format_option(reader.value, "zip", "ignorecrc32", "1"), reader.value);
        reader.warnings << "ZIP recovery mode: CRC verification disabled; recovered files may be damaged";
    }
    reader.streamZip = FinalSettings::flag("zip_openpartial") && ::pread(fd, magic, 4, 0) == 4 &&
                       magic[0] == 'P' && magic[1] == 'K';
    if (reader.streamZip) {
        reader.input = fd;
        archiveCheck(archive_read_open(
                         reader.value, &reader, nullptr,
                         [](archive *, void *data, const void **buffer) -> la_ssize_t {
                             auto *r = static_cast<Reader *>(data);
                             *buffer = r->buffer.data();
                             return ::read(r->input, r->buffer.data(), r->buffer.size());
                         },
                         nullptr),
                     reader.value);
    } else
        archiveCheck(archive_read_open_fd(reader.value, fd, 128 * 1024), reader.value);
}
void validateArchiveFile(const struct stat &st) {
    if (!S_ISREG(st.st_mode))
        throw Error("Archive must be a regular file, not a symlink");
}
std::string uniqueName(const char *prefix) {
    return std::string(prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
// Walk every output component through descriptors; archive entries never become host absolute paths.
void writeEntry(Reader &reader, int root, const Entry &entry, quint64 &total,
                const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits) {
    Fd parent(::dup(root));
    const fs::path path(entry.name);
    for (const auto &component : path.parent_path()) {
        if (::mkdirat(parent.value, component.c_str(), 0755) < 0 && errno != EEXIST)
            check(-1);
        const int child =
            ::openat(parent.value, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        check(child);
        ::close(parent.value);
        parent.value = child;
    }
    const auto leaf = path.filename().native();
    if (entry.directory) {
        if (entry.size != 0)
            throw Error("Directory entry contains unexpected data");
        if (::mkdirat(parent.value, leaf.c_str(), 0755) < 0 && errno != EEXIST)
            check(-1);
        Fd directory(::openat(parent.value, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        archiveCheck(archive_read_data_skip(reader.value), reader.value);
        return;
    }
    if (entry.size > limits.fileBytes || entry.size > limits.bytes - total)
        throw Error("Archive extraction size limit exceeded");
    Fd output(
        ::openat(parent.value, leaf.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    std::vector<char> buffer(128 * 1024);
    quint64 written = 0;
    while (true) {
        checkpoint(control);
        const auto count = archive_read_data(reader.value, buffer.data(), buffer.size());
        if (count < 0) {
            if (written == entry.size && crcWarning(reader))
                break;
            archiveCheck(static_cast<int>(count), reader.value);
        }
        if (!count)
            break;
        if (static_cast<quint64>(count) > limits.fileBytes - written ||
            static_cast<quint64>(count) > limits.bytes - total)
            throw Error("Actual expanded archive data exceeds its limit");
        written += static_cast<quint64>(count);
        total += static_cast<quint64>(count);
        size_t offset = 0;
        while (offset < static_cast<size_t>(count)) {
            checkpoint(control);
            const auto size =
                ::write(output.value, buffer.data() + offset, static_cast<size_t>(count) - offset);
            if (size < 0 && errno == EINTR)
                continue;
            check(static_cast<int>(size));
            if (!size)
                throw Error("Output stopped accepting data");
            offset += static_cast<size_t>(size);
        }
        control->bytes += static_cast<uint64_t>(count);
    }
    if (written != entry.size)
        throw Error("Archive entry size does not match expanded data");
    check(::fchmod(output.value, 0644));
    check(::fsync(output.value));
}
} // namespace
Listing list(const fs::path &path, const std::shared_ptr<LocalFiles::Control> &control,
             const Limits &limits) {
    if (FinalArchives::external(path))
        return FinalArchives::list(path, control, limits);
    Listing result;
    try {
        checkpoint(control);
        const auto before = inspect(path);
        validateArchiveFile(before);
        Fd file(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (!same(before, inspectFd(file.value)))
            throw Error("Archive changed before opening");
        Reader reader;
        openReader(reader, file.value);
        std::map<std::string, bool> seen;
        quint64 total = 0;
        archive_entry *entry = nullptr;
        while (true) {
            checkpoint(control);
            const auto status = archive_read_next_header(reader.value, &entry);
            if (status == ARCHIVE_EOF)
                break;
            if (status < ARCHIVE_OK && reader.streamZip && !result.entries.empty()) {
                result.warning = "Recovered verified ZIP entries before a truncated tail";
                break;
            }
            archiveCheck(status, reader.value);
            if (result.entries.size() >= limits.entries)
                throw Error("Archive entry count limit exceeded");
            auto item = describe(entry);
            if (item.name.empty() && item.directory && item.unsafe.isEmpty()) {
                archiveCheck(archive_read_data_skip(reader.value), reader.value);
                continue;
            }
            if (seen.contains(item.name) && (!item.directory || !seen[item.name]))
                item.unsafe = "Duplicate or conflicting archive pathname";
            seen[item.name] = item.directory;
            const auto size = item.size;
            try {
                drain(reader, size, total, control, limits);
            } catch (const std::exception &e) {
                if (reader.streamZip && !result.entries.empty()) {
                    result.warning = "Recovered verified ZIP prefix: " + QString::fromLocal8Bit(e.what());
                    break;
                }
                throw;
            }
            result.entries.push_back(std::move(item));
            ++control->completed;
        }
        result.warning += reader.warnings.join("; ");
        verify(path, file.value, before);
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.error = QString::fromLocal8Bit(error.what());
    }
    const auto ext = displayPath(path.extension()).toLower();
    if (!result.error.isEmpty() && QStringList{".arj", ".ace", ".lzh", ".lha", ".rar"}.contains(ext))
        return FinalArchives::list(path, control, limits);
    return result;
}
LocalFiles::Result extract(const fs::path &path, const fs::path &destination,
                           const std::vector<std::string> &selected,
                           const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits) {
    if (FinalArchives::external(path))
        return FinalArchives::extract(path, destination, selected, control, limits);
    LocalFiles::Result result;
    LocalFiles::Outcome outcome{path, destination, {}, false};
    fs::path temporaryPath;
    try {
        checkpoint(control);
        if (!destination.is_absolute() || destination.filename().empty() || destination.filename() == "." ||
            destination.filename() == "..")
            throw Error("Choose a new absolute destination directory");
        const auto parentPath = fs::canonical(destination.parent_path());
        Fd parent(::open(parentPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        const auto temporary = uniqueName(".tc-extract-");
        check(::mkdirat(parent.value, temporary.c_str(), 0700));
        temporaryPath = parentPath / temporary;
        Fd output(::openat(parent.value, temporary.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        const auto before = inspect(path);
        validateArchiveFile(before);
        Fd file(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (!same(before, inspectFd(file.value)))
            throw Error("Archive changed before opening");
        Reader reader;
        openReader(reader, file.value);
        archive_entry *header = nullptr;
        std::map<std::string, bool> seen;
        quint64 total = 0;
        size_t count = 0, extracted = 0;
        std::set<std::string> verifiedEntries;
        while (true) {
            checkpoint(control);
            const auto status = archive_read_next_header(reader.value, &header);
            if (status == ARCHIVE_EOF)
                break;
            if (status < ARCHIVE_OK && reader.streamZip && extracted) {
                reader.warnings << "Recovered complete ZIP entries before a truncated tail";
                break;
            }
            archiveCheck(status, reader.value);
            if (++count > limits.entries)
                throw Error("Archive entry count limit exceeded");
            const auto item = describe(header);
            if (!item.unsafe.isEmpty())
                throw Error(item.unsafe.toStdString());
            if (item.name.empty() && item.directory) {
                archiveCheck(archive_read_data_skip(reader.value), reader.value);
                continue;
            }
            if (seen.contains(item.name) && (!item.directory || !seen[item.name]))
                throw Error("Duplicate or conflicting archive pathname");
            seen[item.name] = item.directory;
            const bool wanted =
                selected.empty() || std::any_of(selected.begin(), selected.end(), [&](const auto &name) {
                    return item.name == name || item.name.starts_with(name + "/");
                });
            if (wanted) {
                writeEntry(reader, output.value, item, total, control, limits);
                ++control->completed;
                ++extracted;
                verifiedEntries.insert(item.name);
            } else {
                try {
                    drain(reader, item.size, total, control, limits);
                } catch (const std::exception &) {
                    if (reader.streamZip && !selected.empty() &&
                        std::all_of(selected.begin(), selected.end(),
                                    [&](const auto &name) { return verifiedEntries.contains(name); })) {
                        reader.warnings << "Recovered selected verified entries before a truncated tail";
                        break;
                    }
                    throw;
                }
            }
        }
        if (!selected.empty() && !extracted)
            throw Error("Selected archive entries no longer exist");
        verify(path, file.value, before);
        checkpoint(control);
        renameExclusive(parent.value, temporary, destination.filename().native());
        temporaryPath.clear();
        outcome.error = reader.warnings.isEmpty()
                            ? QString()
                            : "Extracted with integrity warnings: " + reader.warnings.join("; ");
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
        outcome.error = "Extraction cancelled; destination not committed";
    } catch (const std::exception &error) {
        outcome.error = QString::fromLocal8Bit(error.what());
    }
    if (!temporaryPath.empty()) {
        std::error_code error;
        fs::remove_all(temporaryPath, error);
        if (error)
            outcome.error += "\nTemporary output remains: " + QString::fromLocal8Bit(temporaryPath.c_str());
    }
    const auto ext = displayPath(path.extension()).toLower();
    if (!outcome.error.isEmpty() && !result.cancelled &&
        QStringList{".arj", ".ace", ".lzh", ".lha", ".rar"}.contains(ext))
        return FinalArchives::extract(path, destination, selected, control, limits);
    result.items.push_back(std::move(outcome));
    return result;
}
LocalFiles::Result create(const std::vector<fs::path> &sources, const fs::path &destination, Format format,
                          const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits) {
    LocalFiles::Result result;
    LocalFiles::Outcome outcome{sources.empty() ? fs::path{} : sources.front(), destination, {}, false};
    fs::path temporaryPath;
    try {
        if (sources.empty() || !destination.is_absolute() || destination.filename().empty())
            throw Error("Choose sources and an absolute archive destination");
        const auto parentPath = fs::canonical(destination.parent_path());
        Fd parent(::open(parentPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        const auto temporary = uniqueName(".tc-pack-");
        Fd file(::openat(parent.value, temporary.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        temporaryPath = parentPath / temporary;
        const auto scratchRoot = FinalSettings::flag("internalzip_otherdir")
                                     ? FinalSettings::text("internalzip_tempdir")
                                     : QDir::tempPath();
        if (!QDir::isAbsolutePath(scratchRoot) || !QDir(scratchRoot).exists())
            throw Error("Archive temporary directory must exist and be absolute");
        QTemporaryDir scratch(scratchRoot + "/tc-archive-XXXXXX");
        if (!scratch.isValid())
            throw Error("Archive temporary directory is not writable");
        Fd work(::open(QFile::encodeName(scratch.filePath("archive")).constData(),
                       O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        Writer writer;
        if (!writer.value)
            throw Error("Cannot allocate archive writer");
        if (format == Format::Zip) {
            archiveCheck(archive_write_set_format_zip(writer.value), writer.value);
            const int choice = QSettings().value("archive/zipLevelChoice", 0).toInt();
            const int levels[]{6, 9, 1};
            const auto level = QByteArray::number(
                choice >= 0 && choice < 3 ? levels[choice]
                                          : qBound(0, QSettings().value("archive/zipLevel", 6).toInt(), 9));
            archiveCheck(
                archive_write_set_format_option(writer.value, "zip", "compression-level", level.constData()),
                writer.value);
        } else {
            archiveCheck(FinalSettings::flag("tar_linuxcompatible")
                             ? archive_write_set_format_pax_restricted(writer.value)
                             : archive_write_set_format_ustar(writer.value),
                         writer.value);
            if (format == Format::TarGzip)
                archiveCheck(archive_write_add_filter_gzip(writer.value), writer.value);
        }
        if (format == Format::Zip && FinalSettings::text("internalzip_unicodemethod", "UTF-8") == "CP437")
            archiveCheck(archive_write_set_format_option(writer.value, "zip", "hdrcharset", "CP437"),
                         writer.value);
        archiveCheck(archive_write_open_fd(writer.value, work.value), writer.value);
        quint64 total = 0;
        size_t count = 0;
        struct timespec newest {};
        bool hasDatedFile = false;
        std::set<std::string> names;
        std::map<fs::path, fs::path> shortNames;
        std::function<void(const fs::path &, const fs::path &, unsigned)> append;
        append = [&](const fs::path &source, const fs::path &name, unsigned depth) {
            checkpoint(control);
            if (!source.is_absolute() || source.filename().empty() || source.filename() == "." ||
                source.filename() == "..")
                throw Error("Archive sources must be non-root absolute entries");
            if (++count > limits.entries || depth > 128)
                throw Error("Archive creation entry/depth limit exceeded");
            const auto before = inspect(source);
            if (S_ISREG(before.st_mode) &&
                (!hasDatedFile || before.st_mtim.tv_sec > newest.tv_sec ||
                 (before.st_mtim.tv_sec == newest.tv_sec && before.st_mtim.tv_nsec > newest.tv_nsec))) {
                newest = before.st_mtim;
                hasDatedFile = true;
            }
            if (!S_ISREG(before.st_mode) && !S_ISDIR(before.st_mode))
                throw Error("Archive creation accepts regular files and directories; links and special files "
                            "are refused");
            fs::path archiveName = name;
            if (FinalSettings::flag("zip_83name")) {
                const auto parent = shortNames[name.parent_path()];
                int serial = 1;
                archiveName =
                    parent / nativePath(FinalSettings::alias83(displayPath(name.filename()), serial));
                while (names.contains(archiveName.native()))
                    archiveName =
                        parent / nativePath(FinalSettings::alias83(displayPath(name.filename()), ++serial));
                shortNames[name] = archiveName;
            }
            const auto raw = archiveName.native();
            if (FinalSettings::text("internalzip_unicodemethod", "UTF-8") == "ASCII" &&
                std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return c >= 128; }))
                throw Error("The selected archive filename encoding requires ASCII names");
            if (QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size())).toUtf8().toStdString() !=
                raw)
                throw Error("Archive creation requires UTF-8 filenames");
            const auto safe = safeName(raw.c_str());
            if (safe.empty() || !names.insert(safe).second)
                throw Error("Duplicate or invalid source archive name");
            Fd input(::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC |
                                                (S_ISDIR(before.st_mode) ? O_DIRECTORY : 0)));
            if (!same(before, inspectFd(input.value)))
                throw Error("Source changed before opening");
            if (S_ISREG(before.st_mode) && (static_cast<quint64>(before.st_size) > limits.fileBytes ||
                                            static_cast<quint64>(before.st_size) > limits.bytes - total))
                throw Error("Archive creation size limit exceeded");
            auto *entry = archive_entry_new();
            if (!entry)
                throw Error("Cannot allocate archive entry");
            archive_entry_set_pathname(entry, raw.c_str());
            archive_entry_set_filetype(entry, S_ISDIR(before.st_mode) ? AE_IFDIR : AE_IFREG);
            archive_entry_set_perm(entry, before.st_mode & 0777);
            archive_entry_set_size(entry, S_ISDIR(before.st_mode) ? 0 : before.st_size);
            archive_entry_set_mtime(entry, before.st_mtim.tv_sec, before.st_mtim.tv_nsec);
            const auto status = archive_write_header(writer.value, entry);
            archive_entry_free(entry);
            archiveCheck(status, writer.value);
            if (S_ISDIR(before.st_mode)) {
                for (const auto &child : fs::directory_iterator(source))
                    append(child.path(), name / child.path().filename(), depth + 1);
            } else {
                std::vector<char> buffer(128 * 1024);
                quint64 bytes = 0;
                while (true) {
                    checkpoint(control);
                    const auto size = ::read(input.value, buffer.data(), buffer.size());
                    if (size < 0 && errno == EINTR)
                        continue;
                    check(static_cast<int>(size));
                    if (!size)
                        break;
                    if (static_cast<quint64>(size) > limits.bytes - total ||
                        static_cast<quint64>(size) > limits.fileBytes - bytes)
                        throw Error("Source grew beyond archive limit");
                    size_t offset = 0;
                    while (offset < static_cast<size_t>(size)) {
                        const auto written = archive_write_data(writer.value, buffer.data() + offset,
                                                                static_cast<size_t>(size) - offset);
                        if (written <= 0)
                            throw Error("Archive write failed");
                        offset += static_cast<size_t>(written);
                    }
                    bytes += static_cast<quint64>(size);
                    total += static_cast<quint64>(size);
                    control->bytes += static_cast<uint64_t>(size);
                }
                if (bytes != static_cast<quint64>(before.st_size))
                    throw Error("Source changed while packing");
            }
            verify(source, input.value, before);
            ++control->completed;
        };
        for (const auto &source : sources) {
            if (!source.is_absolute() || source.filename().empty())
                throw Error("Archive sources must be absolute entries");
            if (S_ISDIR(inspect(source).st_mode) && inside(parentPath, fs::canonical(source)))
                throw Error("Archive destination cannot be inside a source directory");
            append(source, source.filename(), 0);
        }
        archiveCheck(archive_write_close(writer.value), writer.value);
        check(::lseek(work.value, 0, SEEK_SET) < 0 ? -1 : 0);
        std::array<char, 128 * 1024> data{};
        while (true) {
            checkpoint(control);
            const auto n = ::read(work.value, data.data(), data.size());
            check(n < 0 ? -1 : 0);
            if (!n)
                break;
            ssize_t offset = 0;
            while (offset < n) {
                const auto written = ::write(file.value, data.data() + offset, n - offset);
                check(written <= 0 ? -1 : 0);
                offset += written;
            }
        }
        if (hasDatedFile && QSettings().value("archive/newestDate").toBool()) {
            const struct timespec times[]{newest, newest};
            check(::futimens(file.value, times));
        }
        check(::fsync(file.value));
        checkpoint(control);
        renameExclusive(parent.value, temporary, destination.filename().native());
        temporaryPath.clear();
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
        outcome.error = "Archive creation cancelled; destination not committed";
    } catch (const std::exception &error) {
        outcome.error = QString::fromLocal8Bit(error.what());
    }
    if (!temporaryPath.empty()) {
        std::error_code error;
        fs::remove(temporaryPath, error);
        if (error)
            outcome.error += "\nTemporary archive remains: " + QString::fromLocal8Bit(temporaryPath.c_str());
    }
    result.items.push_back(std::move(outcome));
    return result;
}
} // namespace Archives
