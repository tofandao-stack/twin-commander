#pragma once
#include <QString>
class QDialog;
namespace ApplicationPreferences {
bool flag(const char *id);
QString text(const char *id, const QString &fallback = {});
int number(const char *id, int fallback);
void configure(QDialog *dialog);
void apply();
bool instanceLock();
QString translated(const QString &source);
} // namespace ApplicationPreferences
