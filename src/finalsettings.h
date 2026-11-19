#pragma once
#include "directorymodel.h"
#include "filetools.h"
#include <QImage>
#include <QMimeData>
#include <QVariantMap>
class QDialog;
class QTabWidget;
class QEvent;
namespace FinalSettings {
bool flag(const char *id);
QString text(const char *id, const QString &fallback = {});
int number(const char *id, int fallback);
void configure(QDialog *dialog);
void apply();
bool instanceLock();
QString translated(const QString &source);
QString comment(const std::filesystem::path &path);
void setComment(const std::filesystem::path &path, const QString &value);
QString alias83(const QString &name, int serial = 1);
QByteArray dragType();
QMimeData *dragData(const std::vector<std::filesystem::path> &paths);
std::vector<std::filesystem::path> dropPaths(const QMimeData *data);
void tabMenu(QTabWidget *tabs, int index, const QPoint &position);
FileTools::Statistics sizes(const std::vector<std::filesystem::path> &paths,
                            const std::shared_ptr<LocalFiles::Control> &control);
bool countBeforeOperation(const std::vector<std::filesystem::path> &paths, QWidget *parent, QString &summary);
QImage externalThumbnail(const std::filesystem::path &, const QSize &);
void pruneUnmountedThumbnails();
void synchronizeThumbnails(const std::filesystem::path &source, const std::filesystem::path &destination,
                           bool removed);
} // namespace FinalSettings
