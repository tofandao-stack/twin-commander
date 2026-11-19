#pragma once
#include <QStringList>
#include <QUrl>
#include <filesystem>
class QDialog;
class QWidget;
namespace InterfacePreferences {
void configure(QDialog *dialog);
void adaptDialog(QDialog *dialog);
bool matches(const std::filesystem::path &path, const char *setting);
} // namespace InterfacePreferences
