#pragma once
#include <QStringList>
#include <QUrl>
#include <filesystem>
class QDialog;
class QWidget;
namespace GapSettings {
void configure(QDialog *dialog);
void adaptDialog(QDialog *dialog);
bool matches(const std::filesystem::path &path, const char *setting);
QStringList curlOptions(const QUrl &url, bool transfer, bool upload);
void log(const QString &category, const QString &status, const QString &message);
void launch(const QString &program, const QString &parameters, const QString &workingDirectory);
} // namespace GapSettings
