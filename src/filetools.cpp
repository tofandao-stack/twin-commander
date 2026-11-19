#include "filetools.h"
#include "directorymodel.h"
#include "finalsettings.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace FileTools {
namespace {
using namespace NativeFs;
using LocalFiles::checkpoint;
constexpr size_t entryLimit = 200000;
using Tree = std::map<fs::path, struct stat>;
Tree scan(const fs::path &root, QStringList &errors, bool &truncated,
          const std::shared_ptr<LocalFiles::Control> &control) {
    Tree tree;
    std::error_code error;
    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error),
        end;
    if (error) {
        errors << displayPath(root) + ": " + QString::fromStdString(error.message());
        return tree;
    }
    while (iterator != end) {
        checkpoint(control);
        if (tree.size() >= entryLimit) {
            truncated = true;
            break;
        }
        const auto path = iterator->path();
        if (iterator.depth() >= 128) {
            iterator.disable_recursion_pending();
            truncated = true;
        }
        try {
            const auto st = inspect(path);
            if (!S_ISDIR(st.st_mode))
                iterator.disable_recursion_pending();
            else {
                const int directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (directory < 0) {
                    if (errors.size() < 100)
                        errors << displayPath(path) + ": directory cannot be read";
                    iterator.disable_recursion_pending();
                } else
                    ::close(directory);
            }
            tree.emplace(path.lexically_relative(root), st);
        } catch (const std::exception &failure) {
            if (errors.size() < 100)
                errors << displayPath(path) + ": " + QString::fromLocal8Bit(failure.what());
            iterator.disable_recursion_pending();
        }
        ++control->completed;
        iterator.increment(error);
        if (error) {
            if (errors.size() < 100)
                errors << QString::fromStdString(error.message());
            error.clear();
        }
    }
    return tree;
}
void addStat(Statistics &result, const struct stat &st) {
    if (S_ISLNK(st.st_mode))
        ++result.links;
    else if (S_ISDIR(st.st_mode))
        ++result.directories;
    else if (S_ISREG(st.st_mode)) {
        ++result.files;
        result.bytes += static_cast<quint64>(st.st_size);
        result.allocated += static_cast<quint64>(st.st_blocks) * 512;
    }
}
void ensureParents(const fs::path &root, const fs::path &relative) {
    Fd parent(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto &component : relative) {
        if (component == "." || component == ".." || component.empty())
            throw Error("Invalid synchronization path");
        if (::mkdirat(parent.value, component.c_str(), 0755) < 0 && errno != EEXIST)
            check(-1);
        const int child =
            ::openat(parent.value, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        check(child);
        ::close(parent.value);
        parent.value = child;
    }
}
} // namespace
Statistics statistics(const std::vector<fs::path> &paths,
                      const std::shared_ptr<LocalFiles::Control> &control) {
    Statistics result;
    std::set<fs::path> visited;
    try {
        for (const auto &path : paths) {
            checkpoint(control);
            try {
                if (!visited.insert(path).second)
                    continue;
                const auto st = inspect(path);
                addStat(result, st);
                if (S_ISDIR(st.st_mode)) {
                    const auto tree = scan(path, result.errors, result.truncated, control);
                    for (const auto &[relative, value] : tree)
                        if (visited.insert(path / relative).second)
                            addStat(result, value);
                }
            } catch (const std::exception &error) {
                if (result.errors.size() < 100)
                    result.errors << displayPath(path) + ": " + QString::fromLocal8Bit(error.what());
            }
        }
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    }
    return result;
}
FileComparison compareFiles(const fs::path &left, const fs::path &right,
                            const std::shared_ptr<LocalFiles::Control> &control) {
    FileComparison result;
    try {
        checkpoint(control);
        const auto a = inspect(left), b = inspect(right);
        if (!S_ISREG(a.st_mode) || !S_ISREG(b.st_mode))
            throw Error("Content comparison requires two regular files");
        Fd first(::open(left.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        Fd second(::open(right.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (!same(a, inspectFd(first.value)) || !same(b, inspectFd(second.value)))
            throw Error("File changed before opening");
        std::array<char, 64 * 1024> one{}, two{};
        auto readBlock = [](int fd, char *data, size_t length) {
            size_t count = 0;
            while (count < length) {
                const auto got = ::read(fd, data + count, length - count);
                if (got < 0 && errno == EINTR)
                    continue;
                check(static_cast<int>(got));
                if (!got)
                    break;
                count += static_cast<size_t>(got);
            }
            return count;
        };
        while (true) {
            checkpoint(control);
            const auto countA = readBlock(first.value, one.data(), one.size()),
                       countB = readBlock(second.value, two.data(), two.size());
            const auto length = std::min(countA, countB);
            const auto mismatch =
                std::mismatch(one.begin(), one.begin() + static_cast<ptrdiff_t>(length), two.begin());
            const auto offset = static_cast<size_t>(mismatch.first - one.begin());
            control->bytes += countA + countB;
            if (offset != length || countA != countB) {
                result.offset += offset;
                break;
            }
            if (!countA) {
                result.equal = true;
                break;
            }
            result.offset += countA;
        }
        verify(left, first.value, a);
        verify(right, second.value, b);
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.error = QString::fromLocal8Bit(error.what());
    }
    return result;
}
QString differenceText(Difference difference) {
    switch (difference) {
    case Difference::Equal:
        return "Equal";
    case Difference::LeftOnly:
        return "Left only";
    case Difference::RightOnly:
        return "Right only (retained)";
    case Difference::Changed:
        return "Different";
    case Difference::Conflict:
        return "Type conflict / unsupported";
    }
    return {};
}
DirectoryComparison compareDirectories(const fs::path &left, const fs::path &right, bool contents,
                                       const std::shared_ptr<LocalFiles::Control> &control) {
    DirectoryComparison result;
    try {
        result.left = fs::canonical(left);
        result.right = fs::canonical(right);
        result.leftRoot = inspect(result.left);
        result.rightRoot = inspect(result.right);
        auto a = scan(result.left, result.errors, result.truncated, control);
        auto b = scan(result.right, result.errors, result.truncated, control);
        std::set<fs::path> paths;
        for (const auto &[path, st] : a) {
            (void)st;
            paths.insert(path);
        }
        for (const auto &[path, st] : b) {
            (void)st;
            paths.insert(path);
        }
        for (const auto &path : paths) {
            checkpoint(control);
            CompareRow row;
            row.relative = path;
            const bool haveA = a.contains(path), haveB = b.contains(path);
            if (haveA)
                row.left = a[path];
            if (haveB)
                row.right = b[path];
            row.directory = haveA ? S_ISDIR(row.left.st_mode) : S_ISDIR(row.right.st_mode);
            if (!haveA)
                row.difference = Difference::RightOnly;
            else if (!haveB)
                row.difference = Difference::LeftOnly;
            else if ((row.left.st_mode & S_IFMT) != (row.right.st_mode & S_IFMT))
                row.difference = Difference::Conflict;
            else if (S_ISDIR(row.left.st_mode))
                row.difference = Difference::Equal;
            else if (S_ISREG(row.left.st_mode)) {
                bool equal = row.left.st_size == row.right.st_size;
                if (equal && contents) {
                    const auto comparison = compareFiles(result.left / path, result.right / path, control);
                    if (comparison.cancelled)
                        throw LocalFiles::Cancelled{};
                    if (!comparison.error.isEmpty()) {
                        result.errors << displayPath(path) + ": " + comparison.error;
                        row.difference = Difference::Conflict;
                        result.rows.push_back(row);
                        continue;
                    }
                    equal = comparison.equal;
                } else if (equal)
                    equal = (row.left.st_mtim.tv_sec == row.right.st_mtim.tv_sec ||
                             (FinalSettings::flag("Idconf_1hourdif") &&
                              std::llabs(row.left.st_mtim.tv_sec - row.right.st_mtim.tv_sec) == 3600)) &&
                            row.left.st_mtim.tv_nsec == row.right.st_mtim.tv_nsec;
                row.difference = equal ? Difference::Equal : Difference::Changed;
            } else if (S_ISLNK(row.left.st_mode)) {
                row.difference = fs::read_symlink(result.left / path) == fs::read_symlink(result.right / path)
                                     ? Difference::Equal
                                     : Difference::Changed;
            } else
                row.difference = Difference::Conflict;
            row.actionable = haveA && (S_ISREG(row.left.st_mode) || S_ISDIR(row.left.st_mode)) &&
                             (row.difference == Difference::LeftOnly ||
                              (row.difference == Difference::Changed && haveB && S_ISREG(row.right.st_mode)));
            result.rows.push_back(std::move(row));
        }
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.errors << QString::fromLocal8Bit(error.what());
    }
    return result;
}
LocalFiles::Result synchronize(const DirectoryComparison &plan, const std::vector<size_t> &selected,
                               const std::shared_ptr<LocalFiles::Control> &control,
                               const QString &journalDirectory) {
    LocalFiles::Result result;
    try {
        checkpoint(control);
        if (plan.cancelled || plan.truncated || !plan.errors.empty())
            throw Error("Rescan: incomplete comparison cannot be synchronized");
        if (inside(plan.left, plan.right) || inside(plan.right, plan.left))
            throw Error("Synchronization roots must be separate non-overlapping directories");
        if (fs::canonical(plan.left) != plan.left || fs::canonical(plan.right) != plan.right ||
            !same(plan.leftRoot, inspect(plan.left)) || !same(plan.rightRoot, inspect(plan.right)))
            throw Error("Synchronization root changed; rescan required");
        std::set<size_t> unique(selected.begin(), selected.end());
        // Validate the entire user-approved selection before creating any output.
        for (const auto index : unique) {
            if (index >= plan.rows.size() || !plan.rows[index].actionable)
                throw Error("Invalid synchronization selection");
            const auto &row = plan.rows[index];
            if (row.relative.empty() || row.relative.is_absolute())
                throw Error("Invalid relative synchronization path");
            for (const auto &component : row.relative)
                if (component == ".." || component == ".")
                    throw Error("Unsafe synchronization path");
            if (!same(row.left, inspect(plan.left / row.relative)))
                throw Error("Source changed since comparison; rescan required");
            if (row.difference == Difference::LeftOnly) {
                struct stat st {};
                if (::lstat((plan.right / row.relative).c_str(), &st) == 0 || errno != ENOENT)
                    throw Error("Destination changed since comparison; rescan required");
            } else if (!same(row.right, inspect(plan.right / row.relative)))
                throw Error("Destination changed since comparison; rescan required");
        }
        std::vector<size_t> ordered(unique.begin(), unique.end());
        std::stable_sort(ordered.begin(), ordered.end(), [&](size_t a, size_t b) {
            if (plan.rows[a].directory != plan.rows[b].directory)
                return plan.rows[a].directory;
            return plan.rows[a].relative < plan.rows[b].relative;
        });
        for (const auto index : ordered) {
            checkpoint(control);
            const auto &row = plan.rows[index];
            const auto target = plan.right / row.relative;
            if (!same(row.left, inspect(plan.left / row.relative)))
                throw Error("Source changed during synchronization; remaining work stopped");
            ensureParents(plan.right, row.directory ? row.relative : row.relative.parent_path());
            if (row.directory) {
                result.items.push_back({plan.left / row.relative, target, {}, false});
                ++control->completed;
                continue;
            }
            if (row.difference == Difference::Changed && !same(row.right, inspect(target)))
                throw Error("Destination changed during synchronization; remaining work stopped");
            auto copied = LocalFiles::execute({LocalFiles::Operation::Copy,
                                               {plan.left / row.relative},
                                               target.parent_path(),
                                               row.difference == Difference::Changed
                                                   ? LocalFiles::Conflict::ReplaceFiles
                                                   : LocalFiles::Conflict::Fail,
                                               journalDirectory},
                                              control);
            result.items.insert(result.items.end(), copied.items.begin(), copied.items.end());
            if (!copied.journalError.isEmpty())
                result.journalError += copied.journalError + "\n";
            if (copied.cancelled)
                throw LocalFiles::Cancelled{};
            if (std::any_of(copied.items.begin(), copied.items.end(),
                            [](const auto &item) { return !item.error.isEmpty(); }))
                break;
        }
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.items.push_back({plan.left, plan.right, QString::fromLocal8Bit(error.what()), false});
    }
    return result;
}
Duplicates duplicates(const fs::path &root, const std::shared_ptr<LocalFiles::Control> &control) {
    Duplicates result;
    try {
        const auto base = fs::canonical(root);
        const auto tree = scan(base, result.errors, result.truncated, control);
        std::map<quint64, std::vector<fs::path>> sizes;
        std::set<std::pair<dev_t, ino_t>> identities;
        for (const auto &[relative, st] : tree)
            if (S_ISREG(st.st_mode) && identities.insert({st.st_dev, st.st_ino}).second)
                sizes[static_cast<quint64>(st.st_size)].push_back(base / relative);
        for (const auto &[size, paths] : sizes) {
            if (paths.size() < 2)
                continue;
            std::map<QByteArray, std::vector<fs::path>> hashes;
            for (const auto &path : paths) {
                checkpoint(control);
                try {
                    hashes[NativeFs::hash(path, control, &tree.at(path.lexically_relative(base)))].push_back(
                        path);
                } catch (const std::exception &error) {
                    if (result.errors.size() < 100)
                        result.errors << displayPath(path) + ": " + QString::fromLocal8Bit(error.what());
                }
            }
            for (auto &[digest, matches] : hashes)
                if (matches.size() > 1) {
                    if (result.groups.size() >= 5000) {
                        result.truncated = true;
                        return result;
                    }
                    result.groups.push_back({std::move(matches), size, digest});
                }
        }
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.errors << QString::fromLocal8Bit(error.what());
    }
    return result;
}
} // namespace FileTools
