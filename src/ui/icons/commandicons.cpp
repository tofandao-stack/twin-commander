#include "ui/icons/commandicons.h"
#include "ui/panels/filepanellayout.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QJsonObject>
#include <QMap>
#include <QHash>
#include <QPixmap>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QToolButton>
namespace {
QString semantic(QString name) {
    if (name.startsWith("tc_")) {
        static const auto names = [] {
            QMap<int, QString> result;
            const auto commands = commanderCommandIds();
            for (auto it = commands.begin(); it != commands.end(); ++it)
                if (!result.contains(it.value().toInt()))
                    result[it.value().toInt()] = it.key();
            return result;
        }();
        name = names.value(name.mid(3).toInt(), name);
    }
    return name.remove('&').toLower();
}
QPixmap draw(const QString &key, int size) {
    QPixmap image(size, size);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24., size / 24.);
    const QColor blue("#2868a0"), green("#31844a"), red("#bf4140"), gold("#d79b2c");
    auto stroke = [&](QColor color = QColor("#36546d"), qreal width = 1.4) {
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
    };
    auto box = [&](QRectF r, QColor fill) {
        stroke();
        p.setBrush(fill);
        p.drawRoundedRect(r, 1.4, 1.4);
    };
    auto sheet = [&](int x = 5, int y = 2) {
        box(QRectF(x, y, 13, 18), QColor("#f2f7fc"));
        stroke(blue, 1);
        for (int i = 0; i < 3; ++i)
            p.drawLine(x + 3, y + 6 + i * 4, x + 10, y + 6 + i * 4);
    };
    auto folder = [&] {
        box({2, 6, 20, 15}, QColor("#f6c95c"));
        box({3, 3, 8, 5}, QColor("#f6c95c"));
        box({2, 9, 20, 12}, QColor("#ffdc7a"));
    };
    auto tick = [&] {
        stroke(green, 2.6);
        p.drawPolyline(QPolygonF{{5, 12}, {10, 17}, {20, 6}});
    };
    auto arrow = [&](bool right) {
        stroke(blue, 2.5);
        p.drawLine(4, 12, 20, 12);
        p.drawPolyline(right ? QPolygonF{{14, 6}, {20, 12}, {14, 18}}
                             : QPolygonF{{10, 6}, {4, 12}, {10, 18}});
    };
    auto text = [&](QString value, QColor color = QColor("#2868a0")) {
        p.setPen(color);
        QFont font("DejaVu Sans", 9, QFont::Bold);
        font.setPixelSize(10);
        p.setFont(font);
        p.drawText(QRectF(1, 4, 22, 18), Qt::AlignCenter, value);
    };
    auto has = [&](const char *s) { return key.contains(QLatin1String(s)); };
    if (has("delete") || has("trash")) {
        box({6, 6, 12, 15}, QColor("#d7e1e8"));
        stroke(red, 2);
        p.drawLine(4, 5, 20, 5);
        p.drawLine(9, 2, 15, 2);
        p.drawLine(10, 9, 10, 18);
        p.drawLine(14, 9, 14, 18);
    } else if (has("cancel") || has("disconnect") || has("unshare") || key.endsWith("stop") || has("quit") ||
               has("exit") || has("close")) {
        stroke(red, 3);
        p.drawLine(6, 6, 18, 18);
        p.drawLine(18, 6, 6, 18);
    } else if (has("copy") || has("clipboard")) {
        sheet(2, 2);
        sheet(9, 5);
    } else if (has("move") || has("renmov")) {
        sheet(1, 2);
        arrow(true);
    } else if (has("rename") || has("edit") || has("editor") || has("comment")) {
        sheet();
        stroke(gold, 4);
        p.drawLine(9, 19, 21, 6);
        stroke(QColor("#674322"), 1);
        p.drawLine(8, 21, 10, 17);
    } else if (has("search") || has("find") || has("quick")) {
        stroke(blue, 2);
        p.setBrush(QColor("#d0e9f7"));
        p.drawEllipse(QRectF(3, 2, 13, 13));
        p.drawLine(15, 15, 22, 22);
    } else if (has("pack") || has("archive") || has("zip")) {
        folder();
        stroke(QColor("#805528"), 3);
        p.drawLine(12, 7, 12, 21);
        stroke(Qt::white, 1);
        for (int y = 9; y < 21; y += 3)
            p.drawLine(10, y, 14, y);
        if (has("unpack")) {
            stroke(green, 2);
            p.drawLine(17, 3, 17, 11);
            p.drawPolyline(QPolygonF{{14, 8}, {17, 11}, {20, 8}});
        }
    } else if (has("mkdir") || has("folder") || has("hotlist") || has("history")) {
        folder();
        if (has("new") || has("mkdir")) {
            stroke(green, 2);
            p.drawLine(17, 12, 17, 20);
            p.drawLine(13, 16, 21, 16);
        }
    } else if (has("ftp") || key == "net" || has("network") || has("connection") || has("share") || has("port")) {
        box({2, 3, 8, 7}, QColor("#b9d9ee"));
        box({14, 14, 8, 7}, QColor("#b9d9ee"));
        stroke(blue, 2);
        p.drawPolyline(QPolygonF{{6, 12}, {6, 18}, {12, 18}});
        p.drawPolyline(QPolygonF{{18, 12}, {18, 6}, {12, 6}});
    } else if (has("sync") || has("exchange") || has("sourcetarget") || has("swappanel")) {
        stroke(green, 2);
        p.drawPolyline(QPolygonF{{3, 7}, {20, 7}, {16, 3}});
        p.drawPolyline(QPolygonF{{21, 17}, {4, 17}, {8, 21}});
    } else if (has("compare") || has("compdir") || has("difference")) {
        sheet(1, 2);
        sheet(11, 2);
        stroke(red, 2);
        p.drawLine(9, 11, 15, 11);
        p.drawLine(9, 15, 15, 15);
    } else if (has("checksum") || has("crc") || has("verify") || has("test") || has("ok") || has("apply")) {
        sheet();
        tick();
    } else if (has("save")) {
        box({3, 2, 18, 20}, blue);
        box({7, 3, 10, 6}, QColor("#dce7ef"));
        box({6, 13, 12, 8}, Qt::white);
    } else if (has("load") || has("restore") || has("open")) {
        folder();
        stroke(blue, 2);
        p.drawPolyline(QPolygonF{{12, 1}, {12, 13}, {8, 9}});
    } else if (has("refresh") || has("reread") || has("update")) {
        stroke(green, 2.5);
        p.drawArc(QRectF{3, 3, 18, 18}, 45 * 16, 285 * 16);
        p.drawPolyline(QPolygonF{{20, 2}, {20, 10}, {13, 7}});
    } else if (has("tree") || has("branch")) {
        stroke(blue);
        p.drawLine(5, 2, 5, 21);
        for (int y : {4, 11, 18}) {
            p.drawLine(5, y, 13, y);
            box({13., double(y - 2), 8, 5}, QColor("#f3c75c"));
        }
    } else if (has("thumb") || has("image") || has("icon") || has("color")) {
        box({2, 3, 20, 18}, QColor("#c1e4f5"));
        p.setBrush(green);
        p.drawPolygon(QPolygonF{{3, 20}, {10, 9}, {14, 15}, {18, 10}, {22, 20}});
        p.setBrush(gold);
        p.drawEllipse(QPointF(17, 7), 2, 2);
    } else if (has("select") || has("mark")) {
        box({3, 3, 18, 18}, QColor("#e1edf8"));
        if (has("unselect")) {
            stroke(red, 2);
            p.drawLine(7, 12, 17, 12);
        } else
            tick();
    } else if (has("previous") || has("back")) {
        arrow(false);
    } else if (has("next") || has("target")) {
        arrow(true);
    } else if (has("parent")) {
        stroke(blue, 2.5);
        p.drawLine(12, 21, 12, 3);
        p.drawPolyline(QPolygonF{{6, 9}, {12, 3}, {18, 9}});
    } else if (has("byname") || has("byext") || has("sort") || has("revers") || has("negorder")) {
        text("A↓");
    } else if (has("date") || has("time") || has("log")) {
        box({3, 3, 18, 18}, QColor("#eff5fa"));
        stroke(blue, 2);
        p.drawEllipse(QRectF{6, 6, 12, 12});
        p.drawPolyline(QPolygonF{{12, 8}, {12, 12}, {16, 14}});
    } else if (has("size") || has("space") || has("usage")) {
        box({3, 5, 18, 15}, QColor("#e7f0f7"));
        stroke(green, 2);
        p.drawLine(6, 17, 6, 12);
        p.drawLine(11, 17, 11, 8);
        p.drawLine(16, 17, 16, 5);
    } else if (has("drive") || has("volume")) {
        box({2, 7, 20, 13}, QColor("#cbd8e2"));
        stroke(green, 2);
        p.drawLine(5, 16, 8, 16);
    } else if (has("font")) {
        text("Aa");
    } else if (has("keyboard")) {
        box({2, 6, 20, 13}, QColor("#e1eaf1"));
        stroke(blue, 1.3);
        for (int y : {9, 12})
            for (int x : {5, 9, 13, 17})
                p.drawPoint(x, y);
        p.drawLine(7, 16, 17, 16);
    } else if (has("plugin")) {
        box({6, 7, 12, 10}, QColor("#a7c7df"));
        stroke(blue, 2);
        p.drawLine(9, 3, 9, 7);
        p.drawLine(15, 3, 15, 7);
        p.drawLine(12, 17, 12, 22);
    } else if (has("help") || has("index") || has("about") || has("registration")) {
        stroke(blue);
        p.setBrush(QColor("#d7eafa"));
        p.drawEllipse(QRectF{2, 2, 20, 20});
        text(has("about") ? "i" : "?");
    } else if (has("website") || has("homepage") || has("language")) {
        stroke(blue);
        p.setBrush(QColor("#d7eafa"));
        p.drawEllipse(QRectF{2, 2, 20, 20});
        p.drawEllipse(QRectF{7, 2, 10, 20});
        p.drawLine(2, 12, 22, 12);
        p.drawLine(4, 7, 20, 7);
        p.drawLine(4, 17, 20, 17);
    } else if (has("short") || has("brief") || has("long") || has("full") || has("column") || has("view") ||
               has("tab") || has("layout") || has("display")) {
        box({2, 3, 20, 18}, QColor("#f0f5fa"));
        stroke(blue);
        p.drawLine(2, 7, 22, 7);
        p.drawLine(12, 7, 12, 21);
        for (int y : {11, 15, 19}) {
            p.drawLine(5, y, 9, y);
            p.drawLine(15, y, 19, y);
        }
    } else if (has("encode") || has("decode") || has("split") || has("combine")) {
        sheet();
        text(has("decode") ? "←" : has("split") ? "÷" : "{}");
    } else if (has("file") || has("print") || has("list")) {
        sheet();
    } else { // Settings, extensions and native commands have a deliberate gear glyph.
        stroke(blue, 2);
        p.setBrush(QColor("#c7ddeb"));
        p.drawEllipse(QRectF{5, 5, 14, 14});
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(12, 12);
            p.rotate(i * 45);
            p.drawLine(0, -8, 0, -10);
            p.restore();
        }
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF{9, 9, 6, 6});
    }
    return image;
}
class IconEventFilter : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show)
            if (auto *w = qobject_cast<QWidget *>(object))
                if (qobject_cast<QDialog *>(w) || qobject_cast<QMenu *>(w))
                    CommandIcons::decorate(w);
        return false;
    }
};
} // namespace
QIcon CommandIcons::icon(const QString &name, int size) {
    size = qBound(12, size, 256);
    const auto directory = QSettings().value("appearance/iconDirectory").toString();
    if (!directory.isEmpty()) {
        QIcon custom(QDir(directory).filePath(name + ".png"));
        if (!custom.isNull())
            return custom;
    }
    const auto key = semantic(name);
    static QHash<QString, QIcon> cache;
    if (!cache.contains(key)) {
        if (cache.size() > 512)
            cache.clear();
        QIcon result;
        for (int n : {16, 20, 24, 32, 48, 64, 128})
            result.addPixmap(draw(key, n));
        cache.insert(key, result);
    }
    Q_UNUSED(size);
    return cache.value(key);
}
QIcon CommandIcons::applicationIcon() {
    commanderUiReference();
    QIcon icon;
    for (int n : {16, 32, 48, 64, 128, 256, 512})
        icon.addFile(QString(":/icons/twin-commander-%1.png").arg(n), QSize(n, n));
    return icon;
}
void CommandIcons::decorate(QWidget *root) {
    const bool menus = QSettings().value("icons/menu", true).toBool();
    auto actions = root->findChildren<QAction *>();
    if (auto *menu = qobject_cast<QMenu *>(root))
        for (auto *a : menu->actions())
            if (!actions.contains(a))
                actions << a;
    for (auto *a : actions)
        if (!a->isSeparator()) {
            bool header = false;
            for (auto *associated : a->associatedObjects())
                header |= qobject_cast<QMenuBar *>(associated) != nullptr;
            if (header) { a->setIcon(QIcon()); continue; }
            if (a->icon().isNull())
                a->setIcon(icon(a->objectName().isEmpty() ? a->text() : a->objectName()));
            a->setIconVisibleInMenu(menus);
        }
    for (auto *button : root->findChildren<QAbstractButton *>())
        if (button->icon().isNull() && !qobject_cast<QCheckBox *>(button) &&
            !qobject_cast<QRadioButton *>(button)) {
            if (button->objectName().startsWith("qt_") || button->width() < 20)
                continue;
            if (auto *tool = qobject_cast<QToolButton *>(button)) {
                if (tool->arrowType() != Qt::NoArrow ||
                    (tool->parentWidget() && (tool->parentWidget()->objectName() == "wrappedTabs" ||
                                             tool->parentWidget()->objectName() == "breadcrumbParts")))
                    continue;
                if (tool->text().size() > 1)
                    tool->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            }
            QString name = button->property("iconCommand").toString();
            if (name.isEmpty())
                name = button->objectName() + " " + button->text();
            button->setIcon(icon(name, 16));
            button->setIconSize(QSize(16, 16));
        }
    if (auto *list = root->findChild<QListWidget *>("configurationCategories"))
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setIcon(icon(list->item(i)->text(), 16));
}
void CommandIcons::install() {
    if (qApp->property("commandIconFilter").toBool())
        return;
    qApp->setProperty("commandIconFilter", true);
    qApp->installEventFilter(new IconEventFilter(qApp));
    qApp->setWindowIcon(applicationIcon());
}
