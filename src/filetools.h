#pragma once
#include "nativefs.h"
#include <QStringList>

namespace FileTools {
struct Statistics {
    quint64 files = 0, directories = 0, links = 0, bytes = 0, allocated = 0;
    QStringList errors;
    bool cancelled = false, truncated = false;
};
Statistics statistics(const std::vector<std::filesystem::path> &paths, const std::shared_ptr<LocalFiles::Control> &control);
struct FileComparison { bool equal = false; quint64 offset = 0; QString error; bool cancelled = false; };
FileComparison compareFiles(const std::filesystem::path &left, const std::filesystem::path &right, const std::shared_ptr<LocalFiles::Control> &control);
enum class Difference { Equal, LeftOnly, RightOnly, Changed, Conflict };
struct CompareRow {
    std::filesystem::path relative;
    Difference difference = Difference::Equal;
    bool directory = false;
    bool actionable = false;
    struct stat left{}, right{};
};
struct DirectoryComparison {
    std::filesystem::path left, right;
    struct stat leftRoot{}, rightRoot{};
    std::vector<CompareRow> rows;
    QStringList errors;
    bool cancelled = false;
    bool truncated = false;
};
DirectoryComparison compareDirectories(const std::filesystem::path &left, const std::filesystem::path &right, bool contents,
                                      const std::shared_ptr<LocalFiles::Control> &control);
LocalFiles::Result synchronize(const DirectoryComparison &plan, const std::vector<size_t> &selected,
                              const std::shared_ptr<LocalFiles::Control> &control, const QString &journalDirectory = {});
struct DuplicateGroup { std::vector<std::filesystem::path> paths; quint64 size = 0; QByteArray sha256; };
struct Duplicates { std::vector<DuplicateGroup> groups; QStringList errors; bool cancelled = false, truncated = false; };
Duplicates duplicates(const std::filesystem::path &root, const std::shared_ptr<LocalFiles::Control> &control);
QString differenceText(Difference difference);
}
