#include <QSet>
#include <QVariantMap>
#include "settings/applicationpreferences.h"
#include "operations/iothreadpool.h"
#include "ui/panels/filepanellayout.h"
#include "ui/panels/filepanel.h"
#include "platform/nativeextensions.h"
#include "ui/settings/referenceformrenderer.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QInputDialog>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>
#include <iconv.h>
#include <mutex>
#include <set>
#include <sys/xattr.h>

namespace {
const QSet<QString> defaults{
    "IdConf_longnames",     "IdConfshowSystem",    "IdConf_drivetips",      "IdConf_comments_bothformats",
    "idExecCmdLineFocused", "idTwoDriveBars",      "id_StandardCopyMethod", "ChkCopyStamp",
    "IdConf_trashcan",      "arj_useinternal",     "arj_longnames",         "lzh_useinternal",
    "rar_useinternal",      "ace_useinternal",     "tar_linuxcompatible",   "zip_internalpack",
    "zip_likedirs",         "zip_useinternal",     "z7_internalpack",       "z7_solid",
    "confirm_overwrite",    "confirm_readonly",    "confirm_hidsys",        "confirm_mouse",
    "confirm_dirs",         "confirm_mouse_delete"};
QString key(const char *id) { return "final/" + QString::fromLatin1(id); }
const QMap<QString, QStringList> radioGroups{
    {"copyMethod", {"RdDefaultCopy", "RdBigfileCopy", "RdCopyPaste"}},
    {"zipTemp", {"internalzip_settemp", "internalzip_otherdir"}},
    {"sevenLevel", {"z7_default", "z7_max", "z7_min", "z7_other"}}};
QVariantMap readLanguage(const QString &path) {
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 256 * 1024)
        throw std::runtime_error("Language file exceeds 256 KiB or cannot be read");
    const auto doc = QJsonDocument::fromJson(f.readAll()).object();
    const auto map = doc["strings"].toObject();
    if (map.size() > 2048)
        throw std::runtime_error("Language entry limit exceeded");
    return map.toVariantMap();
}
void translateWidget(QObject *object) {
    const auto change = [object](const QString &field, const QString &now, const auto &set) {
        const auto last = object->property(qPrintable("translated/" + field)).toString();
        if (now != last || !object->property(qPrintable("source/" + field)).isValid())
            object->setProperty(qPrintable("source/" + field), now);
        const auto result =
            ApplicationPreferences::translated(object->property(qPrintable("source/" + field)).toString());
        object->setProperty(qPrintable("translated/" + field), result);
        set(result);
    };
    if (auto *a = qobject_cast<QAction *>(object))
        change("text", a->text(), [a](const auto &s) { a->setText(s); });
    else if (auto *b = qobject_cast<QAbstractButton *>(object))
        change("text", b->text(), [b](const auto &s) { b->setText(s); });
    else if (auto *l = qobject_cast<QLabel *>(object))
        change("text", l->text(), [l](const auto &s) { l->setText(s); });
    else if (auto *g = qobject_cast<QGroupBox *>(object))
        change("title", g->title(), [g](const auto &s) { g->setTitle(s); });
    if (auto *w = qobject_cast<QDialog *>(object))
        change("windowTitle", w->windowTitle(), [w](const auto &s) { w->setWindowTitle(s); });
    for (auto *child : object->children())
        translateWidget(child);
}
class TranslationEventFilter : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show && qobject_cast<QWidget *>(object))
            translateWidget(object);
        return false;
    }
};
class WindowPreferenceEventFilter : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = qobject_cast<QMainWindow *>(object);
        if (!window || !window->property("nativeCommanderWindow").toBool())
            return false;
        if (event->type() == QEvent::WindowStateChange && window->isMinimized() &&
            ApplicationPreferences::flag("Id_ConfMoveToTray")) {
            if (!QSystemTrayIcon::isSystemTrayAvailable()) {
                window->statusBar()->showMessage("System tray is unavailable; the window remains minimized.",
                                                 5000);
                return false;
            }
            auto *tray = window->findChild<QSystemTrayIcon *>("commanderTray");
            if (!tray) {
                tray = new QSystemTrayIcon(commanderIcon("folder"), window);
                tray->setObjectName("commanderTray");
                auto *menu = new QMenu(window);
                const auto restore = [window, tray] {
                    window->showNormal();
                    window->activateWindow();
                    tray->hide();
                };
                QObject::connect(menu->addAction("Restore"), &QAction::triggered, window, restore);
                QObject::connect(menu->addAction("Exit"), &QAction::triggered, window, &QWidget::close);
                QObject::connect(tray, &QSystemTrayIcon::activated, window, [restore](auto reason) {
                    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                        restore();
                });
                tray->setContextMenu(menu);
            }
            tray->show();
            QTimer::singleShot(0, window, [window] {
                if (window->isMinimized())
                    window->hide();
            });
        }
        return false;
    }
};
void automaticUpdate(QMainWindow *window) {
    const auto address = QSettings().value("updates/url").toString();
    if (!ApplicationPreferences::flag("id_checkforupdates") || address.isEmpty() ||
        window->property("checkedReleaseFeed").toString() == address)
        return;
    const QUrl url(address);
    if (url.scheme() != "https" && !(url.scheme() == "http" && url.host() == "127.0.0.1"))
        return;
    window->setProperty("checkedReleaseFeed", address);
    auto *manager = new QNetworkAccessManager(window);
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    auto *reply = manager->get(request);
    auto data = std::make_shared<QByteArray>();
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, data] {
        *data += reply->readAll();
        if (data->size() > 65536)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, window, [window, reply, manager, data] {
        *data += reply->readAll();
        const auto object = QJsonDocument::fromJson(*data).object();
        const auto version = QVersionNumber::fromString(object["version"].toString());
        if (reply->error() != QNetworkReply::NoError || data->size() > 65536 || version.isNull())
            window->statusBar()->showMessage("Automatic update check failed", 5000);
        else if (version > QVersionNumber::fromString(QCoreApplication::applicationVersion())) {
            window->setProperty("availableVersion", version.toString());
            window->setProperty("availableReleaseUrl", object["url"].toString());
            window->statusBar()->showMessage(
                "Update available: " + version.toString() + " · use Check for updates for details", 15000);
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}
struct ProfileInstanceLock : QObject {
    using QObject::QObject;
    std::unique_ptr<QLockFile> lock;
};
} // namespace
bool ApplicationPreferences::flag(const char *id) {
    if (QString(id) == "IdConfAllAndEXE")
        return QSettings().value("icons/mode", 1).toInt() == 3;
    for (auto it = radioGroups.begin(); it != radioGroups.end(); ++it) {
        const int n = it.value().indexOf(QString::fromLatin1(id));
        if (n >= 0)
            return QSettings().value("final/" + it.key(), 0).toInt() == n;
    }
    return QSettings().value(key(id), defaults.contains(QString::fromLatin1(id))).toBool();
}
QString ApplicationPreferences::text(const char *id, const QString &fallback) {
    return QSettings().value(key(id), fallback).toString();
}
int ApplicationPreferences::number(const char *id, int fallback) {
    return QSettings().value(key(id), fallback).toInt();
}
void ApplicationPreferences::configure(QDialog *dialog) {
    const auto draft = [dialog](const QString &name, const QVariant &value) {
        auto map = dialog->property("appearanceDraft").toMap();
        map[name] = value;
        dialog->setProperty("appearanceDraft", map);
    };
    QFile file(":/reference/remaining-0.8.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing final controls baseline");
    const auto rows = QJsonDocument::fromJson(file.readAll()).object()["configuration"].toArray();
    const QMap<QString, QString> initial{{"EdSmallSame", "128"}, {"EdSmallOther", "256"},
                                         {"EdBigSame", "1024"},  {"EdBigOther", "2048"},
                                         {"id_extralines", "3"}, {"z7_value", "6"}};
    const QMap<QString, QStringList> choices{
        {"IdPreferredCommentFormat", {"Native xattr", "descript.ion", "files.bbs"}},
        {"IdConf_GetDirSize", {"Fresh scan", "Recent size index", "Local filesystems only"}},
        {"ViewerHotkeyCombo", {"Internal viewer", "Configured viewer first", "Ask each time"}},
        {"internalzip_unicodemethod", {"UTF-8", "CP437", "ASCII"}},
        {"z7_method", {"LZMA2", "LZMA", "PPMd", "BZip2", "Deflate", "Copy"}},
        {"z7_dict", {"1m", "4m", "16m", "64m", "128m"}},
        {"z7_word", {"16", "32", "64", "128", "256"}},
        {"z7_solidblock", {"16m", "64m", "256m", "1g", "on"}},
        {"z7_threads", {"1", "2", "4", "8", "on"}},
        {"DirUsageLocationCombo", {"Settings", "JSON data file", "Session only"}}};
    for (const auto &v : rows) {
        const auto id = v.toObject()["id"].toString();
        auto *w = dialog->findChild<QWidget *>(id);
        if (!w)
            continue;
        w->setEnabled(true);
        w->setToolTip("Implemented native Linux setting. See the 0.9 control implementation notes for "
                      "platform mappings.");
        const auto setting = "final/" + id;
        if (auto *c = qobject_cast<QCheckBox *>(w)) {
            c->setChecked(flag(qPrintable(id)));
            QObject::connect(c, &QCheckBox::toggled, dialog,
                             [draft, setting](bool on) { draft(setting, on); });
        } else if (auto *e = qobject_cast<QLineEdit *>(w)) {
            e->setText(text(qPrintable(id), initial.value(id)));
            if (initial.contains(id)) {
                const int max = id == "z7_value" ? 9 : id == "id_extralines" ? 100 : 8192;
                e->setValidator(new QIntValidator(0, max, e));
            }
            QObject::connect(e, &QLineEdit::textChanged, dialog, [draft, setting, e](const QString &s) {
                if (!e->validator() || e->hasAcceptableInput())
                    draft(setting, s);
            });
        } else if (auto *c = qobject_cast<QComboBox *>(w)) {
            if (!choices.contains(id))
                continue;
            c->setEditable(false);
            c->clear();
            c->addItems(choices.value(id));
            QString fallback = c->itemText(0);
            if (id == "ViewerHotkeyCombo")
                fallback = c->itemText(1);
            if (id == "z7_dict")
                fallback = "16m";
            if (id == "z7_word")
                fallback = "32";
            if (id == "z7_solidblock")
                fallback = "64m";
            if (id == "z7_threads")
                fallback = "on";
            c->setCurrentText(text(qPrintable(id), fallback));
            QObject::connect(c, &QComboBox::currentTextChanged, dialog,
                             [draft, setting](const QString &s) { draft(setting, s); });
        }
    }
    for (auto it = radioGroups.begin(); it != radioGroups.end(); ++it) {
        auto *group = new QButtonGroup(dialog);
        for (int i = 0; i < it.value().size(); ++i) {
            auto *r = dialog->findChild<QRadioButton *>(it.value()[i]);
            r->setAutoExclusive(false);
            group->addButton(r, i);
            r->setChecked(QSettings().value("final/" + it.key(), 0).toInt() == i);
        }
        QObject::connect(group, &QButtonGroup::idClicked, dialog,
                         [draft, setting = "final/" + it.key()](int index) { draft(setting, index); });
    }
    auto *extra = dialog->findChild<QRadioButton *>("IdConfAllAndEXE");
    auto *base = dialog->findChild<QRadioButton *>("IdConfShowAllIcons");
    extra->setAutoExclusive(false);
    base->group()->addButton(extra, 3);
    extra->setChecked(flag("IdConfAllAndEXE"));
    auto *priority = dialog->findChild<QCheckBox *>("PriorityCheckbox");
    auto *modes = dialog->findChild<QListWidget *>("ViewModeList");
    QObject::connect(modes, &QListWidget::currentRowChanged, dialog, [dialog, priority, modes] {
        QSignalBlocker block(priority);
        priority->setChecked(modes->currentItem() && dialog->property("viewModesDraft")
                                                         .toMap()
                                                         .value(modes->currentItem()->text())
                                                         .toMap()
                                                         .value("priority")
                                                         .toBool());
    });
    QObject::connect(priority, &QCheckBox::toggled, dialog, [dialog, modes](bool on) {
        if (!modes->currentItem())
            return;
        auto map = dialog->property("viewModesDraft").toMap();
        auto mode = map.value(modes->currentItem()->text()).toMap();
        mode["priority"] = on;
        map[modes->currentItem()->text()] = mode;
        dialog->setProperty("viewModesDraft", map);
    });
    auto *scope = dialog->findChild<QComboBox *>("FileSystemCombo");
    scope->clear();
    scope->addItems({"All sets", "Local Linux", "Native providers"});
    scope->setCurrentIndex(QSettings().value("final/columnScope", 0).toInt());
    const auto filterColumns = [dialog, scope] {
        auto *list = dialog->findChild<QListWidget *>("W_CustomColumnList");
        const auto rows = QSettings().value("customColumns").toList();
        for (int i = 0; i < list->count() && i < rows.size(); ++i)
            list->item(i)->setHidden(scope->currentIndex() &&
                                     rows[i].toMap().value("scope", "Local Linux").toString() !=
                                         (scope->currentIndex() == 1 ? "Local Linux" : "Native providers"));
        if (list->currentItem() && list->currentItem()->isHidden())
            list->setCurrentRow(-1);
    };
    QObject::connect(scope, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                     [draft, filterColumns](int i) {
                         draft("final/columnScope", i);
                         filterColumns();
                     });
    QObject::connect(dialog->findChild<QListWidget *>("W_CustomColumnList")->model(),
                     &QAbstractItemModel::rowsInserted, dialog, filterColumns);
    filterColumns();
    auto *updates = dialog->findChild<QCheckBox *>("id_checkforupdates");
    QObject::connect(updates, &QCheckBox::clicked, dialog, [dialog, draft, updates](bool on) {
        if (!on || !QSettings().value("updates/url").toString().isEmpty())
            return;
        bool ok = false;
        const auto address = QInputDialog::getText(
            dialog, "Automatic updates", "HTTPS JSON feed URL (version and url fields):", QLineEdit::Normal,
            {}, &ok);
        if (!ok || QUrl(address).scheme() != "https") {
            updates->setChecked(false);
            return;
        }
        draft("updates/url", address);
    });
    auto *providers = dialog->findChild<QCheckBox *>("ThumbsFromListerPluginsChk");
    providers->setToolTip(
        "Native stdout-PNG providers. Right-click to configure; Windows DLLs are not loaded.");
    providers->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(providers, &QWidget::customContextMenuRequested, dialog,
                     [dialog] { NativeExtensions::configurePlugins(dialog, "thumbnail"); });
    auto *converter = dialog->findChild<QCheckBox *>("ThumbsFromIrfanXnChk");
    converter->setToolTip("Native ImageMagick-compatible converter. Right-click to choose an executable.");
    converter->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(converter, &QWidget::customContextMenuRequested, dialog, [dialog, draft] {
        const auto path = QFileDialog::getOpenFileName(dialog, "Native thumbnail converter");
        if (!path.isEmpty() && QFileInfo(path).isExecutable())
            draft("final/thumbnailConverter", path);
    });
    const QMap<QString, QString> mappings{
        {"idXPthemeBackground", "Native Qt gradient chrome; Windows theme APIs are not loaded."},
        {"IdConf_longnames", "Show real Linux names or display-only DOS-style aliases; files are never "
                             "renamed by this preference."},
        {"IdConfshowSystem", "Honor the SYSTEM bit of Samba user.DOSATTRIB metadata."},
        {"IdConf_Win32Tips", "Show native path and file-comment metadata in tooltips."},
        {"IdConf_ExplorerComments", "Read user.xdg.comment and user.comment attributes; OLE2 is not loaded."},
        {"IdConfAllAndEXE",
         "Use native desktop launcher and executable icons; Windows PE/LNK parsing is not provided."},
        {"NotOnFloppy", "Avoid enhanced executable/launcher icons on removable mount roots."},
        {"NotOnNet", "Avoid enhanced executable/launcher icon reads on network filesystems."},
        {"IdConf_EverythingForSize",
         "Use a native one-second directory-size index; this does not call the Windows Everything SDK."},
        {"IdConf_oneinstanceonly", "Hold this profile's process lock and reject subsequent starts; existing "
                                   "processes are not terminated."},
        {"RdCopyPaste",
         "Qt buffered I/O compatibility backend with native validation; no clipboard/Explorer automation."},
        {"ChkExplorerDelete", "Qt leaf deletion through a pinned directory descriptor; undecodable names "
                              "require the POSIX method."},
        {"FtpModeZ", "Real zlib-compressed FTP MODE Z; requires python3 and server support."},
        {"FtpModeZnotonLAN", "Skip compression for private, loopback and link-local peers."},
        {"ThumbsFromExplorerChk", "Use installed freedesktop thumbnailers, not Windows OLE2."},
        {"zip_zipnt", "Use the native 7-Zip ZIP compatibility backend, not the Windows ZIP-NT program."},
        {"z7_internalpack",
         "Use the installed native 7z/7zz engine; disabling selects the configured external executable."},
        {"zip_keepdamaged", "May publish damaged bytes with integrity warnings. Path, type and size "
                            "validation remains enabled."},
        {"zip_openpartial", "Recover complete verified ZIP entries before a truncated tail."},
        {"IdAutoReUpload", "Re-upload stable edits to tracked downloads in this FTP session. Failed uploads "
                           "are not automatically retried."},
        {"Hot_Params_Edit", "Native shortcut parameters: directory for New tab/Home, or filename mask for "
                            "custom/all file view. No shell."}};
    for (auto it = mappings.begin(); it != mappings.end(); ++it)
        if (auto *widget = dialog->findChild<QWidget *>(it.key()))
            widget->setToolTip(it.value());
    auto *languages = dialog->findChild<QListWidget *>("W_langlist");
    languages->clear();
    const auto add = [languages](const QString &name, const QString &path) {
        auto *i = new QListWidgetItem(name, languages);
        i->setData(Qt::UserRole, path);
        if (QSettings().value("final/language").toString() == path)
            languages->setCurrentItem(i);
    };
    add("English", {});
    add("Română (native sample)", ":/languages/ro.json");
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/languages";
    for (const auto &name : QDir(directory).entryList({"*.json"}, QDir::Files))
        add(name, QDir(directory).filePath(name));
    QObject::connect(languages, &QListWidget::currentRowChanged, dialog, [draft, languages](int i) {
        if (i >= 0)
            draft("final/language", languages->item(i)->data(Qt::UserRole));
    });
}
QString ApplicationPreferences::translated(const QString &s) {
    return qApp->property("nativeLanguage").toMap().value(s, s).toString();
}
void ApplicationPreferences::apply() {
    try {
        qApp->setProperty("nativeLanguage", readLanguage(QSettings().value("final/language").toString()));
        if (!qApp->property("languageFilter").toBool()) {
            qApp->installEventFilter(new TranslationEventFilter(qApp));
            qApp->installEventFilter(new WindowPreferenceEventFilter(qApp));
            qApp->setProperty("languageFilter", true);
        }
        for (auto *w : qApp->topLevelWidgets()) {
            translateWidget(w);
            if (auto *window = qobject_cast<QMainWindow *>(w)) {
                window->setStyleSheet(
                    flag("idXPthemeBackground")
                        ? "QMenuBar, QToolBar, QWidget#functionKeyBar { background: "
                          "qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f9f9ff,stop:1 #c5d2eb); }"
                        : QString());
                automaticUpdate(window);
                if (!flag("Id_ConfMoveToTray"))
                    if (auto *tray = window->findChild<QSystemTrayIcon *>("commanderTray")) {
                        const bool restore = tray->isVisible() && window->isHidden();
                        tray->hide();
                        if (restore)
                            window->showNormal();
                    }
            }
        }
        if (!instanceLock())
            QMessageBox::warning(nullptr, "Single instance", "Another instance holds this profile's lock.");
    } catch (const std::exception &e) {
        QMessageBox::warning(nullptr, "Preferences", QString::fromLocal8Bit(e.what()));
    }
}
bool ApplicationPreferences::instanceLock() {
    auto *holder =
        static_cast<ProfileInstanceLock *>(qApp->findChild<QObject *>("nativeInstanceLock", Qt::FindDirectChildrenOnly));
    if (!holder) {
        holder = new ProfileInstanceLock(qApp);
        holder->setObjectName("nativeInstanceLock");
    }
    if (!flag("IdConf_oneinstanceonly")) {
        holder->lock.reset();
        return true;
    }
    if (holder->lock)
        return true;
    const auto dir = QFileInfo(QSettings().fileName()).absolutePath();
    QDir().mkpath(dir);
    auto lock = std::make_unique<QLockFile>(dir + "/twin-commander.lock");
    if (!lock->tryLock(0))
        return false;
    holder->lock = std::move(lock);
    return true;
}
