#include "filesystem/pathencoding.h"
#include "app/mainwindow.h"
#include "settings/panelpreferences.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class PreferencesTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static void put(const QString &path, const QByteArray &text = "data") {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(text), text.size());
    }
    static int row(DirectoryModel *model, const QString &name) {
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->entry(i)->name == name)
                return i;
        return -1;
    }
    static QDialog *options(MainWindow &window) {
        window.findChild<QAction *>("cm_Config")->trigger();
        return window.findChild<QDialog *>("referenceConfiguration");
    }
    static void button(QDialog *dialog, const QString &caption) {
        for (auto *b : dialog->findChildren<QPushButton *>())
            if (b->text().remove('&') == caption) {
                b->click();
                return;
            }
        QFAIL(qPrintable("Missing button: " + caption));
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TwinCommanderPreferencesTests");
        QCoreApplication::setApplicationName("PanelPreferences");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
    }
    void init() { QSettings().clear(); }
    void baselineReductionAndCancel() {
        QTemporaryDir dir;
        MainWindow window({dir.path(), dir.path()});
        window.show();
        auto *dialog = options(window);
        QVERIFY(dialog);
        QFile baseline(QFINDTESTDATA("../docs/reference/disabled-controls-current.json"));
        QVERIFY(baseline.open(QIODevice::ReadOnly));
        int nowEnabled = 0;
        for (const auto &v :
             QJsonDocument::fromJson(baseline.readAll()).object()["configuration"].toArray()) {
            auto *w = dialog->findChild<QWidget *>(v.toObject()["id"].toString());
            QVERIFY(w);
            if (w->isEnabled())
                ++nowEnabled;
        }
        QVERIFY2(nowEnabled >= 90,
                 qPrintable(QString("Only %1 baseline configuration controls enabled").arg(nowEnabled)));
        dialog->findChild<QCheckBox *>("IdDirBrackets")->setChecked(true);
        dialog->findChild<QComboBox *>("id_sizeDisplay")->setCurrentIndex(3);
        button(dialog, "Cancel");
        QVERIFY(!QSettings().contains("display/brackets"));
        QVERIFY(!QSettings().contains("format/size"));
    }
    void applyFormattingAndReopen() {
        QTemporaryDir dir;
        put(dir.filePath("sample.txt"), QByteArray(2048, 'x'));
        QDir().mkdir(dir.filePath("folder"));
        MainWindow window({dir.path(), dir.path()});
        window.show();
        auto *panel = window.activePanel();
        QTRY_VERIFY(row(panel->model(), "sample.txt") >= 0);
        auto *dialog = options(window);
        dialog->findChild<QRadioButton *>("id_confExtAfterName")->click();
        dialog->findChild<QComboBox *>("id_sizeDisplay")->setCurrentIndex(1);
        dialog->findChild<QCheckBox *>("IdDirBrackets")->setChecked(true);
        dialog->findChild<QCheckBox *>("IdDirBackslash")->setChecked(true);
        dialog->findChild<QLineEdit *>("id_TabDate")->setText("210");
        dialog->findChild<QComboBox *>("idiconsize")->setCurrentIndex(3);
        dialog->findChild<QCheckBox *>("IdIconsForMenu")->setChecked(false);
        QSignalSpy loaded(panel->model(), &DirectoryModel::loaded);
        button(dialog, "Apply");
        QTRY_VERIFY(loaded.count() > 0);
        const auto index = panel->model()->index(row(panel->model(), "sample.txt"), 0);
        QCOMPARE(index.data().toString(), QString("sample.txt"));
        QVERIFY(panel->table()->isColumnHidden(1));
        QCOMPARE(panel->model()->index(index.row(), 2).data().toString(), QString("2 k"));
        QCOMPARE(panel->model()->index(row(panel->model(), "folder"), 0).data().toString(),
                 QString("[folder]/"));
        QCOMPARE(panel->table()->columnWidth(3), 210);
        QCOMPARE(panel->table()->iconSize(), QSize(32, 32));
        int toolbarIcons = 0;
        for (auto *tool : window.findChildren<QToolButton *>())
            if (tool->property("referenceSlot").isValid() && tool->defaultAction()) {
                QVERIFY(!tool->icon().isNull());
                const bool enabled = tool->defaultAction()->isEnabled();
                tool->defaultAction()->setEnabled(!enabled);
                QVERIFY(!tool->icon().isNull());
                tool->defaultAction()->setEnabled(enabled);
                ++toolbarIcons;
            }
        QVERIFY(toolbarIcons > 0);
        button(dialog, "OK");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        dialog = options(window);
        QCOMPARE(dialog->findChild<QComboBox *>("id_sizeDisplay")->currentIndex(), 1);
        QVERIFY(dialog->findChild<QRadioButton *>("id_confExtAfterName")->isChecked());
        button(dialog, "Cancel");
    }
    void formatsAndSorting() {
        QSettings s;
        s.setValue("format/century", false);
        s.setValue("format/ampm", true);
        s.setValue("format/seconds", 1);
        s.setValue("format/dateOrder", 1);
        s.setValue("format/dateSeparator", "/");
        s.setValue("format/timeSeparator", ".");
        QCOMPARE(PanelPreferences::dateText(QDateTime(QDate(2026, 11, 18), QTime(14, 3, 5))),
                 QString("18/11/26 02.03.05 PM"));
        s.setValue("format/thousands", " ");
        QCOMPARE(PanelPreferences::sizeText(1234567, 0), QString("1 234 567"));
        QTemporaryDir dir;
        put(dir.filePath("file10"));
        put(dir.filePath("file2"));
        QDir().mkdir(dir.filePath("z-dir"));
        QDir().mkdir(dir.filePath("a-dir"));
        s.setValue("display/sortMethod", 2);
        s.setValue("display/directorySort", 0);
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(dir.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(row(&model, "file2") < row(&model, "file10"));
        model.sort(2, Qt::DescendingOrder);
        QVERIFY(row(&model, "a-dir") < row(&model, "z-dir"));
    }
    void quickSearchAndSelectionPolicy() {
        QTemporaryDir dir;
        put(dir.filePath("alpha.txt"));
        put(dir.filePath("beta.txt"));
        QDir().mkdir(dir.filePath("folder"));
        FilePanel panel(nativePath(dir.path()));
        panel.show();
        QTRY_VERIFY(row(panel.model(), "beta.txt") >= 0);
        panel.focusFiles();
        QTest::keyClicks(panel.table(), "bet");
        QCOMPARE(panel.model()->entry(panel.table()->currentIndex().row())->name, QString("beta.txt"));
        QSettings().setValue("quick/mode", 1);
        QTest::qWait(1250);
        QTest::keyClick(panel.table(), Qt::Key_A, Qt::AltModifier);
        QCOMPARE(panel.model()->entry(panel.table()->currentIndex().row())->name, QString("alpha.txt"));
        QSettings().setValue("selection/directories", 0);
        panel.table()->clearSelection();
        panel.selectMask("*");
        for (const auto &path : panel.selectedPaths())
            QVERIFY(!std::filesystem::is_directory(path));
        QSettings().setValue("quick/mode", 4);
        const auto before = panel.table()->currentIndex();
        QTest::keyClicks(panel.table(), "alpha");
        QCOMPARE(panel.table()->currentIndex(), before);
    }
    void automaticRefreshAndAppendPreservesSelection() {
        QTemporaryDir dir;
        put(dir.filePath("z-file"));
        QSettings().setValue("refresh/names", true);
        QSettings().setValue("refresh/metadata", true);
        QSettings().setValue("refresh/append", true);
        FilePanel panel(nativePath(dir.path()));
        panel.show();
        QTRY_VERIFY(row(panel.model(), "z-file") >= 0);
        panel.selectPaths({nativePath(dir.filePath("z-file"))});
        put(dir.filePath("a-new"));
        QTRY_VERIFY_WITH_TIMEOUT(row(panel.model(), "a-new") >= 0, 5000);
        QVERIFY(row(panel.model(), "a-new") > row(panel.model(), "z-file"));
        QCOMPARE(panel.selectedPaths().size(), size_t(1));
        QCOMPARE(panel.selectedPaths()[0], nativePath(dir.filePath("z-file")));
        put(dir.filePath("z-file"), "longer changed data");
        QTRY_COMPARE_WITH_TIMEOUT(panel.model()->entry(row(panel.model(), "z-file"))->size, quint64(19),
                                  5000);
    }
    void tabsAndSessionPreferences() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("a-long-directory-name"));
        MainWindow window({dir.filePath("a-long-directory-name"), dir.path()});
        window.show();
        auto *tabs = qobject_cast<QTabWidget *>(window.activePanel()->parentWidget()->parentWidget());
        // QTabWidget owns a QStackedWidget, which owns each panel.
        QVERIFY(tabs);
        QTRY_VERIFY(!window.activePanel()->path().empty());
        QSettings().setValue("tabs/nearCurrent", true);
        QSettings().setValue("tabs/foreground", false);
        QSettings().setValue("tabs/limit", true);
        QSettings().setValue("tabs/titleLength", 5);
        auto *before = tabs->currentWidget();
        window.findChild<QAction *>("cm_OpenNewTab")->trigger();
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentWidget(), before);
        QTRY_VERIFY(!qobject_cast<FilePanel *>(tabs->widget(1))->path().empty());
        QVERIFY(tabs->tabText(1).size() <= 5);
        QSettings().setValue("session/directories", false);
        QSettings().setValue("session/commands", false);
        window.findChild<QAction *>("cm_ConfigSaveSettings")->trigger();
        QVERIFY(QSettings().value("leftPaths/native").toList().isEmpty());
    }
    void historyLimitsAndBookmarkEditor() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("child"));
        FilePanel panel(nativePath(dir.path()));
        panel.show();
        QTRY_VERIFY(!panel.path().empty());
        panel.navigate(nativePath(dir.filePath("child")));
        QTRY_COMPARE(panel.path(), nativePath(dir.filePath("child")));
        QSettings().setValue("history/saveLimit", 1);
        panel.storeHistory();
        QCOMPARE(QSettings().value("directoryHistoryNative").toList().size(), 1);
        QSettings().setValue("history/showLimit", 1);
        panel.showHistory();
        auto *menu = panel.findChild<QMenu *>("directoryHistoryMenu");
        QVERIFY(menu);
        QCOMPARE(menu->actions().size(), 1);
        menu->close();
        QSettings().setValue("bookmarksNative", QVariantList{QFile::encodeName(dir.path())});
        MainWindow window({dir.path(), dir.path()});
        window.show();
        window.findChild<QAction *>("cm_DirectoryHotlist")->trigger();
        auto *configure = window.findChild<QAction *>("configureBookmarks");
        QVERIFY(configure);
        QVERIFY(configure->isEnabled());
        configure->trigger();
        auto *editor = window.findChild<QDialog *>("bookmarkEditor");
        QVERIFY(editor);
        auto *list = editor->findChild<QListWidget *>("bookmarkList");
        QCOMPARE(list->count(), 1);
        delete list->takeItem(0);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(QSettings().value("bookmarksNative").toList().isEmpty());
    }
};
QTEST_MAIN(PreferencesTests)
#include "preferences_tests.moc"
