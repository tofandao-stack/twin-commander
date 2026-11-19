#pragma once
#include <QString>
#include <filesystem>
namespace FileComments {
QString read(const std::filesystem::path &path);
void write(const std::filesystem::path &path, const QString &value);
} // namespace FileComments
