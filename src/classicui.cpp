#include "classicui.h"
#include "filepanel.h"
#include "finalsettings.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QResizeEvent>
#include <QResource>
#include <QScrollArea>
#include <QSettings>
#include <QStorageInfo>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStyleOptionMenuItem>
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

QJsonObject commanderUiReference() {
    static const bool registered = [] {
        Q_INIT_RESOURCE(commander);
        return true;
    }();
    Q_UNUSED(registered);
    QFile file(":/reference/tc-11.58.1-ui.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing reference UI catalog");
    return QJsonDocument::fromJson(file.readAll()).object();
}
QJsonObject commanderCommandIds() {
    static const auto commands = [] {
        commanderUiReference();
        QFile file(":/reference/tc-11.58.1-command-ids.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Missing command ID inventory");
        return QJsonDocument::fromJson(file.readAll()).object().value("commands").toObject();
    }();
    return commands;
}
QIcon commanderIcon(const QString &name, int size) {
    static QHash<QString, QIcon> cache;
    const auto directory = QSettings().value("appearance/iconDirectory").toString();
    const auto key = directory + name + QString::number(size);
    if (!directory.isEmpty()) {
        const QIcon custom(QDir(directory).filePath(name + ".png"));
        if (!custom.isNull())
            return custom;
    }
    if (cache.contains(key))
        return cache.value(key);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    auto pen = [&](QColor color, qreal width = 1) {
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    };
    auto folder = [&] {
        pen(QColor("#8d650c"));
        p.setBrush(QColor("#f9cb55"));
        p.drawRoundedRect(QRectF(2, 6, 19, 14), 1, 1);
        p.drawRect(QRectF(3, 4, 8, 3));
        p.setBrush(QColor("#ffdf7c"));
        p.drawPolygon(QPolygonF{{2, 10}, {22, 10}, {19, 20}, {1, 20}});
    };
    if (name == "folder" || name.contains("MkDir", Qt::CaseInsensitive) ||
        name.contains("Pack", Qt::CaseInsensitive) || name.contains("Unpack", Qt::CaseInsensitive)) {
        folder();
        if (name.contains("Pack", Qt::CaseInsensitive) || name.contains("Unpack", Qt::CaseInsensitive)) {
            pen(QColor("#6d5830"), 2);
            p.drawLine(12, 7, 12, 20);
            pen(QColor("#e2e2e2"));
            for (int y = 9; y < 19; y += 3)
                p.drawLine(10, y, 14, y);
        }
    } else if (name == "parent" || name.contains("Previous", Qt::CaseInsensitive) ||
               name.contains("Next", Qt::CaseInsensitive)) {
        const bool right = name.contains("Next", Qt::CaseInsensitive);
        pen(QColor("#58636b"));
        p.setBrush(QColor("#89959f"));
        if (name == "parent")
            p.drawPolygon(QPolygonF{{12, 2}, {4, 11}, {8, 11}, {8, 21}, {16, 21}, {16, 11}, {20, 11}});
        else if (right)
            p.drawPolygon(QPolygonF{{21, 12}, {12, 4}, {12, 8}, {3, 8}, {3, 16}, {12, 16}, {12, 20}});
        else
            p.drawPolygon(QPolygonF{{3, 12}, {12, 4}, {12, 8}, {21, 8}, {21, 16}, {12, 16}, {12, 20}});
    } else if (name.contains("Reread", Qt::CaseInsensitive)) {
        pen(QColor("#409b0b"), 2.5);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(4, 4, 16, 16), 30 * 16, 285 * 16);
        p.setBrush(QColor("#58af15"));
        p.drawPolygon(QPolygonF{{20, 3}, {19, 11}, {13, 6}});
    } else if (name.contains("Search", Qt::CaseInsensitive)) {
        pen(QColor("#343434"), 1.5);
        p.setBrush(QColor("#555b63"));
        p.drawRoundedRect(QRectF(3, 7, 7, 13), 2, 2);
        p.drawRoundedRect(QRectF(14, 7, 7, 13), 2, 2);
        p.drawRect(QRectF(8, 9, 8, 5));
        p.setBrush(QColor("#b7d9f5"));
        p.drawEllipse(QRectF(4, 13, 5, 5));
        p.drawEllipse(QRectF(15, 13, 5, 5));
    } else if (name.contains("Thumb", Qt::CaseInsensitive)) {
        pen(QColor("#67736d"));
        p.setBrush(Qt::white);
        p.drawRect(2, 3, 20, 18);
        p.fillRect(4, 5, 16, 14, QColor("#a3d6ef"));
        p.setBrush(QColor("#4b8b27"));
        p.drawPolygon(QPolygonF{{4, 18}, {9, 9}, {13, 14}, {17, 10}, {20, 18}});
    } else if (name.contains("SeparateTree", Qt::CaseInsensitive) ||
               name.contains("DirBranch", Qt::CaseInsensitive) ||
               name.contains("FileSync", Qt::CaseInsensitive)) {
        pen(QColor("#4879a4"));
        p.drawLine(6, 3, 6, 21);
        for (int y : {5, 12, 19}) {
            p.drawLine(6, y, 15, y);
            p.setBrush(QColor("#f2c85d"));
            p.drawRect(12, y - 2, 9, 5);
        }
        if (name.contains("FileSync", Qt::CaseInsensitive)) {
            pen(QColor("#1f8e32"), 2);
            p.drawLine(2, 4, 21, 4);
            p.drawLine(18, 1, 21, 4);
            p.drawLine(2, 22, 21, 22);
            p.drawLine(2, 22, 5, 19);
        }
    } else if (name.contains("Ftp", Qt::CaseInsensitive)) {
        pen(QColor("#526d98"));
        p.setBrush(QColor("#dceaf8"));
        p.drawRect(2, 13, 8, 7);
        p.drawRect(14, 13, 8, 7);
        p.drawLine(6, 11, 18, 11);
        p.drawLine(12, 9, 12, 17);
        p.setPen(name.contains("New", Qt::CaseInsensitive) ? QColor("#1e5baa") : QColor("#bd2222"));
        QFont f("DejaVu Sans", 6);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 24, 10), Qt::AlignCenter,
                   name.contains("New", Qt::CaseInsensitive) ? "URL" : "FTP");
    } else if (name.contains("ExchangeSelection", Qt::CaseInsensitive)) {
        pen(QColor("#a12142"), 2);
        p.drawLine(12, 3, 12, 21);
        p.drawLine(3, 12, 21, 12);
        p.drawLine(5, 5, 19, 19);
        p.drawLine(5, 19, 19, 5);
    } else if (name.contains("Short", Qt::CaseInsensitive) || name.contains("Long", Qt::CaseInsensitive)) {
        for (int y = 4; y <= 19; y += 5) {
            pen(QColor("#a37a1c"));
            p.setBrush(QColor("#ffde70"));
            p.drawRect(2, y, 3, 3);
            pen(QColor("#355f91"));
            p.drawLine(7, y + 1, 12, y + 1);
            if (name.contains("Long", Qt::CaseInsensitive)) {
                pen(QColor("#777777"));
                p.drawLine(15, y + 1, 22, y + 1);
            } else {
                pen(QColor("#a37a1c"));
                p.drawRect(15, y, 3, 3);
                pen(QColor("#355f91"));
                p.drawLine(20, y + 1, 23, y + 1);
            }
        }
    } else if (name == "drive") {
        pen(QColor("#68737b"));
        p.setBrush(QColor("#cdd6dc"));
        p.drawRoundedRect(QRectF(2, 8, 20, 11), 1, 1);
        p.setBrush(QColor("#f0f2f5"));
        p.drawPolygon(QPolygonF{{2, 8}, {6, 4}, {19, 4}, {22, 8}});
        p.fillRect(5, 14, 2, 2, QColor("#3fa243"));
    } else {
        pen(QColor("#5c748b"));
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF{{5, 2}, {15, 2}, {20, 7}, {20, 22}, {5, 22}});
        p.drawLine(15, 2, 15, 7);
        p.drawLine(15, 7, 20, 7);
        if (name.contains("MultiRename", Qt::CaseInsensitive) ||
            name.compare("cm_Edit", Qt::CaseInsensitive) == 0 || name == "editor") {
            pen(QColor("#b8552f"), 3);
            p.drawLine(8, 18, 21, 5);
            pen(QColor("#f7cf69"), 1.5);
            p.drawLine(7, 17, 20, 4);
        } else {
            pen(QColor("#698bae"));
            for (int y : {11, 14, 17})
                p.drawLine(8, y, 17, y);
        }
    }
    p.end();
    QIcon result(pixmap);
    cache.insert(key, result);
    return result;
}
namespace {
class CommanderStyle : public QProxyStyle {
  public:
    CommanderStyle() : QProxyStyle(QStyleFactory::create("Windows")) {}
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override {
        const auto *menu = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (element == CE_MenuItem && menu && menu->checked &&
            QSettings().value("icons/pressedChecks").toBool()) {
            auto copy = *menu;
            copy.checked = false;
            QProxyStyle::drawControl(element, &copy, painter, widget);
            QStyleOptionButton button;
            button.rect = QRect(menu->rect.left() + 3, menu->rect.center().y() - 8, 16, 16);
            button.state = State_Enabled | State_Sunken;
            QProxyStyle::drawPrimitive(PE_PanelButtonCommand, &button, painter, widget);
            if (!menu->icon.isNull())
                menu->icon.paint(painter, button.rect.adjusted(1, 1, -1, -1));
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override {
        if (metric == QStyle::PM_SmallIconSize && qobject_cast<const QMenu *>(widget)) {
            const int sizes[]{16, 20, 24, 32};
            return sizes[qBound(0, QSettings().value("icons/menuSize", 0).toInt(), 3)];
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
} // namespace
void applyCommanderAppearance() {
    if (qApp->property("commanderAppearanceInstalled").toBool())
        return;
    qApp->setProperty("commanderAppearanceInstalled", true);
    qApp->setStyle(new CommanderStyle);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#e5e5eb"));
    palette.setColor(QPalette::Button, QColor("#e5e5eb"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f2f3f7"));
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::ButtonText, Qt::black);
    palette.setColor(QPalette::Highlight, QColor("#000080"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Mid, QColor("#a6a6ad"));
    palette.setColor(QPalette::Light, Qt::white);
    palette.setColor(QPalette::Dark, QColor("#85858b"));
    qApp->setPalette(palette);
    const auto families = QFontDatabase::families();
    QFont font(families.contains("Tahoma") ? "Tahoma" : "Liberation Sans");
    font.setPixelSize(11);
    qApp->setFont(font);
    applySavedAppearance();
}
PanelHost::PanelHost(QTabWidget *tabs, QWidget *parent)
    : QWidget(parent), tabs_(tabs), drives_(new QWidget(this)), drive_(new QComboBox(this)),
      space_(new QLabel(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *buttons = new QHBoxLayout(drives_);
    buttons->setContentsMargins(1, 0, 1, 0);
    buttons->setSpacing(1);
    auto addDrive = [&](const QString &text, const QString &path) {
        auto *button = new QToolButton(drives_);
        button->setText(text);
        button->setIcon(commanderIcon("drive", 16));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(QSettings().value("layout/flatDrives", true).toBool());
        button->setFixedHeight(22);
        button->setProperty("driveRoot", path);
        button->setToolTip(path);
        buttons->addWidget(button);
        drive_->addItem(commanderIcon("drive", 16), text, path);
        connect(button, &QToolButton::clicked, this, [this, path] {
            if (auto *panel = qobject_cast<FilePanel *>(tabs_->currentWidget())) {
                const auto remembered = QSettings()
                                            .value("volumes/" + QString::fromLatin1(path.toUtf8().toBase64()))
                                            .toByteArray();
                panel->navigate(!QSettings().value("navigation/volumeRoot", true).toBool() &&
                                        !remembered.isEmpty()
                                    ? std::filesystem::path(remembered.toStdString())
                                    : nativePath(path));
                panel->focusFiles();
            }
        });
    };
    addDrive("/", "/");
    addDrive("~", QDir::homePath());
    for (const auto &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.rootPath() == "/" ||
            volume.rootPath().startsWith("/proc") || volume.rootPath().startsWith("/sys") ||
            volume.bytesTotal() <= 0)
            continue;
        addDrive(QDir(volume.rootPath()).dirName(), volume.rootPath());
    }
    buttons->addStretch();
    layout->addWidget(drives_);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(2);
    drive_->setFixedWidth(62);
    drive_->setMaxVisibleItems(20);
    drive_->setObjectName("volumeSelector");
    drive_->setAccessibleName("Drive list");
    row->addWidget(drive_);
    row->addWidget(space_, 1);
    space_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    for (const auto &label : {QString("/"), QString("..")}) {
        auto *button = new QToolButton(this);
        button->setText(label);
        button->setFixedSize(24, 23);
        button->setAutoRaise(true);
        button->setAccessibleName(label == "/" ? "Root directory" : "Parent directory");
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, label] {
            if (auto *panel = qobject_cast<FilePanel *>(tabs_->currentWidget())) {
                if (label == "/")
                    panel->navigate("/");
                else
                    panel->parentDirectory();
                panel->focusFiles();
            }
        });
    }
    layout->addLayout(row);
    wrappedTabs_ = new QWidget(this);
    wrappedTabs_->setObjectName("wrappedTabs");
    auto *wrap = new QGridLayout(wrappedTabs_);
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->setSpacing(1);
    auto *scroll = new QScrollArea(this);
    wrappedScroller_ = scroll;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(wrappedTabs_);
    layout->addWidget(scroll);
    scroll->hide();
    layout->addWidget(tabs_, 1);
    connect(drive_, &QComboBox::activated, this, [this](int index) {
        if (!updating_)
            if (auto *panel = qobject_cast<FilePanel *>(tabs_->currentWidget())) {
                const auto root = drive_->itemData(index).toString();
                const auto remembered = QSettings()
                                            .value("volumes/" + QString::fromLatin1(root.toUtf8().toBase64()))
                                            .toByteArray();
                panel->navigate(!QSettings().value("navigation/volumeRoot", true).toBool() &&
                                        !remembered.isEmpty()
                                    ? std::filesystem::path(remembered.toStdString())
                                    : nativePath(root));
                panel->focusFiles();
            }
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { refresh(); });
}
void PanelHost::showDriveButtons(bool visible) {
    drives_->setVisible(visible);
    for (int i = 0; i < drive_->count(); ++i)
        drive_->setItemIcon(i, FinalSettings::flag("IdDriveIconsFromExplorer")
                                   ? QIcon::fromTheme("drive-harddisk", commanderIcon("drive", 16))
                                   : commanderIcon("drive", 16));
    const int spaces[]{0, 2, 4, 8, 16};
    const int x = spaces[qBound(0, QSettings().value("icons/driveX", 0).toInt(), 4)],
              y = spaces[qBound(0, QSettings().value("icons/driveY", 0).toInt(), 4)];
    for (auto *button : drives_->findChildren<QToolButton *>()) {
        button->setAutoRaise(QSettings().value("layout/flatDrives", true).toBool());
        button->setMinimumWidth(26 + x);
        button->setFixedHeight(22 + y);
        const auto root = button->property("driveRoot").toString();
        const QStorageInfo volume(root);
        button->setToolTip(FinalSettings::flag("IdConf_drivetips") ? volume.displayName() + " · " + root
                                                                   : QString());
        button->setIcon(FinalSettings::flag("IdDriveIconsFromExplorer")
                            ? QIcon::fromTheme("drive-harddisk", commanderIcon("drive", 16))
                            : commanderIcon("drive", 16));
    }
}
void PanelHost::refresh() {
    auto *panel = qobject_cast<FilePanel *>(tabs_->currentWidget());
    if (!panel || panel->path().empty())
        return;
    const QStorageInfo mounted(displayPath(panel->path()));
    if (mounted.isValid())
        QSettings().setValue("volumes/" + QString::fromLatin1(mounted.rootPath().toUtf8().toBase64()),
                             QByteArray::fromStdString(panel->path().native()));
    auto path = panel->path();
    while (nativePath(displayPath(path)) != path && path.has_parent_path() && path != path.parent_path())
        path = path.parent_path();
    const QStorageInfo info(displayPath(path));
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    space_->setText(QString("[%1] %2 k of %3 k free")
                        .arg(QString::fromLatin1(info.fileSystemType()),
                             locale.toString(info.bytesAvailable() / 1024),
                             locale.toString(info.bytesTotal() / 1024)));
    space_->setToolTip(info.rootPath() + "\n" + QString::fromLocal8Bit(info.device()));
    updating_ = true;
    int selected = 0, longest = 0;
    for (int i = 0; i < drive_->count(); ++i) {
        const auto candidate = nativePath(drive_->itemData(i).toString());
        const auto text = displayPath(candidate);
        if ((panel->path() == candidate || displayPath(panel->path()).startsWith(text + "/")) &&
            text.size() > longest) {
            selected = i;
            longest = text.size();
        }
    }
    drive_->setCurrentIndex(selected);
    updating_ = false;
}

void applySavedAppearance() {
    QSettings settings;
    auto palette = qApp->palette();
    if (settings.contains("appearance/theme")) {
        const bool dark = settings.value("appearance/theme").toInt() == 1;
        palette = qApp->style()->standardPalette();
        palette.setColor(QPalette::Window, dark ? QColor("#25252b") : QColor("#e5e5eb"));
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::Base, dark ? QColor("#17171b") : QColor("#ffffff"));
        palette.setColor(QPalette::AlternateBase, dark ? QColor("#303038") : QColor("#f2f3f7"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(role, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Highlight, QColor("#000080"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    for (const auto &pair : {std::pair<QString, QPalette::ColorRole>{"textColor", QPalette::Text},
                             {"baseColor", QPalette::Base},
                             {"cursorColor", QPalette::Highlight},
                             {"cursorTextColor", QPalette::HighlightedText},
                             {"alternateColor", QPalette::AlternateBase},
                             {"buttonColor", QPalette::Button}})
        if (settings.contains("appearance/" + pair.first))
            palette.setColor(pair.second, QColor(settings.value("appearance/" + pair.first).toString()));
    const QMap<QString, QPalette::ColorRole> roles{{"Window", QPalette::Window},
                                                   {"WindowText", QPalette::WindowText},
                                                   {"Button", QPalette::Button},
                                                   {"ButtonText", QPalette::ButtonText},
                                                   {"Mid", QPalette::Mid},
                                                   {"Light", QPalette::Light},
                                                   {"Dark", QPalette::Dark},
                                                   {"Link", QPalette::Link},
                                                   {"ToolTipBase", QPalette::ToolTipBase},
                                                   {"ToolTipText", QPalette::ToolTipText}};
    for (auto it = roles.begin(); it != roles.end(); ++it)
        if (settings.contains("appearance/roles/" + it.key()))
            palette.setColor(it.value(), QColor(settings.value("appearance/roles/" + it.key()).toString()));
    if (settings.value("appearance/systemCursor").toBool()) {
        palette.setColor(QPalette::Highlight, qApp->style()->standardPalette().color(QPalette::Highlight));
        palette.setColor(QPalette::HighlightedText,
                         qApp->style()->standardPalette().color(QPalette::HighlightedText));
    }
    qApp->setPalette(palette);
    if (!qApp->property("basePreferenceFont").isValid())
        qApp->setProperty("basePreferenceFont", qApp->font());
    const auto fontFor = [&settings](const QString &key, const QFont &fallback) {
        Q_UNUSED(fallback);
        QFont font = qvariant_cast<QFont>(qApp->property("basePreferenceFont"));
        if (settings.contains("appearance/" + key))
            font.fromString(settings.value("appearance/" + key).toString());
        if (settings.value("appearance/manualDpi").toBool()) {
            const double scale = qBound(72, settings.value("appearance/dpi", 96).toInt(), 240) / 96.0;
            if (font.pixelSize() > 0)
                font.setPixelSize(qMax(6, qRound(font.pixelSize() * scale)));
            else
                font.setPointSizeF(qMax(4.0, font.pointSizeF() * scale));
        }
        return font;
    };
    qApp->setFont(fontFor("dialogFont", qApp->font()));
    for (auto *widget : qApp->allWidgets()) {
        if (qobject_cast<QMainWindow *>(widget))
            widget->setFont(fontFor("mainFont", qApp->font()));
        if (auto *panel = qobject_cast<FilePanel *>(widget)) {
            for (auto *view : panel->findChildren<QAbstractItemView *>()) {
                view->setFont(fontFor("fileFont", qApp->font()));
                view->setAlternatingRowColors(settings.contains("appearance/alternateColor"));
            }
            panel->setActive(panel->isActiveWindow());
            panel->update();
        }
        if (qobject_cast<QDialog *>(widget))
            widget->setFont(fontFor("dialogFont", qApp->font()));
    }
}

void PanelHost::updateTabLayout(bool visible) {
    tabLayoutVisible_ = visible;
    if (!wrappedTabs_)
        return;
    const bool multi = FinalSettings::flag("ChkTabsMultiLines") && visible &&
                       (tabs_->count() > 1 || QSettings().value("classic/alwaysShowTabs").toBool());
    wrappedScroller_->setVisible(multi);
    wrappedTabs_->show();
    if (!multi)
        return;
    tabs_->tabBar()->hide();
    auto *grid = qobject_cast<QGridLayout *>(wrappedTabs_->layout());
    while (auto *item = grid->takeAt(0)) {
        if (auto *old = item->widget()) {
            old->hide();
            old->deleteLater();
        }
        delete item;
    }
    const int columns = qMax(1, width() / 130);
    const int rows = (tabs_->count() + columns - 1) / columns;
    const int rowHeight = qMax(26, fontMetrics().height() + 8);
    wrappedTabs_->setMinimumHeight(rows * rowHeight);
    wrappedScroller_->setFixedHeight(qMin(8, rows) * rowHeight + 4);
    for (int i = 0; i < tabs_->count(); ++i) {
        auto *button = new QToolButton(wrappedTabs_);
        button->setText(tabs_->tabText(i));
        button->setIcon(tabs_->tabIcon(i));
        button->setToolTip(tabs_->tabToolTip(i));
        button->setCheckable(true);
        button->setChecked(i == tabs_->currentIndex());
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QToolButton::clicked, tabs_, [tabs = tabs_, i] { tabs->setCurrentIndex(i); });
        connect(button, &QWidget::customContextMenuRequested, tabs_,
                [tabs = tabs_, button, i](const QPoint &point) {
                    FinalSettings::tabMenu(tabs, i, button->mapToGlobal(point));
                });
        grid->addWidget(button, i / columns, i % columns);
    }
}
void PanelHost::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (FinalSettings::flag("ChkTabsMultiLines"))
        updateTabLayout(tabLayoutVisible_);
}
