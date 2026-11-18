#pragma once
#include "fileoperations.h"
#include <QStringList>

struct SearchRequest {
    std::filesystem::path root;
    QString pattern = "*";
    QString content;
    bool caseSensitive = false;
    bool hidden = false;
    bool recursive = true;
    size_t limit = 5000;
    bool nameRegex = false;
    bool contentRegex = false;
    qint64 minBytes = -1, maxBytes = -1;
    qint64 modifiedAfter = -1, modifiedBefore = -1;
};
struct SearchResult {
    std::vector<std::filesystem::path> paths;
    QStringList errors;
    bool cancelled = false;
    bool truncated = false;
    quint64 inspected = 0;
    quint64 contentSkipped = 0;
};
SearchResult searchFiles(const SearchRequest &request, const std::shared_ptr<LocalFiles::Control> &control);
