#include "batchrename.h"
#include "directorymodel.h"
#include "nativesettings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <set>

namespace BatchRename {
using namespace NativeFs;
namespace {
QString expandMask(const QString &mask, const QString &name, const QString &extension, const fs::path &path,
                   const struct stat &st, size_t index, const Rule &rule) {
    const auto date = QDateTime::fromSecsSinceEpoch(st.st_mtime);
    QString output;
    for (qsizetype offset = 0; offset < mask.size();) {
        if (mask[offset] != '[') {
            if (mask[offset] == ']')
                throw Error("Unbalanced rename mask");
            output += mask[offset++];
            continue;
        }
        if (mask.mid(offset, 3) == "[[]") {
            output += '[';
            offset += 3;
            continue;
        }
        if (mask.mid(offset, 3) == "[]]") {
            output += ']';
            offset += 3;
            continue;
        }
        const auto end = mask.indexOf(']', offset);
        if (end < 0)
            throw Error("Unbalanced rename mask");
        const auto token = mask.mid(offset + 1, end - offset - 1);
        offset = end + 1;
        if (token == "C") {
            output += QString::number(rule.start + static_cast<qint64>(index) * rule.step)
                          .rightJustified(rule.width, '0');
            continue;
        }
        if (token.startsWith('=')) {
            output += NativeSettings::field(path, token.mid(1));
            continue;
        }
        if (token == "S") {
            output += QString::number(st.st_size);
            continue;
        }
        if (!token.isEmpty() && QRegularExpression("^[YyMDhms]+$").match(token).hasMatch()) {
            for (const auto letter : token) {
                const QString format = letter == 'Y'   ? "yyyy"
                                       : letter == 'y' ? "yy"
                                       : letter == 'M' ? "MM"
                                       : letter == 'D' ? "dd"
                                       : letter == 'h' ? "HH"
                                       : letter == 'm' ? "mm"
                                                       : "ss";
                output += date.toString(format);
            }
            continue;
        }
        const auto range =
            QRegularExpression("^([NEP]?)(-?[1-9][0-9]*)?(?:(-|,)(-?[1-9][0-9]*)?)?$").match(token);
        if (!range.hasMatch() || token.isEmpty())
            throw Error(("Unsupported rename token: [" + token + "]").toStdString());
        QString value = range.captured(1) == "N"   ? name
                        : range.captured(1) == "E" ? extension
                        : range.captured(1) == "P" ? displayPath(path.parent_path().filename())
                                                   : displayPath(path);
        if (range.captured(2).isEmpty()) {
            if (range.captured(1).isEmpty())
                throw Error("Invalid range token");
            output += value;
            continue;
        }
        const auto points = value.toUcs4();
        const qint64 length = points.size();
        const auto locate = [length](qint64 position) {
            return position > 0 ? position - 1 : length + position;
        };
        const qint64 first = locate(range.captured(2).toLongLong());
        qint64 last = first;
        if (range.captured(3) == ",") {
            if (range.captured(4).isEmpty() || range.captured(4).toLongLong() < 1)
                throw Error("Invalid range length");
            last = first + range.captured(4).toLongLong() - 1;
        } else if (range.captured(3) == "-")
            last = range.captured(4).isEmpty() ? length - 1 : locate(range.captured(4).toLongLong());
        if (first >= 0 && first < length && last >= first) {
            const auto count = std::min(last, length - 1) - first + 1;
            const std::u32string unicode(points.begin() + first, points.begin() + first + count);
            output += QString::fromUcs4(unicode.data(), unicode.size());
        }
    }
    return output;
}
} // namespace
Plan preview(const std::vector<fs::path> &sources, const Rule &rule) {
    Plan result;
    try {
        if (sources.empty() || sources.size() > 10000)
            throw Error("Select between 1 and 10000 entries");
        if (rule.start < 0 || rule.start > 1000000000 || rule.step < 1 || rule.step > 1000000 ||
            rule.width < 1 || rule.width > 12)
            throw Error("Invalid counter parameters");
        Rule expanded = rule;
        if (rule.autoWidth)
            expanded.width =
                QString::number(rule.start + static_cast<qint64>(sources.size() - 1) * rule.step).size();
        const QRegularExpression expression(rule.find, rule.caseSensitiveFind
                                                           ? QRegularExpression::NoPatternOption
                                                           : QRegularExpression::CaseInsensitiveOption);
        if (rule.regex && !expression.isValid())
            throw Error(expression.errorString().toStdString());
        const auto parent = fs::canonical(sources.front().parent_path());
        std::set<fs::path> originals, targets;
        for (size_t index = 0; index < sources.size(); ++index) {
            if (rule.cancellation && rule.cancellation->load())
                throw Error("Preview cancelled");
            const auto &path = sources[index];
            if (!path.is_absolute() || path.filename().empty() || path.filename() == "." ||
                path.filename() == ".." || fs::canonical(path.parent_path()) != parent)
                throw Error("Batch rename requires entries in one directory");
            const auto raw = path.filename().native();
            if (QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size())).toUtf8().toStdString() !=
                raw)
                throw Error("Batch name transformations require lossless UTF-8 filenames");
            const auto source = parent / path.filename();
            if (!originals.insert(source).second)
                throw Error("Duplicate source entry");
            const auto before = inspect(source);
            const auto filename = displayPath(path.filename());
            const auto dot = filename.lastIndexOf('.');
            const auto name = dot > 0 ? filename.left(dot) : filename;
            auto extension = dot > 0 ? filename.mid(dot) : QString();
            auto changed = name;
            if (!rule.find.isEmpty()) {
                if (rule.regex)
                    changed.replace(expression, rule.replacement);
                else
                    changed.replace(rule.find, rule.replacement,
                                    rule.caseSensitiveFind ? Qt::CaseSensitive : Qt::CaseInsensitive);
            }
            if (rule.replaceExtension && !rule.find.isEmpty()) {
                auto suffix = extension.mid(1);
                if (rule.regex)
                    suffix.replace(expression, rule.replacement);
                else
                    suffix.replace(rule.find, rule.replacement,
                                   rule.caseSensitiveFind ? Qt::CaseSensitive : Qt::CaseInsensitive);
                extension = suffix.isEmpty() ? QString() : "." + suffix;
            }
            QString output;
            if (rule.commanderMasks) {
                output = expandMask(rule.pattern, changed, extension.mid(1), path, before, index, expanded);
                const auto suffix =
                    expandMask(rule.extensionPattern, name, extension.mid(1), path, before, index, expanded);
                if (!suffix.isEmpty())
                    output += "." + suffix;
            } else {
                // Expand once so tokens present in an original filename stay literal.
                const QRegularExpression tokens("\\{(name|ext|extname|n)\\}");
                auto matches = tokens.globalMatch(rule.pattern);
                qsizetype offset = 0;
                while (matches.hasNext()) {
                    const auto match = matches.next();
                    output += rule.pattern.mid(offset, match.capturedStart() - offset);
                    output += match.captured(1) == "name"  ? changed
                              : match.captured(1) == "ext" ? extension
                              : match.captured(1) == "extname"
                                  ? extension.mid(1)
                                  : QString::number(rule.start + static_cast<qint64>(index) * rule.step)
                                        .rightJustified(expanded.width, '0');
                    offset = match.capturedEnd();
                }
                output += rule.pattern.mid(offset);
            }
            if (rule.letterCase == LetterCase::Lower)
                output = output.toLower();
            if (rule.letterCase == LetterCase::Upper)
                output = output.toUpper();
            if (rule.letterCase == LetterCase::Capitalize) {
                output = output.toLower();
                if (!output.isEmpty()) {
                    const int size = output[0].isHighSurrogate() && output.size() > 1 ? 2 : 1;
                    output = output.left(size).toUpper() + output.mid(size);
                }
            }
            if (rule.letterCase == LetterCase::Title) {
                const auto original = output.toLower();
                const QRegularExpression words("(\\p{L})([\\p{L}\\p{M}]*)");
                auto matches = words.globalMatch(original);
                output.clear();
                qsizetype offset = 0;
                while (matches.hasNext()) {
                    const auto match = matches.next();
                    output += original.mid(offset, match.capturedStart() - offset) +
                              match.captured(1).toUpper() + match.captured(2);
                    offset = match.capturedEnd();
                }
                output += original.mid(offset);
            }
            if (output.isEmpty() || output == "." || output == ".." || output.contains('/') ||
                output.contains(QChar::Null) || output.toUtf8().size() > 255)
                throw Error("Generated filename is invalid or longer than 255 UTF-8 bytes");
            const auto target = parent / nativePath(output);
            if (!targets.insert(target).second)
                throw Error("Two selected entries would have the same destination");
            result.items.push_back({source, target, before});
        }
        for (const auto &item : result.items) {
            struct stat existing {};
            if (::lstat(item.target.c_str(), &existing) == 0) {
                if (!originals.contains(item.target))
                    throw Error(("Destination already exists: " + displayPath(item.target)).toStdString());
            } else if (errno != ENOENT)
                check(-1);
        }
    } catch (const std::exception &error) {
        result.errors << QString::fromLocal8Bit(error.what());
    }
    return result;
}
LocalFiles::Result apply(const Plan &plan, const std::shared_ptr<LocalFiles::Control> &control,
                         const QString &journalDirectory, const std::shared_ptr<Plan> &inverse) {
    LocalFiles::Result result;
    if (inverse) {
        inverse->items.clear();
        inverse->errors.clear();
    }
    if (!plan.errors.empty() || plan.items.empty()) {
        result.items.push_back({{}, {}, "A valid non-empty preview is required", false});
        return result;
    }
    struct Work {
        Item item;
        std::string temporary;
        int phase = 0;
    };
    std::vector<Work> work;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QString error;
    QFile events;
    const auto parentPath = plan.items.front().source.parent_path();
    const auto journal = [&](bool finished) {
        if (journalDirectory.isEmpty())
            return;
        QJsonArray entries;
        const auto bytes = [](const fs::path &path) {
            return QString::fromLatin1(QByteArray::fromStdString(path.native()).toBase64());
        };
        for (const auto &entry : work)
            entries.append(QJsonObject{{"sourceBase64", bytes(entry.item.source)},
                                       {"destinationBase64", bytes(entry.item.target)},
                                       {"temporaryBase64", bytes(parentPath / entry.temporary)},
                                       {"phase", entry.phase}});
        QJsonObject data{{"version", 1},
                         {"kind", "batch-rename"},
                         {"outcomeLog", "rename-" + QString::fromStdString(id) + ".jsonl"},
                         {"finished", finished},
                         {"error", error},
                         {"items", entries}};
        QDir().mkpath(journalDirectory);
        QSaveFile file(journalDirectory + "/rename-" + QString::fromStdString(id) + ".json");
        const auto payload = QJsonDocument(data).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit())
            result.journalError = "Could not persist batch rename journal";
    };
    const auto record = [&](const Work &entry) {
        if (!events.isOpen())
            return;
        const auto path = [](const fs::path &value) {
            return QString::fromLatin1(QByteArray::fromStdString(value.native()).toBase64());
        };
        const auto bytes = QJsonDocument(QJsonObject{{"sourceBase64", path(entry.item.source)},
                                                     {"destinationBase64", path(entry.item.target)},
                                                     {"temporaryBase64", path(parentPath / entry.temporary)},
                                                     {"phase", entry.phase}})
                               .toJson(QJsonDocument::Compact) +
                           '\n';
        if (events.write(bytes) != bytes.size() || !events.flush())
            result.journalError = "Could not append batch rename phase log";
    };
    try {
        LocalFiles::checkpoint(control);
        Fd parent(::open(parentPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        std::set<fs::path> originals, targets;
        for (const auto &item : plan.items) {
            if (item.source.parent_path() != parentPath || item.target.parent_path() != parentPath ||
                item.source.filename().empty() || item.target.filename().empty() ||
                item.target.filename() == "." || item.target.filename() == "..")
                throw Error("Invalid batch rename plan");
            if (!originals.insert(item.source).second || !targets.insert(item.target).second ||
                !same(item.before, inspect(item.source)))
                throw Error("Sources changed or plan contains duplicate paths; preview again");
        }
        for (const auto &item : plan.items) {
            struct stat st {};
            const auto found = ::lstat(item.target.c_str(), &st);
            if (found == 0 && !originals.contains(item.target))
                throw Error("Destination changed since preview");
            if (found < 0 && errno != ENOENT)
                check(-1);
            if (item.source != item.target)
                work.push_back({item, ".tc-rename-" + id + "-" + std::to_string(work.size()), 0});
        }
        journal(false);
        if (!result.journalError.isEmpty())
            throw Error(result.journalError.toStdString());
        if (!journalDirectory.isEmpty()) {
            events.setFileName(journalDirectory + "/rename-" + QString::fromStdString(id) + ".jsonl");
            if (!events.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                throw Error("Cannot create batch rename phase log");
        }
        try {
            for (auto &entry : work) {
                LocalFiles::checkpoint(control);
                if (!same(entry.item.before, inspect(entry.item.source)))
                    throw Error("Source changed during batch rename");
                renameExclusive(parent.value, entry.item.source.filename().native(), entry.temporary);
                entry.phase = 1;
                record(entry);
                if (!result.journalError.isEmpty())
                    throw Error(result.journalError.toStdString());
            }
            for (auto &entry : work) {
                LocalFiles::checkpoint(control);
                const auto staged = inspect(parentPath / entry.temporary);
                if (staged.st_dev != entry.item.before.st_dev || staged.st_ino != entry.item.before.st_ino ||
                    staged.st_size != entry.item.before.st_size ||
                    staged.st_mode != entry.item.before.st_mode ||
                    staged.st_mtim.tv_sec != entry.item.before.st_mtim.tv_sec ||
                    staged.st_mtim.tv_nsec != entry.item.before.st_mtim.tv_nsec)
                    throw Error("Staged entry changed during batch rename");
                renameExclusive(parent.value, entry.temporary, entry.item.target.filename().native());
                entry.phase = 2;
                ++control->completed;
                record(entry);
                if (inverse)
                    inverse->items.push_back(
                        {entry.item.target, entry.item.source, inspect(entry.item.target)});
                if (!result.journalError.isEmpty())
                    throw Error(result.journalError.toStdString());
            }
        } catch (...) {
            // First vacate published targets, then restore original names. Never overwrite an external entry.
            for (auto &entry : work)
                if (entry.phase == 2) {
                    try {
                        const auto current = inspect(entry.item.target);
                        if (current.st_ino != entry.item.before.st_ino ||
                            current.st_dev != entry.item.before.st_dev)
                            throw Error("Published entry was replaced externally");
                        renameExclusive(parent.value, entry.item.target.filename().native(), entry.temporary);
                        entry.phase = 1;
                        record(entry);
                    } catch (const std::exception &failure) {
                        result.items.push_back({entry.item.target, parentPath / entry.temporary,
                                                "Rollback failed: " + QString::fromLocal8Bit(failure.what()),
                                                false});
                    }
                }
            for (auto &entry : work)
                if (entry.phase == 1) {
                    try {
                        const auto current = inspect(parentPath / entry.temporary);
                        if (current.st_ino != entry.item.before.st_ino ||
                            current.st_dev != entry.item.before.st_dev)
                            throw Error("Temporary entry was replaced externally");
                        renameExclusive(parent.value, entry.temporary, entry.item.source.filename().native());
                        entry.phase = 0;
                        record(entry);
                    } catch (const std::exception &failure) {
                        result.items.push_back({parentPath / entry.temporary, entry.item.source,
                                                "Restore failed; recover this temporary entry: " +
                                                    QString::fromLocal8Bit(failure.what()),
                                                false});
                    }
                }
            throw;
        }
        for (const auto &item : plan.items)
            result.items.push_back({item.source, item.target, {}, item.source == item.target});
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
        error = "Batch cancelled; rollback attempted. Inspect any recovery errors.";
    } catch (const std::exception &failure) {
        error = QString::fromLocal8Bit(failure.what());
    }
    if (!error.isEmpty() && inverse) {
        inverse->items.clear();
        inverse->errors << error;
    }
    if (!error.isEmpty())
        result.items.push_back({parentPath, {}, error, false});
    journal(true);
    return result;
}
} // namespace BatchRename
