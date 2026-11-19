#pragma once
#include <QMimeData>
#include <filesystem>
#include <vector>
namespace NativePathMime {
QByteArray mimeType();
QMimeData *encode(const std::vector<std::filesystem::path> &paths);
std::vector<std::filesystem::path> decode(const QMimeData *data);
} // namespace NativePathMime
