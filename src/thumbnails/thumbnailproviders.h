#pragma once
#include <QImage>
#include <filesystem>
namespace ThumbnailProviders {
QImage render(const std::filesystem::path &path, const QSize &size);
void synchronizeCache(const std::filesystem::path &source, const std::filesystem::path &destination,
                      bool removed);
void pruneUnmounted();
} // namespace ThumbnailProviders
