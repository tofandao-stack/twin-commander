#pragma once
#include "archives/archives.h"
namespace ExternalArchiveBackend {
bool external(const std::filesystem::path &path);
Archives::Listing list(const std::filesystem::path &, const std::shared_ptr<LocalFiles::Control> &,
                       const Archives::Limits &);
LocalFiles::Result extract(const std::filesystem::path &, const std::filesystem::path &,
                           const std::vector<std::string> &, const std::shared_ptr<LocalFiles::Control> &,
                           const Archives::Limits &);
LocalFiles::Result pack(const std::vector<std::filesystem::path> &, const std::filesystem::path &, bool zip,
                        QByteArray password, const std::shared_ptr<LocalFiles::Control> &);
QStringList sevenArguments(bool zip, bool encrypted);
} // namespace ExternalArchiveBackend
