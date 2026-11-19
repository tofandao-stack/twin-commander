#include "gapsettings.h"
#include "directorymodel.h"
#include "nativefs.h"
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

bool GapSettings::matches(const std::filesystem::path &path, const char *setting) {
    const auto patterns = QSettings().value(setting).toString().split(';', Qt::SkipEmptyParts);
    for (const auto &pattern : patterns)
        if (QDir::match(pattern.trimmed(), displayPath(path)))
            return true;
    return false;
}
void GapSettings::configure(QDialog *dialog) {
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
void GapSettings::adaptDialog(QDialog *dialog) {
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
QStringList GapSettings::curlOptions(const QUrl &url, bool transfer, bool upload) {
    QSettings s;
    QStringList args;
    if (url.scheme() == "ftp" || url.scheme() == "ftps") {
        if (s.value("ftp/passive", true).toBool())
            args << "--ftp-pasv";
        else {
            QString address = s.value("ftp/externalIP").toBool() ? s.value("ftp/portAddress", "-").toString()
                                                                 : QString("-");
            if (address.isEmpty() || address.contains('\n') || address.contains('\r') ||
                address.contains(QChar::Null))
                throw NativeFs::Error("Invalid active FTP address");
            if (s.value("ftp/portRange").toBool()) {
                const int from = s.value("ftp/portFrom", 50000).toInt(),
                          to = s.value("ftp/portTo", 50100).toInt();
                if (from < 1024 || to > 65535 || from > to)
                    throw NativeFs::Error("Invalid active FTP port range");
                address += QString(":%1-%2").arg(from).arg(to);
            }
            args << "--ftp-port" << address;
        }
        if (transfer) {
            const int mode = s.value("ftp/mode", 0).toInt();
            if (mode == 1 || (mode == 2 && QDir::match({"*.txt", "*.csv", "*.html", "*.xml", "*.md"},
                                                       QFileInfo(url.path()).fileName())))
                args << "--use-ascii";
        }
    }
    // Retries are safe for listings and staged downloads. Never retry a mutating upload automatically.
    if (!upload) {
        args << "--retry"
             << QString::number(qBound(transfer && s.value("ftp/stallRetry").toBool() ? 1 : 0,
                                       s.value("ftp/retries", 0).toInt(), 5))
             << "--retry-delay" << QString::number(qBound(0, s.value("ftp/retryDelay", 1).toInt(), 60))
             << "--retry-max-time" << "240";
        if (transfer && s.value("ftp/stallRetry").toBool())
            args << "--speed-limit" << "1" << "--speed-time"
                 << QString::number(qBound(1, s.value("ftp/stallSeconds", 30).toInt(), 300))
                 << "--retry-all-errors";
    }
    return args;
}
void GapSettings::launch(const QString &program, const QString &parameters, const QString &directory) {
    const auto executable =
        QFileInfo(program).isAbsolute() ? program : QStandardPaths::findExecutable(program);
    if (executable.isEmpty() || !QFileInfo(executable).isExecutable())
        throw NativeFs::Error("External button program is missing or not executable");
    if (!directory.isEmpty() && !QFileInfo(directory).isDir())
        throw NativeFs::Error("Start directory does not exist");
    if (!QProcess::startDetached(executable, QProcess::splitCommand(parameters), directory))
        throw NativeFs::Error("External button program could not start");
}
void GapSettings::log(const QString &category, const QString &status, const QString &message) {
    QSettings s;
    if (!s.value("logging/enabled").toBool() || !s.value("logging/" + category, true).toBool() ||
        !s.value("logging/" + status, true).toBool())
        return;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    try {
        using namespace NativeFs;
        const auto path = nativePath(s.value("logging/file").toString());
        if (path.empty() || path.filename().empty())
            return;
        Fd parent(::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        const auto leaf = path.filename().native();
        Fd file(::openat(parent.value, leaf.c_str(),
                         O_RDWR | O_APPEND | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600));
        const auto st = inspectFd(file.value);
        if (!S_ISREG(st.st_mode))
            return;
        if (::flock(file.value, LOCK_EX | LOCK_NB) < 0)
            return;
        const int rotation = s.value("logging/rotation", 0).toInt();
        const auto today = QDateTime::currentDateTimeUtc().date();
        const bool rotate =
            st.st_size > 0 &&
            ((rotation == 1 && QDateTime::fromSecsSinceEpoch(st.st_mtim.tv_sec, Qt::UTC).date() != today) ||
             (rotation == 2 &&
              st.st_size >= qBound(1, s.value("logging/sizeKiB", 1024).toInt(), 1048576) * 1024LL));
        const auto prefix = leaf + ".tc-log.";
        if (rotate) {
            const auto name = prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            renameExclusive(parent.value, leaf, name); // Reopen the new active log without recursion.
            const int next = ::openat(parent.value, leaf.c_str(),
                                      O_RDWR | O_APPEND | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            check(next);
            ::close(file.value);
            file.value = next;
        }
        if (inspectFd(file.value).st_size == 0) {
            const QByteArray header = "# TwinCommander log v1\n";
            check(::write(file.value, header.data(), header.size()) < 0 ? -1 : 0);
        }
        const auto line =
            QJsonDocument(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                      {"category", category},
                                      {"status", status},
                                      {"message", message.left(4096)}})
                .toJson(QJsonDocument::Compact) +
            '\n';
        const auto written = ::write(file.value, line.data(), line.size());
        if (written != line.size())
            return;
        if (s.value("logging/retention").toBool()) {
            std::vector<std::pair<qint64, std::filesystem::path>> owned;
            size_t checked = 0;
            for (const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
                if (++checked > 10000)
                    break;
                const auto name = entry.path().filename().native();
                if (name.rfind(prefix, 0) != 0)
                    continue;
                const int fd =
                    ::openat(parent.value, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0)
                    continue;
                Fd candidate(fd);
                const auto meta = inspectFd(fd);
                const QByteArray marker = "# TwinCommander log v1\n";
                char header[64]{};
                if (!S_ISREG(meta.st_mode) || ::read(fd, header, marker.size()) != marker.size() ||
                    QByteArray(header, marker.size()) != marker)
                    continue;
                owned.emplace_back(meta.st_mtim.tv_sec * 1000000000LL + meta.st_mtim.tv_nsec, entry.path());
            }
            std::sort(owned.rbegin(), owned.rend());
            const int keep = qBound(1, s.value("logging/keep", 5).toInt(), 100) - 1;
            for (size_t i = keep; i < owned.size(); ++i)
                ::unlinkat(parent.value, owned[i].second.filename().c_str(), 0);
        }
    } catch (const std::exception &) { /* A log error must not change a completed file operation's result. */
    }
}
