#include "search.h"
#include "directorymodel.h"
#include <QRegularExpression>
#include <array>
#include <QFile>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

SearchResult searchFiles(const SearchRequest &request, const std::shared_ptr<LocalFiles::Control> &control)
{
    SearchResult result;
    namespace fs = std::filesystem;
    auto options = request.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression mask(request.nameRegex ? request.pattern : QRegularExpression::wildcardToRegularExpression(request.pattern), options);
    const QRegularExpression contentExpression(request.content, options);
    if (!mask.isValid() || (request.contentRegex && !contentExpression.isValid())) { result.errors << "Invalid regular expression"; return result; }
    if (request.minBytes >= 0 && request.maxBytes >= 0 && request.minBytes > request.maxBytes) { result.errors << "Minimum size exceeds maximum size"; return result; }
    if (request.modifiedAfter >= 0 && request.modifiedBefore >= 0 && request.modifiedAfter > request.modifiedBefore) { result.errors << "Modified date range is reversed"; return result; }
    std::error_code error;
    const auto root = fs::canonical(request.root, error);
    if (error) { result.errors << QString::fromStdString(error.message()); return result; }
    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error), end;
    if (error) { result.errors << QString::fromStdString(error.message()); return result; }
    QByteArray needle = request.content.toUtf8();
    if (needle.size() > 4096) { result.errors << "Content query exceeds 4096 UTF-8 bytes"; return result; }
    if (!request.caseSensitive) needle = needle.toLower();
    while (iterator != end && !control->cancelled) {
        const auto entry = *iterator;
        ++result.inspected;
        ++control->completed;
        const auto filename = entry.path().filename().native();
        const bool hidden = !filename.empty() && filename.front() == '.';
        if (!request.recursive || (hidden && !request.hidden)) iterator.disable_recursion_pending();
        bool matches = (!hidden || request.hidden) && mask.match(displayPath(entry.path().filename())).hasMatch();
        if (matches && (request.minBytes >= 0 || request.maxBytes >= 0 || request.modifiedAfter >= 0 || request.modifiedBefore >= 0)) {
            struct stat st{};
            if (::lstat(entry.path().c_str(), &st)) matches = false;
            else {
                if (request.minBytes >= 0 || request.maxBytes >= 0) matches = S_ISREG(st.st_mode)
                    && (request.minBytes < 0 || st.st_size >= request.minBytes) && (request.maxBytes < 0 || st.st_size <= request.maxBytes);
                matches = matches && (request.modifiedAfter < 0 || st.st_mtime >= request.modifiedAfter)
                    && (request.modifiedBefore < 0 || st.st_mtime <= request.modifiedBefore);
            }
        }
        if (matches && !needle.isEmpty()) {
            matches = false;
            const auto status = entry.symlink_status(error);
            if (!error && fs::is_regular_file(status)) {
                const auto size = entry.file_size(error);
                if (!error && size <= 8 * 1024 * 1024) {
                    const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                    QFile file;
                    struct stat opened{};
                    const bool regular = fd >= 0 && ::fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode);
                    const bool readable = regular && file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle);
                    if (!readable) {
                        if (fd >= 0) ::close(fd);
                        if (result.errors.size() < 100) result.errors << "Cannot read " + displayPath(entry.path());
                    } else if (request.contentRegex) {
                        if (!control->cancelled) {
                            const auto contents = file.read(8 * 1024 * 1024 + 1);
                            if (file.error() != QFileDevice::NoError) {
                                if (result.errors.size() < 100) result.errors << "Cannot read " + displayPath(entry.path());
                            } else if (contents.size() <= 8 * 1024 * 1024) matches = contentExpression.match(QString::fromUtf8(contents)).hasMatch();
                            else ++result.contentSkipped;
                        }
                    } else {
                        std::array<char, 64 * 1024> buffer{};
                        QByteArray tail;
                        quint64 read = 0;
                        while (!control->cancelled && read < 8 * 1024 * 1024) {
                            const auto count = file.read(buffer.data(), std::min<quint64>(buffer.size(), 8 * 1024 * 1024 - read));
                            if (count <= 0) {
                                if (count < 0 && result.errors.size() < 100) result.errors << "Cannot read " + displayPath(entry.path());
                                break;
                            }
                            read += static_cast<quint64>(count);
                            auto block = tail + QByteArray(buffer.data(), static_cast<qsizetype>(count));
                            if (!request.caseSensitive) block = block.toLower();
                            if (block.contains(needle)) { matches = true; break; }
                            tail = block.right(needle.size() - 1);
                        }
                    }
                } else ++result.contentSkipped;
            }
        }
        if (matches) {
            if (result.paths.size() >= request.limit) { result.truncated = true; break; }
            result.paths.push_back(entry.path());
        }
        iterator.increment(error);
        if (error) {
            if (result.errors.size() < 100) result.errors << QString::fromStdString(error.message());
            error.clear();
        }
    }
    result.cancelled = control->cancelled;
    return result;
}
