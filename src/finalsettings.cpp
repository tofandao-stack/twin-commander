#include "finalsettings.h"
#include "backgroundpool.h"
#include "classicui.h"
#include "filepanel.h"
#include "nativesettings.h"
#include "referenceforms.h"
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
            FinalSettings::translated(object->property(qPrintable("source/" + field)).toString());
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
class LanguageFilter : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show && qobject_cast<QWidget *>(object))
            translateWidget(object);
        return false;
    }
};
class WindowPreferences : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = qobject_cast<QMainWindow *>(object);
        if (!window || !window->property("nativeCommanderWindow").toBool())
            return false;
        if (event->type() == QEvent::WindowStateChange && window->isMinimized() &&
            FinalSettings::flag("Id_ConfMoveToTray")) {
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
    if (!FinalSettings::flag("id_checkforupdates") || address.isEmpty() ||
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
struct Instance : QObject {
    using QObject::QObject;
    std::unique_ptr<QLockFile> lock;
};
QByteArray recode(const QByteArray &data, bool encode) {
    iconv_t c = iconv_open(encode ? "CP437" : "UTF-8", encode ? "UTF-8" : "CP437");
    if (c == iconv_t(-1))
        throw std::runtime_error("CP437 converter unavailable");
    QByteArray out(data.size() * 4 + 16, 0);
    char *input = const_cast<char *>(data.constData()), *output = out.data();
    size_t left = data.size(), space = out.size();
    const auto result = iconv(c, &input, &left, &output, &space);
    iconv_close(c);
    if (result == size_t(-1))
        throw std::runtime_error("Comment text cannot be represented in the selected encoding");
    out.resize(out.size() - space);
    return out;
}
QByteArray small(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return {};
    NativeFs::Fd input(fd);
    const auto st = NativeFs::inspectFd(fd);
    if (!S_ISREG(st.st_mode) || st.st_size > 2 * 1024 * 1024)
        return {};
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return {};
    return file.read(2 * 1024 * 1024);
}
QMap<QString, QString> legacy(const std::filesystem::path &dir, const QString &file) {
    auto data = small(dir / nativePath(file));
    if (FinalSettings::flag("IdConf_comments_doscharset"))
        data = recode(data, false);
    QMap<QString, QString> values;
    for (const auto &line : QString::fromUtf8(data).split('\n')) {
        if (values.size() >= 10000)
            break;
        QString name, value;
        if (line.startsWith('"')) {
            const auto end = line.indexOf('"', 1);
            if (end < 0)
                continue;
            name = line.mid(1, end - 1);
            value = line.mid(end + 1).trimmed();
        } else {
            const auto end = line.indexOf(QRegularExpression("\\s"));
            if (end < 0)
                continue;
            name = line.left(end);
            value = line.mid(end).trimmed();
        }
        if (!name.isEmpty())
            values[name] = value;
    }
    return values;
}
QString xattr(const std::filesystem::path &path, const char *name) {
    const auto size = ::lgetxattr(path.c_str(), name, nullptr, 0);
    if (size < 0 || size > 65536)
        return {};
    QByteArray value(size, 0);
    if (::lgetxattr(path.c_str(), name, value.data(), value.size()) < 0)
        return {};
    return QString::fromUtf8(value);
}
} // namespace
bool FinalSettings::flag(const char *id) {
    if (QString(id) == "IdConfAllAndEXE")
        return QSettings().value("icons/mode", 1).toInt() == 3;
    for (auto it = radioGroups.begin(); it != radioGroups.end(); ++it) {
        const int n = it.value().indexOf(QString::fromLatin1(id));
        if (n >= 0)
            return QSettings().value("final/" + it.key(), 0).toInt() == n;
    }
    return QSettings().value(key(id), defaults.contains(QString::fromLatin1(id))).toBool();
}
QString FinalSettings::text(const char *id, const QString &fallback) {
    return QSettings().value(key(id), fallback).toString();
}
int FinalSettings::number(const char *id, int fallback) {
    return QSettings().value(key(id), fallback).toInt();
}
void FinalSettings::configure(QDialog *dialog) {
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
                     [dialog] { NativeSettings::configurePlugins(dialog, "thumbnail"); });
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
QString FinalSettings::translated(const QString &s) {
    return qApp->property("nativeLanguage").toMap().value(s, s).toString();
}
void FinalSettings::apply() {
    try {
        qApp->setProperty("nativeLanguage", readLanguage(QSettings().value("final/language").toString()));
        if (!qApp->property("languageFilter").toBool()) {
            qApp->installEventFilter(new LanguageFilter(qApp));
            qApp->installEventFilter(new WindowPreferences(qApp));
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
bool FinalSettings::instanceLock() {
    auto *holder =
        static_cast<Instance *>(qApp->findChild<QObject *>("nativeInstanceLock", Qt::FindDirectChildrenOnly));
    if (!holder) {
        holder = new Instance(qApp);
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
QString FinalSettings::alias83(const QString &name, int serial) {
    const auto dot = name.lastIndexOf('.');
    auto stem = dot > 0 ? name.left(dot) : name, extension = dot > 0 ? name.mid(dot + 1) : QString();
    const auto clean = [](QString s) {
        s = s.toUpper();
        s.remove(QRegularExpression("[^A-Z0-9_$~!#%&{}()@'`-]"));
        return s;
    };
    stem = clean(stem);
    extension = clean(extension).left(3);
    if (stem.isEmpty())
        stem = "FILE";
    if (serial > 1 || stem.size() > 8 || name.contains(' ') || name.toUtf8().size() != name.size()) {
        const auto suffix = "~" + QString::number(serial);
        stem = stem.left(qMax(1, 8 - suffix.size())) + suffix;
    }
    return stem.left(8) + (extension.isEmpty() ? QString() : "." + extension);
}
QString FinalSettings::comment(const std::filesystem::path &path) {
    const auto format = text("IdPreferredCommentFormat", "Native xattr");
    const auto native = xattr(path, "user.twincommander.comment");
    QString value = format == "Native xattr"
                        ? native
                        : legacy(path.parent_path(), format).value(displayPath(path.filename()));
    if ((flag("IdConf_DescriptComments") || format != "Native xattr") && value.isEmpty()) {
        value = legacy(path.parent_path(), format == "files.bbs" ? "files.bbs" : "descript.ion")
                    .value(displayPath(path.filename()));
        if (value.isEmpty() && flag("IdConf_comments_bothformats")) {
            value = legacy(path.parent_path(), format == "files.bbs" ? "descript.ion" : "files.bbs")
                        .value(displayPath(path.filename()));
            if (value.isEmpty())
                value = native;
        }
    }
    if (value.isEmpty() && flag("IdConf_ExplorerComments")) {
        value = xattr(path, "user.xdg.comment");
        if (value.isEmpty())
            value = xattr(path, "user.comment");
    }
    return value;
}
void FinalSettings::setComment(const std::filesystem::path &path, const QString &value) {
    if (value.toUtf8().size() > 65536)
        throw std::runtime_error("Comment limit is 64 KiB");
    const auto format = text("IdPreferredCommentFormat", "Native xattr");
    if (format == "Native xattr") {
        const auto data = value.toUtf8();
        NativeFs::check(::lsetxattr(path.c_str(), "user.twincommander.comment", data.data(), data.size(), 0));
        return;
    }
    const auto name = displayPath(path.filename());
    if (nativePath(name) != path.filename() || name.contains('"') || name.contains('\n') ||
        value.contains('\n') || value.contains('\r'))
        throw std::runtime_error("Legacy comments require single-line representable names and comments");
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    const auto sidecar = path.parent_path() / nativePath(format);
    struct stat st {};
    if (::lstat(sidecar.c_str(), &st) == 0 && (!S_ISREG(st.st_mode) || st.st_size > 2 * 1024 * 1024))
        throw std::runtime_error("Legacy comment sidecar is not a bounded regular file");
    auto raw = small(sidecar);
    if (st.st_size > 0 && raw.isEmpty())
        throw std::runtime_error("Legacy comment sidecar could not be read");
    if (flag("IdConf_comments_doscharset"))
        raw = recode(raw, false);
    const auto original = QString::fromUtf8(raw);
    if (original.toUtf8() != raw)
        throw std::runtime_error("Legacy sidecar contains invalid UTF-8");
    QStringList lines;
    const QString quoted = '"' + name + '"';
    for (const auto &line : original.split('\n')) {
        const bool quotedMatch =
            line.startsWith(quoted) && (line.size() == quoted.size() || line[quoted.size()].isSpace());
        const bool plainMatch = !name.contains(QRegularExpression("\\s")) && line.startsWith(name) &&
                                line.size() > name.size() && line[name.size()].isSpace();
        if (quotedMatch || plainMatch)
            continue;
        if (!line.isEmpty())
            lines << line;
    }
    if (!value.isEmpty())
        lines << quoted + " " + value;
    auto data = (lines.join('\n') + (lines.isEmpty() ? QString() : "\n")).toUtf8();
    if (flag("IdConf_comments_doscharset"))
        data = recode(data, true);
    if (data.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Legacy sidecar limit is 2 MiB");
    NativeFs::Fd parent(::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    const auto leaf = nativePath(format).native();
    const auto temporary = ".tc-comment-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    NativeFs::Fd out(::openat(parent.value, temporary.c_str(),
                              O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    try {
        qsizetype offset = 0;
        while (offset < data.size()) {
            const auto n = ::write(out.value, data.data() + offset, data.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            NativeFs::check(n <= 0 ? -1 : 0);
            offset += n;
        }
        NativeFs::check(::fsync(out.value));
        struct stat current {};
        const bool exists = ::fstatat(parent.value, leaf.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0;
        if (st.st_ino) {
            if (!exists || !NativeFs::same(st, current))
                throw std::runtime_error("Legacy sidecar changed during editing");
            NativeFs::check(::renameat(parent.value, temporary.c_str(), parent.value, leaf.c_str()));
        } else
            NativeFs::renameExclusive(parent.value, temporary, leaf);
    } catch (...) {
        ::unlinkat(parent.value, temporary.c_str(), 0);
        throw;
    }
}
QByteArray FinalSettings::dragType() { return "application/x-twincommander-native-paths"; }
QMimeData *FinalSettings::dragData(const std::vector<std::filesystem::path> &paths) {
    auto *mime = new QMimeData;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << quint32(paths.size());
    for (const auto &path : paths)
        stream << QByteArray::fromStdString(path.native());
    mime->setData(dragType(), data);
    return mime;
}
std::vector<std::filesystem::path> FinalSettings::dropPaths(const QMimeData *data) {
    std::vector<std::filesystem::path> paths;
    const auto bytes = data->data(dragType());
    if (bytes.size() > 4 * 1024 * 1024)
        return {};
    QDataStream stream(bytes);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 10000)
        return {};
    for (quint32 i = 0; i < count; ++i) {
        QByteArray path;
        stream >> path;
        if (stream.status() != QDataStream::Ok || path.contains('\0') || !path.startsWith('/'))
            return {};
        paths.emplace_back(path.toStdString());
    }
    return paths;
}
void FinalSettings::tabMenu(QTabWidget *tabs, int index, const QPoint &point) {
    if (index < 0 || index >= tabs->count())
        return;
    auto *panel = tabs->widget(index);
    auto *menu = new QMenu(tabs);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *lock = menu->addAction(
        panel->property("lockedRoot").toByteArray().isEmpty() ? "Lock this tab's root" : "Unlock this tab");
    QObject::connect(lock, &QAction::triggered, panel, [panel, tabs] {
        auto *p = qobject_cast<FilePanel *>(panel);
        panel->setProperty("lockedRoot", panel->property("lockedRoot").toByteArray().isEmpty()
                                             ? QByteArray::fromStdString(p->path().native())
                                             : QByteArray());
        const auto title = displayPath(p->path().filename());
        tabs->setTabText(
            tabs->indexOf(panel),
            title + (flag("ChkTabsLockedAsterisk") && !panel->property("lockedRoot").toByteArray().isEmpty()
                         ? "*"
                         : ""));
    });
    menu->popup(point);
}
FileTools::Statistics FinalSettings::sizes(const std::vector<std::filesystem::path> &paths,
                                           const std::shared_ptr<LocalFiles::Control> &control) {
    static std::mutex mutex;
    struct Entry {
        FileTools::Statistics stats;
        qint64 time;
    };
    static std::map<std::filesystem::path, Entry> cache;
    if (paths.size() == 1 &&
        (flag("IdConf_EverythingForSize") || text("IdConf_GetDirSize") == "Recent size index")) {
        std::lock_guard lock(mutex);
        auto it = cache.find(paths[0]);
        if (it != cache.end() && QDateTime::currentMSecsSinceEpoch() - it->second.time < 1000)
            return it->second.stats;
    }
    FileTools::Statistics result;
    if (text("IdConf_GetDirSize") == "Local filesystems only")
        for (const auto &p : paths) {
            const auto type = QStorageInfo(displayPath(p)).fileSystemType();
            if (type.contains("nfs") || type.contains("cifs") || type.contains("sshfs")) {
                result.errors << "Network directory size scan disabled by preferences";
                return result;
            }
        }
    result = FileTools::statistics(paths, control);
    if (paths.size() == 1 && !result.cancelled && !result.truncated && result.errors.isEmpty()) {
        std::lock_guard lock(mutex);
        if (cache.size() > 1024)
            cache.clear();
        cache[paths[0]] = {result, QDateTime::currentMSecsSinceEpoch()};
    }
    return result;
}
bool FinalSettings::countBeforeOperation(const std::vector<std::filesystem::path> &paths, QWidget *parent,
                                         QString &summary) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Count selected items");
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel("Counting files and bytes before the operation…", &dialog);
    layout->addWidget(label);
    auto *cancel = new QPushButton("Cancel", &dialog);
    layout->addWidget(cancel);
    auto control = std::make_shared<LocalFiles::Control>();
    QFutureWatcher<FileTools::Statistics> watcher;
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        control->cancelled = true;
        cancel->setEnabled(false);
        label->setText("Cancelling…");
    });
    QObject::connect(&watcher, &QFutureWatcher<FileTools::Statistics>::finished, &dialog, &QDialog::accept);
    watcher.setFuture(QtConcurrent::run(ioPool(), sizes, paths, control));
    dialog.exec();
    if (!watcher.isFinished()) {
        control->cancelled = true;
        watcher.waitForFinished();
    }
    const auto result = watcher.result();
    summary = QString("%1 files, %2 directories, %3 bytes")
                  .arg(result.files)
                  .arg(result.directories)
                  .arg(result.bytes);
    if (!result.errors.isEmpty() || result.truncated) {
        QMessageBox::warning(parent, "Incomplete count", result.errors.join('\n'));
        return false;
    }
    return !result.cancelled;
}
