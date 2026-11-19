#include "filesystem/pathencoding.h"
#include "settings/interfacepreferences.h"
#include "ui/panels/directorymodel.h"
#include "filesystem/nativefs.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <fcntl.h>
#include <mutex>
#include <sys/file.h>
#include <tuple>

bool InterfacePreferences::matches(const std::filesystem::path &path, const char *setting) {
    const auto patterns = QSettings().value(setting).toString().split(';', Qt::SkipEmptyParts);
    for (const auto &pattern : patterns)
        if (QDir::match(pattern.trimmed(), displayPath(path)))
            return true;
    return false;
}
void InterfacePreferences::configure(QDialog *dialog) {
    const auto draft = [dialog](const QString &key, const QVariant &value) {
        auto map = dialog->property("appearanceDraft").toMap();
        map[key] = value;
        dialog->setProperty("appearanceDraft", map);
    };
    const auto check = [dialog, draft](const char *id, const QString &key, bool fallback = false,
                                       const QString &tip = QString()) {
        auto *w = dialog->findChild<QCheckBox *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip(tip);
        w->setChecked(QSettings().value(key, fallback).toBool());
        QObject::connect(w, &QCheckBox::toggled, dialog, [draft, key](bool on) { draft(key, on); });
    };
    const auto edit = [dialog, draft](const char *id, const QString &key, const QString &fallback = QString(),
                                      int min = -1, int max = 0) {
        auto *w = dialog->findChild<QLineEdit *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip({});
        w->setText(QSettings().value(key, fallback).toString());
        if (min >= 0) {
            w->setValidator(new QIntValidator(min, max, w));
            w->setToolTip(QString("Range %1–%2").arg(min).arg(max));
        }
        QObject::connect(w, &QLineEdit::textChanged, dialog, [w, key, draft, min](const QString &text) {
            if (min < 0)
                draft(key, text);
            else if (w->hasAcceptableInput())
                draft(key, text.toInt());
        });
    };
    const auto combo = [dialog, draft](const char *id, const QString &key, const QStringList &items,
                                       int fallback = 0) {
        auto *w = dialog->findChild<QComboBox *>(id);
        if (!w)
            return;
        w->setEnabled(true);
        w->setToolTip({});
        w->setEditable(false);
        w->clear();
        w->addItems(items);
        w->setCurrentIndex(qBound(0, QSettings().value(key, fallback).toInt(), int(items.size() - 1)));
        QObject::connect(w, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                         [key, draft](int n) { draft(key, n); });
    };
    const auto radios = [dialog, draft](const QString &key, const QStringList &ids, int fallback) {
        auto *group = new QButtonGroup(dialog);
        for (int i = 0; i < ids.size(); ++i) {
            auto *r = dialog->findChild<QRadioButton *>(ids[i]);
            r->setAutoExclusive(false);
            r->setEnabled(true);
            r->setToolTip({});
            group->addButton(r, i);
            r->setChecked(QSettings().value(key, fallback).toInt() == i);
        }
        QObject::connect(group, &QButtonGroup::idClicked, dialog, [key, draft](int i) { draft(key, i); });
    };
    for (const auto &p : {std::pair{"LogCpMvShChk", "transfer"},
                          {"LogDeleteChk", "delete"},
                          {"LogFoldersChk", "folders"},
                          {"LogArchiveChk", "archive"},
                          {"LogFsChk", "filesystem"},
                          {"LogSuccess", "success"},
                          {"LogErrors", "error"},
                          {"LogSkipped", "skipped"}})
        check(p.first, QString("logging/") + p.second, true);
    radios("logging/rotation", {"IdLogNever", "IdLogEveryDay", "IdLogWhenLarger"}, 0);
    edit("IdLogSizeLimit", "logging/sizeKiB", "1024", 1, 1048576);
    check("IdKeepMaximum", "logging/retention");
    edit("IdLogFileCount", "logging/keep", "5", 1, 100);
    edit("FTP_Retry", "ftp/retries", "0", 0, 5);
    edit("FTP_retry_delay", "ftp/retryDelay", "1", 0, 60);
    combo("FtpTransferModeBox", "ftp/mode", {"Binary", "ASCII text", "Automatic by extension"});
    combo("FtpOpenInBox", "ftp/downloadTarget",
          {"Ask for a new filename", "Current local folder (no overwrite)"});
    check("FtpBackGnd", "ftp/background", true);
    check("FtpPassiveMode", "ftp/passive", true);
    check("FtpPORTlimitrange", "ftp/portRange");
    edit("FtpPortFrom", "ftp/portFrom", "50000", 1024, 65535);
    edit("FtpPortTo", "ftp/portTo", "50100", 1024, 65535);
    check("FtpAutoResumeOption", "ftp/stallRetry", false,
          "Retry stalled downloads safely in their private staging file. Uploads are not resumed.");
    edit("FtpAutoResumeValue", "ftp/stallSeconds", "30", 1, 300);
    check("FtpPortIPbox", "ftp/externalIP");
    edit("FtpPortIPedit", "ftp/portAddress", "-");
    check("ThumbsCustomFieldsChk", "thumbnail/showFields", true);
    check("ThumbsFromInternalChk", "thumbnail/images", true);
    edit("ThumbsFromInternalEdt", "thumbnail/imageMasks",
         "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.tif;*.tiff;*.ico");
    check("ThumbsFromTextChk", "thumbnail/text");
    edit("ThumbsFromTextEdt", "thumbnail/textMasks", "*.txt;*.md;*.log;*.csv;*.json");
    check("idFlatDriveBar", "layout/flatDrives", true);
    check("idFlatInterface", "layout/flatInterface", true);
    check("idShowBreadcrumb", "layout/breadcrumbs");
    check("idAutoOpenBreadcrumb", "layout/breadcrumbHover");
    radios("mouse/selection", {"IdConfrightbutton", "IdConfleftbutton"}, 1);
    check("IdClickOnIcon", "mouse/iconSelect");
    check("IdOnlyOnTouchscreen", "mouse/touchOnly");
    check("IdRubberband", "mouse/rubberband", true);
    check("Id_ConfSingleClickStart", "mouse/singleClick");
    check("Id_ConfselOnlyName", "rename/nameOnly");
    check("id_ConfAutoComplete", "completion/enabled");
    check("id_ConfAutoSuggest", "completion/inline");
    check("Id_ConfAlwaysToRoot", "navigation/volumeRoot", true);
    check("SND_onlylong", "sound/onlyLong");
    edit("SND_Delay", "sound/minSeconds", "10", 0, 3600);
    combo("id_DarkModeCombo", "appearance/theme", {"Light", "Dark"});
    check("id_CursorThemed", "appearance/systemCursor", false, "Use the active Qt style's cursor highlight.");
    auto *role = dialog->findChild<QComboBox *>("id_OtherName"),
         *color = dialog->findChild<QComboBox *>("id_OtherColor");
    role->setEnabled(true);
    color->setEnabled(true);
    role->setToolTip({});
    color->setToolTip({});
    role->clear();
    role->addItems({"Window", "WindowText", "Button", "ButtonText", "Mid", "Light", "Dark", "Link",
                    "ToolTipBase", "ToolTipText"});
    color->setEditable(true);
    color->clear();
    color->addItems({"#ffffff", "#000000", "#000080", "#e5e5eb"});
    const auto loadColor = [dialog, role, color] {
        QSignalBlocker block(color);
        const auto key = "appearance/roles/" + role->currentText();
        const QPalette::ColorRole roles[]{QPalette::Window,     QPalette::WindowText, QPalette::Button,
                                          QPalette::ButtonText, QPalette::Mid,        QPalette::Light,
                                          QPalette::Dark,       QPalette::Link,       QPalette::ToolTipBase,
                                          QPalette::ToolTipText};
        const auto fallback = qApp->palette().color(roles[qBound(0, role->currentIndex(), 9)]).name();
        color->setCurrentText(dialog->property("appearanceDraft")
                                  .toMap()
                                  .value(key, QSettings().value(key, fallback))
                                  .toString());
    };
    QObject::connect(role, &QComboBox::currentTextChanged, dialog, [loadColor] { loadColor(); });
    QObject::connect(color, &QComboBox::currentTextChanged, dialog, [draft, role](const QString &text) {
        if (QColor(text).isValid())
            draft("appearance/roles/" + role->currentText(), text);
    });
    loadColor();
    check("DpiCheck", "appearance/manualDpi");
    check("AutoSizeCheck", "appearance/scaleDialogs", true);
    auto *dpi = dialog->findChild<QComboBox *>("DpiEdit");
    dpi->setEnabled(true);
    dpi->setToolTip("Font DPI, 72–240. Geometry scaling is controlled separately.");
    dpi->setEditable(true);
    dpi->clear();
    dpi->addItems({"96", "120", "144", "192", "240"});
    dpi->lineEdit()->setValidator(new QIntValidator(72, 240, dpi));
    dpi->setCurrentText(QSettings().value("appearance/dpi", 96).toString());
    QObject::connect(dpi, &QComboBox::currentTextChanged, dialog, [draft, dpi](const QString &text) {
        if (dpi->lineEdit()->hasAcceptableInput())
            draft("appearance/dpi", text.toInt());
    });
    for (const auto &p : {std::pair{"NoRereadEdit", "refresh/exclude"},
                          {"NoReread2Edit", "session/exclude"},
                          {"WatchDirsExcludeEdit", "refresh/watchExclude"}}) {
        edit(p.first, p.second);
        dialog->findChild<QLineEdit *>(p.first)->setToolTip(
            "Semicolon-separated Linux directory path globs, e.g. /mnt/*;/media/*");
    }
    check("IdShowSpecialFoldersLocal", "icons/specialFolders", false,
          "Allow the Qt desktop provider's special directory icons.");
    check("IdShowDefIconsFromExplorer", "icons/nativeDefaults", false,
          "Use the Linux desktop's generic file/folder icons.");
    check("IdHidSysExclamation", "icons/hiddenBadge");
    check("IdPressedButtons", "icons/pressedChecks");
    combo("idDriveExtraX", "icons/driveX", {"0", "2", "4", "8", "16"});
    combo("idDriveExtraY", "icons/driveY", {"0", "2", "4", "8", "16"});
    check("ChkTabsConfirmClose", "tabs/confirmClose", false,
          "Confirm Ctrl+Shift+W: close all other tabs while retaining the current tab.");
    check("ChkTabsDriveInTitle", "tabs/volumeTitle", false, "Prefix tab titles with the Linux mount root.");
    check("ChkTabsSaveFiltersSeparately", "tabs/separateFilters", true);
    radios("archive/zipLevelChoice",
           {"internalzip_default", "internalzip_max", "internalzip_min", "internalzip_other"}, 0);
    edit("internalzip_value", "archive/zipLevel", "6", 0, 9);
    check("zip_setdate", "archive/newestDate");
    // Direct view-mode fields edit the same staged records as the existing picker buttons.
    auto *modes = dialog->findChild<QListWidget *>("ViewModeList");
    const QMap<QString, QString> fields{{"AdditionalSortEdit", "secondary"},
                                        {"AutoRunEdit", "command"},
                                        {"id_TabColorBg", "background"},
                                        {"id_TabColorBg2", "alternate"},
                                        {"id_TabColor", "tabColor"}};
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto *w = dialog->findChild<QWidget *>(it.key());
        w->setEnabled(true);
        w->setToolTip({});
        const auto read = [w] {
            if (auto *c = qobject_cast<QComboBox *>(w))
                return c->currentText();
            return qobject_cast<QLineEdit *>(w)->text();
        };
        if (auto *c = qobject_cast<QComboBox *>(w)) {
            c->setEditable(true);
            c->clear();
            c->addItems({"", "#ffffff", "#000080", "#eeeeee"});
        }
        const auto store = [dialog, modes, key = it.value(), read] {
            if (!modes->currentItem())
                return;
            auto all = dialog->property("viewModesDraft").toMap();
            auto record = all.value(modes->currentItem()->text()).toMap();
            const auto value = read();
            if ((key == "background" || key == "alternate" || key == "tabColor") && !value.isEmpty() &&
                !QColor(value).isValid())
                return;
            record[key] = value;
            all[modes->currentItem()->text()] = record;
            dialog->setProperty("viewModesDraft", all);
        };
        if (auto *c = qobject_cast<QComboBox *>(w))
            QObject::connect(c, &QComboBox::editTextChanged, dialog, [store] { store(); });
        else
            QObject::connect(qobject_cast<QLineEdit *>(w), &QLineEdit::textEdited, dialog,
                             [store] { store(); });
        QObject::connect(
            modes, &QListWidget::currentRowChanged, dialog, [dialog, modes, w, key = it.value()] {
                QSignalBlocker block(w);
                const auto value = modes->currentItem() ? dialog->property("viewModesDraft")
                                                              .toMap()
                                                              .value(modes->currentItem()->text())
                                                              .toMap()
                                                              .value(key)
                                                              .toString()
                                                        : QString();
                if (auto *c = qobject_cast<QComboBox *>(w))
                    c->setCurrentText(value);
                else
                    qobject_cast<QLineEdit *>(w)->setText(value);
            });
    }
    auto *choose = dialog->findChild<QComboBox *>("ModeChooseCombo");
    auto *rules = dialog->findChild<QListWidget *>("ModeChangeList");
    choose->setEnabled(true);
    choose->setToolTip("Assign a saved mode to the selected rule; Apply/OK saves the assignment.");
    choose->clear();
    choose->addItems(dialog->property("viewModesDraft").toMap().keys());
    const auto refreshChoose = [dialog, choose] {
        QSignalBlocker block(choose);
        choose->clear();
        choose->addItems(dialog->property("viewModesDraft").toMap().keys());
    };
    QObject::connect(modes->model(), &QAbstractItemModel::rowsInserted, dialog, refreshChoose);
    QObject::connect(modes->model(), &QAbstractItemModel::rowsRemoved, dialog, refreshChoose);
    QObject::connect(rules, &QListWidget::currentRowChanged, dialog,
                     [choose](int) { choose->setCurrentIndex(-1); });
    QObject::connect(choose, qOverload<int>(&QComboBox::activated), dialog, [dialog, draft, rules, choose] {
        auto all = dialog->property("appearanceDraft")
                       .toMap()
                       .value("autoViewRules", QSettings().value("autoViewRules"))
                       .toList();
        const int row = rules->currentRow();
        if (row < 0 || row >= all.size())
            return;
        auto record = all[row].toMap();
        record["mode"] = choose->currentText();
        all[row] = record;
        draft("autoViewRules", all);
        rules->item(row)->setText(record.value("path").toString() + " → " + choose->currentText());
    });
}
void InterfacePreferences::adaptDialog(QDialog *dialog) {
    QSettings s;
    if (!s.value("appearance/manualDpi").toBool() || !s.value("appearance/scaleDialogs", true).toBool())
        return;
    const double factor = qBound(72, s.value("appearance/dpi", 96).toInt(), 240) / 96.0;
    if (qFuzzyCompare(factor, 1.0))
        return;
    const auto children = dialog->findChildren<QWidget *>();
    for (auto *w : children) {
        const auto r = w->geometry();
        w->setGeometry(qRound(r.x() * factor), qRound(r.y() * factor), qRound(r.width() * factor),
                       qRound(r.height() * factor));
    }
    dialog->setFixedSize(qRound(dialog->width() * factor), qRound(dialog->height() * factor));
}
