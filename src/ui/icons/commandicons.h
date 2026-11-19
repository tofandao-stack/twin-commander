#pragma once
#include <QIcon>
class QWidget;
namespace CommandIcons {
QIcon icon(const QString &name, int size = 24);
QIcon applicationIcon();
void decorate(QWidget *root);
void install();
} // namespace CommandIcons
