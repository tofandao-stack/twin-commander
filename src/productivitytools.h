#pragma once
#include "nativefs.h"
namespace Productivity {
QByteArray selectionData(const std::vector<std::filesystem::path> &paths);
std::vector<std::filesystem::path> selectionPaths(const QByteArray &data, const std::filesystem::path &directory);
void exportNew(const std::filesystem::path &path, const QByteArray &data);
QByteArray readSmall(const std::filesystem::path &path, size_t limit = 8*1024*1024);
QString fileList(const std::vector<std::filesystem::path> &paths, bool recursive, const std::shared_ptr<LocalFiles::Control> &control);
QByteArray checksumManifest(const std::vector<std::filesystem::path> &paths, QCryptographicHash::Algorithm algorithm, const std::shared_ptr<LocalFiles::Control> &control);
QString verifyManifest(const std::filesystem::path &manifest, const std::shared_ptr<LocalFiles::Control> &control);
}
