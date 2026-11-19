#pragma once
#include <QString>
#include <filesystem>

QString displayPath(const std::filesystem::path &path);
std::filesystem::path nativePath(const QString &path);
