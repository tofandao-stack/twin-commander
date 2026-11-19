#pragma once
#include <QString>
#include <QtGlobal>
class QDateTime;
namespace Preferences {
QString sizeText(quint64 bytes, int mode = -1);
QString dateText(const QDateTime &date);
void saveFrequent();
} // namespace Preferences
