#pragma once
#include "nativefs.h"
#include <QStringList>

namespace BatchRename {
enum class LetterCase { Preserve, Lower, Upper, Capitalize, Title };
struct Rule {
    QString pattern = "{name}{ext}";
    QString find, replacement;
    bool regex = false;
    LetterCase letterCase = LetterCase::Preserve;
    qint64 start = 1, step = 1;
    int width = 3;
    bool commanderMasks = false;
    QString extensionPattern = "[E]";
    std::shared_ptr<std::atomic_bool> cancellation{};
    bool autoWidth = false, caseSensitiveFind = true, replaceExtension = false;
};
struct Item {
    std::filesystem::path source, target;
    struct stat before {};
};
struct Plan {
    std::vector<Item> items;
    QStringList errors;
};
Plan preview(const std::vector<std::filesystem::path> &sources, const Rule &rule);
LocalFiles::Result apply(const Plan &plan, const std::shared_ptr<LocalFiles::Control> &control,
                         const QString &journalDirectory = {}, const std::shared_ptr<Plan> &inverse = {});
} // namespace BatchRename
