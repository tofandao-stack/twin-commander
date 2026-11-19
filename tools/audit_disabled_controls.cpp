// Run against the built commander_ui library; see docs/disabled-controls-audit.md.
#include "mainwindow.h"
#include "toolwindows.h"
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTemporaryDir>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temp;
    app.setOrganizationName("TwinCommanderAudit");
    app.setApplicationName("DisabledControls");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    MainWindow window({temp.path(), temp.path()});
    window.show();
    app.processEvents();
    window.findChild<QAction *>("cm_Config")->trigger();
    app.processEvents();
    auto *dialog = window.findChild<QDialog *>("referenceConfiguration");
    auto *pages = dialog->findChild<QStackedWidget *>("configurationPages");
    auto *categories = dialog->findChild<QListWidget *>("configurationCategories");
    QJsonArray rows;
    for (int i = 0; i < pages->count(); ++i) {
        categories->setCurrentRow(i);
        app.processEvents();
        auto *page = pages->widget(i);
        for (auto *w : page->findChildren<QWidget *>()) {
            if (w->objectName().isEmpty() || w->objectName().startsWith("qt_") || w->isEnabled())
                continue;
            if (!(qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) ||
                  qobject_cast<QLineEdit *>(w) || qobject_cast<QListWidget *>(w) ||
                  qobject_cast<QPlainTextEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
                  qobject_cast<QSlider *>(w)))
                continue;
            QString caption;
            if (auto *b = qobject_cast<QAbstractButton *>(w))
                caption = b->text();
            if (caption.isEmpty()) {
                int best = 100000;
                for (auto *l : page->findChildren<QLabel *>()) {
                    if (l->parentWidget() != w->parentWidget() || l->text().isEmpty())
                        continue;
                    const auto delta = w->pos() - l->pos();
                    if (delta.y() >= -3 && delta.y() < 40 && delta.x() >= -5) {
                        int score = qAbs(delta.y()) * 10 + qAbs(delta.x());
                        if (score < best) {
                            best = score;
                            caption = l->text();
                        }
                    }
                }
            }
            rows.append(QJsonObject{{"page", categories->item(i)->text().trimmed()},
                                    {"id", w->objectName()},
                                    {"type", w->metaObject()->className()},
                                    {"caption", caption},
                                    {"visible", w->isVisible()},
                                    {"tooltip", w->toolTip()}});
        }
    }
    QJsonArray barControls;
    for (const auto &pair : {qMakePair("cm_ButtonConfig", "horizontalBarEditor"),
                             qMakePair("cm_ButtonConfig2", "verticalBarEditor")}) {
        window.findChild<QAction *>(pair.first)->trigger();
        app.processEvents();
        auto *editor = window.findChild<QDialog *>(pair.second);
        for (auto *w : editor->findChildren<QWidget *>()) {
            if (w->objectName().isEmpty() || w->objectName().startsWith("qt_") || w->isEnabled())
                continue;
            if (!(qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) ||
                  qobject_cast<QLineEdit *>(w) || qobject_cast<QListWidget *>(w)))
                continue;
            auto *b = qobject_cast<QAbstractButton *>(w);
            barControls.append(QJsonObject{{"editor", pair.second},
                                           {"id", w->objectName()},
                                           {"type", w->metaObject()->className()},
                                           {"caption", b ? b->text() : QString()},
                                           {"visible", w->isVisible()}});
        }
    }
    QJsonArray actions;
    for (auto *a : window.findChildren<QAction *>())
        if (!a->isEnabled())
            actions.append(QJsonObject{{"id", a->objectName()},
                                       {"caption", a->text()},
                                       {"referenceAvailable", a->property("referenceAvailable").toBool()}});
    SearchDialog search(nativePath(temp.path()), &window);
    const bool nativeSearch = search.findChild<QWidget *>("searchNativeFields")->isEnabled();
    QFile out(argc > 1 ? argv[1] : "build-linux/disabled-controls.json");
    if (!out.open(QIODevice::WriteOnly))
        return 1;
    out.write(QJsonDocument(QJsonObject{{"configuration", rows},
                                        {"nativeFieldSearchEnabled", nativeSearch},
                                        {"buttonBarEditors", barControls},
                                        {"disabledActionsAtStartup", actions}})
                  .toJson());
}
