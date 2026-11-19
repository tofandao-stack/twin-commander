#include "settings/panelpreferences.h"
#include "ui/panels/filepanellayout.h"
#include "settings/applicationpreferences.h"
#include "app/mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QRadioButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <tuple>

QString PanelPreferences::sizeText(quint64 bytes, int mode) {
    QSettings s;
    if (mode < 0)
        mode = s.value("format/size", 0).toInt();
    QString text;
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    if (mode == 0)
        text = locale.toString(bytes);
    else if (mode == 1)
        text = locale.toString(bytes / 1024) + " k";
    else if (mode == 2)
        text = locale.toString(bytes / (1024 * 1024)) + " M";
    else {
        static const QStringList units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
        double value = bytes;
        int unit = 0;
        while (value >= 1024 && unit < units.size() - 1) {
            value /= 1024;
            ++unit;
        }
        text = locale.toString(value, 'f', unit ? 1 : 0) + " " + units[unit];
    }
    const auto thousands = s.value("format/thousands", ",").toString();
    const auto decimal = s.value("format/decimal", ".").toString();
    text.replace(',', QChar(0x1f));
    text.replace('.', decimal);
    text.replace(QChar(0x1f), thousands);
    return text;
}
QString PanelPreferences::dateText(const QDateTime &date) {
    QSettings s;
    const QString year = s.value("format/century", true).toBool() ? "yyyy" : "yy";
    const auto ds = s.value("format/dateSeparator", "-").toString();
    const auto ts = s.value("format/timeSeparator", ":").toString();
    const QStringList dates{year + "-MM-dd", "dd-MM-" + year, "MM-dd-" + year};
    auto pattern = dates.value(s.value("format/dateOrder", 0).toInt(), dates.front());
    // Separators are literal choices, not arbitrary Qt format strings.
    pattern.replace("-", "'" + ds + "'");
    const bool ampm = s.value("format/ampm", false).toBool();
    QString time = ampm ? "hh" : "HH";
    time += "'" + ts + "'mm";
    if (s.value("format/seconds", false).toBool())
        time += "'" + ts + "'ss";
    if (ampm)
        time += " AP";
    return date.toString(pattern + " " + time);
}
void PanelPreferences::saveFrequent() {
    QSettings settings;
    const auto usage = qApp->property("frequentUsage").toMap();
    std::vector<std::pair<int, QString>> ranked;
    for (auto it = usage.begin(); it != usage.end(); ++it)
        ranked.emplace_back(it.value().toInt(), it.key());
    std::sort(ranked.rbegin(), ranked.rend());
    QVariantMap saved;
    const int limit = qBound(1, settings.value("frequent/saveLimit", 100).toInt(), 1000);
    for (int i = 0; i < qMin(limit, int(ranked.size())); ++i)
        saved[ranked[i].second] = ranked[i].first;
    const auto location = ApplicationPreferences::text("DirUsageLocationCombo", "Settings");
    if (location == "Session only")
        qApp->setProperty("sessionFrequent", saved);
    else if (location == "JSON data file") {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        QSaveFile file(dir + "/frequent.json");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument::fromVariant(saved).toJson());
            file.commit();
        }
    } else
        settings.setValue("frequent/saved", saved);
}
QVariantMap PanelPreferences::frequent() {
    const auto location = ApplicationPreferences::text("DirUsageLocationCombo", "Settings");
    if (location == "Session only")
        return qApp->property("sessionFrequent").toMap();
    if (location == "JSON data file") {
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/frequent.json");
        if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024)
            return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        return {};
    }
    return QSettings().value("frequent/saved").toMap();
}
void PanelPreferences::clearFrequent() {
    qApp->setProperty("sessionFrequent", QVariantMap{});
    qApp->setProperty("frequentUsage", QVariantMap{});
    QSettings().remove("frequent/saved");
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/frequent.json");
}
