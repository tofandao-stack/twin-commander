#include "preferences.h"
#include "classicui.h"
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QIntValidator>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QRadioButton>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <tuple>

QString Preferences::sizeText(quint64 bytes, int mode) {
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
QString Preferences::dateText(const QDateTime &date) {
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
void Preferences::saveFrequent() {
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
    settings.setValue("frequent/saved", saved);
}
void MainWindow::configureFunctionalOptions(QDialog *dialog) {
    const auto draft = [dialog](const QString &key, const QVariant &value) {
        auto map = dialog->property("appearanceDraft").toMap();
        map[key] = value;
        dialog->setProperty("appearanceDraft", map);
    };
    const auto check = [dialog, draft](const char *id, const char *key, bool fallback = false) {
        auto *w = dialog->findChild<QCheckBox *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip({});
        w->setChecked(QSettings().value(key, fallback).toBool());
        connect(w, &QCheckBox::toggled, dialog, [draft, key](bool value) { draft(key, value); });
    };
    const auto number = [dialog, draft](const char *id, const char *key, int fallback, int low, int high) {
        auto *w = dialog->findChild<QLineEdit *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip(QString("Range: %1–%2").arg(low).arg(high));
        w->setValidator(new QIntValidator(low, high, w));
        w->setText(QSettings().value(key, fallback).toString());
        connect(w, &QLineEdit::textChanged, dialog, [w, draft, key](const QString &text) {
            if (w->hasAcceptableInput())
                draft(key, text.toInt());
        });
    };
    const auto combo = [dialog, draft](const char *id, const char *key, const QStringList &labels,
                                       int fallback = 0) {
        auto *w = dialog->findChild<QComboBox *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip({});
        w->setEditable(false);
        w->clear();
        w->addItems(labels);
        w->setCurrentIndex(qBound(0, QSettings().value(key, fallback).toInt(), int(labels.size() - 1)));
        connect(w, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                [draft, key](int index) { draft(key, index); });
    };
    const auto choices = [dialog, draft](const char *key, const QStringList &ids, int fallback) {
        auto *group = new QButtonGroup(dialog);
        group->setExclusive(true);
        for (int i = 0; i < ids.size(); ++i)
            if (auto *w = dialog->findChild<QRadioButton *>(ids[i])) {
                w->setAutoExclusive(false);
                group->addButton(w, i);
                w->setEnabled(true);
                w->setToolTip({});
                w->setChecked(QSettings().value(key, fallback).toInt() == i);
            }
        connect(group, &QButtonGroup::idClicked, dialog, [draft, key](int id) { draft(key, id); });
    };
    check("IdConfshowhidden", "display/hidden");
    check("IdShowUpdir", "display/rootParent");
    check("IdDirBrackets", "display/brackets");
    check("IdDirBackslash", "display/directorySlash");
    check("IdBriefViewWidthLimit", "display/limitBrief");
    number("BriefViewWidthLimitEdit", "display/briefWidth", 190, 60, 1200);
    check("IdConf_tooltips", "display/tooltips", true);
    check("IdConf_NameTips", "display/nameTips", true);
    choices("display/directorySort", {"IdConfDirsByName", "IdConfDirsLikeFiles"}, 1);
    combo("SortMethodCombo", "display/sortMethod", {"Case sensitive", "Case insensitive", "Natural numeric"});
    check("IdDimmedHidden", "icons/dimHidden");
    check("IdShowOverlayIcons", "icons/overlay", true);
    check("Id3dFrame", "icons/frame");
    check("IdIconsForMenu", "icons/menu", true);
    choices("icons/mode", {"IdConfShowAllIcons", "IdConfshowStandardIcons", "IdConfShowNoIcons"}, 1);
    combo("idiconsize", "icons/size", {"16", "20", "24", "32", "48"});
    combo("idIconExtraX", "icons/extraX", {"0", "2", "4", "8", "16"});
    combo("idIconExtraY", "icons/extraY", {"0", "2", "4", "8", "16"});
    combo("IdMenuIconSize", "icons/menuSize", {"16", "20", "24", "32"});
    choices("format/extension", {"id_confExtAfterName", "id_confExtAligned"}, 1);
    for (const auto &row : {std::tuple{"id_tabExt", "columns/extension", 55},
                            {"id_tabSize", "columns/size", 90},
                            {"id_TabDate", "columns/date", 127},
                            {"id_TabAttr", "columns/attributes", 42},
                            {"id_TabComments", "columns/comment", 180}})
        number(std::get<0>(row), std::get<1>(row), std::get<2>(row), 20, 1200);
    check("id_ShowCentury", "format/century", true);
    check("id_UseAMPMFormat", "format/ampm");
    check("id_AdjustWidth", "columns/stretch", true);
    check("id_dircountinfooter", "format/directoryCount", true);
    const QStringList sizes{"Bytes", "KiB", "MiB", "Automatic binary units"};
    combo("id_sizeDisplay", "format/size", sizes);
    combo("id_size_footer", "format/footerSize", sizes, 1);
    combo("id_confDateFormat", "format/dateOrder",
          {"Year / month / day", "Day / month / year", "Month / day / year"});
    combo("id_confTimeFormat", "format/seconds", {"Hours and minutes", "Hours, minutes and seconds"});
    for (const auto &row : {std::pair{"id_sepDate", "format/dateSeparator"},
                            {"id_sepTime", "format/timeSeparator"},
                            {"id_sepThousands", "format/thousands"},
                            {"id_sepDecimal", "format/decimal"}}) {
        auto *w = dialog->findChild<QComboBox *>(row.first);
        w->setEnabled(true);
        w->setToolTip({});
        w->setEditable(false);
        w->clear();
        const QString key = row.second;
        const QStringList items = key.endsWith("dateSeparator")   ? QStringList{"-", "/", "."}
                                  : key.endsWith("timeSeparator") ? QStringList{":", "."}
                                  : key.endsWith("decimal")       ? QStringList{".", ","}
                                                                  : QStringList{",", ".", " ", ""};
        w->addItems(items);
        w->setCurrentIndex(qMax(0, items.indexOf(QSettings().value(key, items.front()).toString())));
        connect(w, &QComboBox::currentTextChanged, dialog,
                [draft, key](const QString &text) { draft(key, text); });
    }
    check("ChkTabsSameWidth", "tabs/sameWidth");
    check("ChkTabsLimitOption", "tabs/limit");
    number("EdtTabsLimitLength", "tabs/titleLength", 20, 3, 100);
    check("ChkTabsCloseDoubleClick", "tabs/doubleClose");
    check("ChkTabsOpenForeground", "tabs/foreground", true);
    check("ChkTabsOpenNearCurrent", "tabs/nearCurrent");
    check("ChkTabsAlwaysVisible", "classic/alwaysShowTabs");
    check("ChkTabsActivateTarget", "tabs/activate", true);
    check("ChkTabsIcons", "tabs/icons", true);
    check("ChkTabsOpenList", "tabs/list");
    check("ChkTabsLastVisitedFirst", "tabs/mru");
    check("IdConfsavedirs", "session/directories", true);
    check("IdConfsavepanels", "session/panels", true);
    check("IdConfCommandLines", "session/commands", true);
    choices("selection/directories", {"IdConfFilesOnly", "IdConfAlsoDirs"}, 1);
    choices("quick/mode",
            {"IdConfAltForMenu", "IdConfAltForQuickSearch", "IdConfLetterQuickSearch",
             "IdConfLetterDlgQuickSearch", "IdConfDisabledQuickSearch"},
            2);
    check("IdConfExactBeginning", "quick/beginning", true);
    check("IdConfExactEnding", "quick/ending");
    check("WatchDirsDetect", "refresh/names");
    check("WatchDirsSizeDateAttr", "refresh/metadata");
    check("WatchDirsAtEnd", "refresh/append");
    check("WatchDirsNoBackground", "refresh/foregroundOnly");
    check("WatchDirsFooter", "refresh/footer", true);
    check("WatchDirsHeader", "refresh/header", true);
    check("id_historythinning2", "history/thin", true);
    check("ShowHistoryIconsChk", "history/icons");
    check("SaveHistoryOnExitChk", "history/saveOnExit");
    check("ShowLastVisitedInMiddleChk", "history/recentMiddle");
    check("ShowFreqentlyUsedMaxInMiddleChk", "history/frequentMiddle");
    number("HistoryMaxSaveEdit", "history/saveLimit", 100, 1, 1000);
    number("HistoryMaxShowEdit", "history/showLimit", 30, 1, 1000);
    number("FrequentlyUsedMaxSaveEdit", "frequent/saveLimit", 100, 1, 1000);
    number("FrequentlyUsedMaxShowEdit", "frequent/showLimit", 20, 1, 1000);
    number("MaxWidthEdit", "history/width", 100, 20, 300);
    combo("ScrollListToCombo", "history/scroll", {"First item", "Most recent directory", "Frequently used"});
    auto *font = dialog->findChild<QComboBox *>("HistoryFontCombo");
    font->setEnabled(true);
    font->setToolTip({});
    font->setEditable(false);
    font->clear();
    font->addItems({"Application font", "Monospace", "Sans Serif", "Serif"});
    font->setCurrentIndex(QSettings().value("history/font", 0).toInt());
    connect(font, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
            [draft](int v) { draft("history/font", v); });
    check("id_ColorTypesEnabled", "appearance/typeColors", true);
    check("id_CursorInverted", "appearance/invertCursor");
    check("id_MarkInverted", "appearance/invertSelection");
    const QMap<QString, QString> colors{{"id_ForeColor", "textColor"},
                                        {"id_BackColor", "baseColor"},
                                        {"id_MarkColor", "markColor"},
                                        {"id_CursorColor", "cursorColor"},
                                        {"id_CursorFontColor", "cursorTextColor"},
                                        {"id_BackColor2", "alternateColor"}};
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        auto *w = dialog->findChild<QComboBox *>(it.key());
        w->setEnabled(true);
        w->setToolTip({});
        w->setEditable(true);
        w->clear();
        w->addItems({"#000000", "#ffffff", "#000080", "#be0000", "#eeeeee"});
        const auto key = "appearance/" + it.value();
        const QMap<QString, QString> defaults{{"textColor", "#000000"},       {"baseColor", "#ffffff"},
                                              {"markColor", "#be0000"},       {"cursorColor", "#000080"},
                                              {"cursorTextColor", "#ffffff"}, {"alternateColor", "#f2f3f7"}};
        w->setCurrentText(QSettings().value(key, defaults.value(it.value())).toString());
        connect(w, &QComboBox::currentTextChanged, dialog, [draft, key](const QString &value) {
            if (QColor(value).isValid())
                draft(key, value);
        });
    }
    check("AutoSwitchModeCheck", "autoView/enabled", true);
}

namespace {
class TooltipPreference : public QObject {
  public:
    explicit TooltipPreference(QObject *parent) : QObject(parent) {}
    bool eventFilter(QObject *, QEvent *event) override {
        return event->type() == QEvent::ToolTip && !QSettings().value("display/tooltips", true).toBool();
    }
};
} // namespace
void MainWindow::applyFunctionalPreferences(bool panels) {
    QSettings s;
    if (!qApp->property("preferenceTooltipFilter").toBool()) {
        qApp->installEventFilter(new TooltipPreference(qApp));
        qApp->setProperty("preferenceTooltipFilter", true);
    }
    const int menuSizes[]{16, 20, 24, 32};
    const int menuSize = menuSizes[qBound(0, s.value("icons/menuSize", 0).toInt(), 3)];
    for (auto *action : findChildren<QAction *>()) {
        if (!action->property("preferenceOriginalIcon").isValid())
            action->setProperty("preferenceOriginalIcon", action->icon());
        // Toolbar proxies have their own presentation; change only menu commands.
        if (action->property("tcCommandId").isValid()) {
            auto icon = qvariant_cast<QIcon>(action->property("preferenceOriginalIcon"));
            if (icon.isNull())
                icon = commanderIcon(action->objectName(), menuSize);
            action->setIcon(s.value("icons/menu", true).toBool() ? QIcon(icon.pixmap(menuSize, menuSize))
                                                                 : QIcon());
        }
    }
    QFileIconProvider icons;
    for (auto *tabs : {left_, right_}) {
        const int limit = qBound(3, s.value("tabs/titleLength", 20).toInt(), 100);
        int maxWidth = 40;
        for (int i = 0; i < tabs->count(); ++i) {
            auto *panel = qobject_cast<FilePanel *>(tabs->widget(i));
            if (!panel)
                continue;
            if (panels)
                panel->applyPreferences();
            auto title = displayPath(panel->path().filename());
            if (title.isEmpty())
                title = displayPath(panel->path());
            if (title.isEmpty())
                title = "Loading…";
            if (s.value("tabs/limit").toBool())
                title = title.left(limit);
            tabs->setTabText(i, title);
            const auto modeIcon = qvariant_cast<QIcon>(panel->property("viewModeTabIcon"));
            tabs->setTabIcon(i, s.value("tabs/icons", true).toBool()
                                    ? (modeIcon.isNull() ? icons.icon(QFileIconProvider::Folder) : modeIcon)
                                    : QIcon());
            maxWidth = qMax(maxWidth, QFontMetrics(tabs->font()).horizontalAdvance(title) + 32);
        }
        tabs->setStyleSheet(s.value("tabs/sameWidth").toBool()
                                ? QString("QTabBar::tab { min-width: %1px; max-width: %1px; }").arg(maxWidth)
                                : QString());
        tabs->setTabBarAutoHide(!s.value("classic/alwaysShowTabs").toBool());
    }
}
