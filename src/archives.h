#pragma once
#include "fileoperations.h"

namespace Archives {
struct Limits {
    quint64 bytes = 4ULL * 1024 * 1024 * 1024;
    quint64 fileBytes = 512ULL * 1024 * 1024;
    size_t entries = 100000;
};
struct Entry {
    std::string name;
    quint64 size = 0;
    bool directory = false;
    QString unsafe;
};
struct Listing {
    std::vector<Entry> entries;
    QString error;
    bool cancelled = false;
    QString warning{};
};
enum class Format { Zip, TarGzip, Tar };
Listing list(const std::filesystem::path &archive, const std::shared_ptr<LocalFiles::Control> &control,
             const Limits &limits = {});
LocalFiles::Result extract(const std::filesystem::path &archive, const std::filesystem::path &newDirectory,
                           const std::vector<std::string> &selected,
                           const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits = {});
LocalFiles::Result create(const std::vector<std::filesystem::path> &sources,
                          const std::filesystem::path &destination, Format format,
                          const std::shared_ptr<LocalFiles::Control> &control, const Limits &limits = {});
} // namespace Archives
