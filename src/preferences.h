#pragma once
#include <QString>
#include <QVariantMap>
#include <QtGlobal>
class QDateTime;
namespace Preferences {
QString sizeText(quint64 bytes, int mode = -1);
QString dateText(const QDateTime &date);
void saveFrequent();
QVariantMap frequent();
void clearFrequent();
} // namespace Preferences
