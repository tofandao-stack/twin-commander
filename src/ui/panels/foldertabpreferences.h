#pragma once
#include <QPoint>
class QTabWidget;
namespace FolderTabPreferences {
void showContextMenu(QTabWidget *tabs, int index, const QPoint &position);
}
