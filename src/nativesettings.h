#pragma once
#include <QStringList>
#include <QVariant>
#include <filesystem>
class QWidget;
namespace NativeSettings {
bool editRecords(QWidget *parent, const QString &title, const QString &key, const QStringList &columns,
                 const QString &fixedKind = {});
bool launchAssociated(const std::filesystem::path &path, const QString &kind);
QString mountUrl(const QString &uri);
QString field(const std::filesystem::path &path, const QString &name);
QVariantList plugins(const QString &kind = {});
void configurePlugins(QWidget *parent, const QString &kind = {});
} // namespace NativeSettings
