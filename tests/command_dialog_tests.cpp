#include "filesystem/pathencoding.h"
#include "ui/dialogs/fileoperationdialogs.h"
#include "filesystem/filetransforms.h"
#include "app/mainwindow.h"
#include "platform/nativeextensions.h"
#include "network/remoteservices.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QtTest>
#include <fstream>

class CommandDialogTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static void put(const QString &path, const QByteArray &bytes = "fixture") {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TwinCommanderRemainingGui");
        QCoreApplication::setApplicationName("RemainingGui");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        QDir().mkpath(settings_.filePath("data"));
        qputenv("XDG_DATA_HOME", QFile::encodeName(settings_.filePath("data")));
    }
    void init() {
        QSettings().clear();
        qApp->setProperty("frequentUsage", QVariantMap{});
    }
    void remainingMainCommandsAndConfigurationButtonsHaveHandlers() {
        QTemporaryDir directory;
        MainWindow window({directory.path(), directory.path()});
        window.show();
        QTRY_VERIFY(!window.activePanel()->path().empty());
        window.findChild<QAction *>("cm_Config")->trigger();
        auto *options = window.findChild<QDialog *>("referenceConfiguration");
        QVERIFY(options);
        QFile file(QFINDTESTDATA("../docs/reference/disabled-buttons-0.4.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto baseline = QJsonDocument::fromJson(file.readAll()).array();
        int commands = 0, configuration = 0;
        for (const auto &value : baseline) {
            const auto row = value.toObject();
            const auto context = row.value("context").toString(), id = row.value("id").toString();
            if (context == "commands") {
                QAction *found = nullptr;
                if (id.startsWith("bar_"))
                    found = window.findChild<QAction *>("cm_SwitchSeparateTree");
                else {
                    const auto native = id.mid(3).toInt();
                    for (auto *action : window.findChildren<QAction *>())
                        if (action->property("tcCommandId").toInt() == native) {
                            found = action;
                            break;
                        }
                }
                QVERIFY2(found && (id.startsWith("bar_") || found->property("referenceAvailable").toBool()),
                         qPrintable(id));
                if (id != "tc_552" && id != "tc_553" && id != "tc_515")
                    QVERIFY2(found->isEnabled(), qPrintable(id));
                ++commands;
            } else if (context == "configuration") {
                auto *button = options->findChild<QAbstractButton *>(id);
                QVERIFY2(button && button->isEnabled(), qPrintable(id));
                ++configuration;
            }
        }
        QCOMPARE(commands, 53);
        QCOMPARE(configuration, 73);
        const auto screenshots = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QVERIFY(options->grab().save(screenshots + "/remaining-options.png"));
        }
    }
    void renameNextAndUndoWorkAfterACompletedBatch() {
        QTemporaryDir directory;
        put(directory.filePath("a.txt"));
        JobQueue queue;
        QPointer<BatchRenameDialog> dialog =
            new BatchRenameDialog({nativePath(directory.filePath("a.txt"))}, &queue, directory.filePath("jobs"));
        dialog->show();
        auto *pattern = dialog->findChild<QLineEdit *>("renamePattern");
        auto *apply = dialog->findChild<QPushButton *>("applyRename");
        auto *undo = dialog->findChild<QPushButton *>("renameUndo");
        auto *next = dialog->findChild<QPushButton *>("renameNext");
        QVERIFY(!undo->isEnabled());
        pattern->setText("renamed_[C]");
        QTRY_VERIFY(apply->isEnabled());
        apply->click();
        QTRY_VERIFY(!queue.busy());
        QTRY_VERIFY(undo->isEnabled());
        QVERIFY(next->isEnabled());
        QVERIFY(QFile::exists(directory.filePath("renamed_001.txt")));
        next->click();
        QTRY_COMPARE(dialog->findChild<QTableWidget *>("renamePreview")->item(0, 2)->text(),
                     QString("renamed_001.txt"));
        undo->click();
        QTRY_VERIFY(!queue.busy());
        QTRY_VERIFY(QFile::exists(directory.filePath("a.txt")));
        QVERIFY(!QFile::exists(directory.filePath("renamed_001.txt")));
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
    }
    void thumbnailTreeQuickViewAndCommentModesAreUsable() {
        QTemporaryDir directory, cache;
        put(directory.filePath("text.txt"), "preview text");
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(directory.filePath("image.png")));
        QSettings().setValue("thumbnail/cache", cache.path());
        QSettings().setValue("thumbnail/fields", "size;parent");
        MainWindow window({directory.path(), directory.path()});
        window.show();
        auto *panel = window.activePanel();
        QTRY_COMPARE(panel->model()->rowCount(), 3);
        window.findChild<QAction *>("tc_269")->trigger();
        QTRY_VERIFY(!panel->model()->entry(1)->thumbnail.isNull());
        QVERIFY(window.findChild<QAction *>("tc_269")->isChecked());
        QVERIFY(!QDir(cache.path()).entryList({"tc-thumb-*.json"}, QDir::Files).isEmpty());
        window.findChild<QAction *>("tc_303")->trigger();
        QVERIFY(panel->findChild<QTreeView *>("panelTree")->isVisible());
        QVERIFY(window.findChild<QAction *>("tc_303")->isChecked());
        QCOMPARE(panel->selectedPaths(), std::vector<std::filesystem::path>{nativePath(directory.path())});
        window.findChild<QAction *>("cm_SrcLong")->trigger();
        QVERIFY(window.findChild<QAction *>("cm_SrcLong")->isChecked());
        QTRY_COMPARE(panel->model()->rowCount(), 3);
        panel->selectPaths({nativePath(directory.filePath("text.txt"))});
        window.findChild<QAction *>("tc_304")->trigger();
        auto *dock = window.findChild<QDockWidget *>("quickViewDock");
        QVERIFY(dock && dock->isVisible());
        QVERIFY(window.findChild<QAction *>("tc_304")->isChecked());
        QVERIFY(dock->widget());
        window.findChild<QAction *>("tc_304")->trigger();
        QVERIFY(!dock->isVisible());
        QVERIFY(!window.findChild<QAction *>("tc_304")->isChecked());
        window.findChild<QAction *>("tc_3202")->trigger();
        QVERIFY(window.findChild<QDockWidget *>("sharedDirectoryTree")->isVisible());
        QVERIFY(window.findChild<QDockWidget *>("rightDirectoryTree")->isVisible());
        window.findChild<QAction *>("tc_3200")->trigger();
        QVERIFY(!window.findChild<QDockWidget *>("sharedDirectoryTree")->isVisible());
        window.findChild<QAction *>("tc_300")->trigger();
        QTRY_COMPARE(panel->model()->columnCount(), 6);
        QCOMPARE(panel->model()->headerData(5, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QString("comment"));
        QAction *fullButton = nullptr;
        for (auto *action : window.findChild<QToolBar *>("referenceButtonBar")->actions())
            if (action->property("referenceToolbarCommand")
                    .toString()
                    .compare("cm_SrcLong", Qt::CaseInsensitive) == 0)
                fullButton = action;
        QVERIFY(fullButton);
        fullButton->trigger();
        fullButton->trigger();
        QVERIFY(fullButton->isChecked());
        QVERIFY(window.findChild<QAction *>("cm_SrcLong")->isChecked());
    }
    void hotkeysAliasesAndAutomaticViewRulesApply() {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("sub"));
        put(directory.filePath("sub/file.txt"));
        QSettings().setValue("viewModes",
                             QVariantMap{{"brief", QVariantMap{{"brief", true}, {"sort", 2}, {"order", 1}}}});
        QSettings().setValue("autoViewRules", QVariantList{QVariantMap{{"path", directory.filePath("sub")},
                                                                       {"masks", "*.txt"},
                                                                       {"mode", "brief"}}});
        MainWindow window({directory.path(), directory.path()});
        window.show();
        auto *panel = window.activePanel();
        QTRY_VERIFY(!panel->path().empty());
        panel->navigate(nativePath(directory.filePath("sub")));
        QTRY_VERIFY(panel->isBrief());
        QCOMPARE(panel->table()->horizontalHeader()->sortIndicatorSection(), 2);
        window.findChild<QAction *>("cm_Config")->trigger();
        auto *options = window.findChild<QDialog *>("referenceConfiguration");
        options->findChild<QLineEdit *>("HOT_Command_Edit")->setText("cm_SrcShort");
        options->findChild<QAbstractButton *>("HotkeyRadioBtn")->setChecked(true);
        options->findChild<QCheckBox *>("HOT_Control")->setChecked(true);
        options->findChild<QComboBox *>("HOT_Keys")->setCurrentText("F12");
        options->findChild<QAbstractButton *>("HOT_OKBTN")->click();
        QVERIFY(window.findChild<QAction *>("cm_SrcShort")->shortcuts().contains(QKeySequence("Ctrl+F12")));
        options->findChild<QAbstractButton *>("AliasRadioBtn")->setChecked(true);
        options->findChild<QComboBox *>("AliasCombo")->setEditText("briefme");
        options->findChild<QAbstractButton *>("HOT_OKBTN")->click();
        QVERIFY(QSettings().value("commandAliases").toMap().contains("briefme"));
        window.findChild<QAction *>("cm_SrcLong")->trigger();
        QLineEdit *command = nullptr;
        for (auto *edit : window.findChildren<QLineEdit *>())
            if (edit->accessibleName() == "Command line")
                command = edit;
        QVERIFY(command);
        command->setText("briefme");
        QVERIFY(QMetaObject::invokeMethod(command, "returnPressed", Qt::DirectConnection));
        QVERIFY(panel->isBrief());
        options->findChild<QAbstractButton *>("HOT_CANCELBTN")->click();
        QVERIFY(!QSettings().value("commandAliases").toMap().contains("briefme"));
    }
    void viewerLimitDoesNotChangeTheEditorLimit() {
        QTemporaryDir directory;
        const auto path = directory.filePath("text.txt");
        put(path, QByteArray(1024 * 1024 + 1, 'a'));
        QSettings().setValue("viewer/maxMiB", 1);
        QPointer<FileContentDialog> viewer = new FileContentDialog(nativePath(path), false);
        viewer->show();
        const auto rejected = [viewer] {
            for (auto *label : viewer->findChildren<QLabel *>())
                if (label->text().contains("size limit"))
                    return true;
            return false;
        };
        QTRY_VERIFY(rejected());
        viewer->close();
        QTRY_VERIFY(viewer.isNull());
        QPointer<FileContentDialog> editor = new FileContentDialog(nativePath(path), true);
        editor->show();
        auto *text = editor->findChild<QPlainTextEdit *>();
        QTRY_VERIFY(!text->isReadOnly());
        editor->close();
        QTRY_VERIFY(editor.isNull());
    }
    void ftpMenusFollowConnectionStateAndClosingCancelsWork() {
        if (QStandardPaths::findExecutable("python3").isEmpty() ||
            QStandardPaths::findExecutable("curl").isEmpty())
            QSKIP("python3 and curl required");
        qputenv("NO_PROXY", "127.0.0.1,localhost,::1");
        qputenv("no_proxy", "127.0.0.1,localhost,::1");
        QTemporaryDir directory;
        put(directory.filePath("file.txt"));
        QProcess server;
        server.start("python3", {QFINDTESTDATA("fixtures/ftp_server.py"), directory.path()});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead(5000));
        const int port = server.readLine().trimmed().toInt();
        QVERIFY(port > 0);
        QPointer<MainWindow> window = new MainWindow({directory.path(), directory.path()});
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        QTRY_VERIFY(!window->activePanel()->path().empty());
        QVERIFY(!window->findChild<QAction *>("tc_552")->isEnabled());
        const auto connectToFixture = [&] {
            QTimer::singleShot(0, window, [port] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto edits = dialog->findChildren<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly);
                QCOMPARE(edits.size(), 3);
                edits[0]->setText(QString("ftp://127.0.0.1:%1/").arg(port));
                edits[1]->setText("test");
                edits[2]->setText("secret");
                dialog->accept();
            });
            window->findChild<QAction *>("tc_551")->trigger();
        };
        connectToFixture();
        QTRY_VERIFY(window->findChild<QAction *>("tc_552")->isEnabled());
        QVERIFY(window->findChild<QAction *>("tc_553")->isEnabled());
        auto *browser = window->findChild<FtpBrowserDialog *>("remoteBrowser");
        QVERIFY(browser);
        auto *names = browser->findChild<QListWidget *>();
        QVERIFY(names->count() > 0);
        names->setCurrentRow(0);
        const auto original = browser->findChild<QLineEdit *>()->text();
        for (auto *button : browser->findChildren<QPushButton *>())
            if (button->text() == "Open directory")
                button->click();
        QTRY_COMPARE(browser->findChild<QLineEdit *>()->text(), original);
        QVERIFY(window->findChild<QAction *>("tc_552")->isEnabled());
        window->findChild<QAction *>("tc_552")->trigger();
        QVERIFY(!window->findChild<QAction *>("tc_552")->isEnabled());
        connectToFixture();
        window->close();
        QTRY_VERIFY(window.isNull());
        server.terminate();
        server.waitForFinished();
    }
    void customMenuAndPluginManagersPreserveData() {
        QTemporaryDir directory;
        const auto file = directory.filePath("menu.json");
        put(file, QJsonDocument(
                      QJsonObject{{"menus",
                                   QJsonArray{QJsonObject{
                                       {"caption", "Tools"},
                                       {"items", QJsonArray{QJsonObject{{"caption", "Refresh"},
                                                                        {"command", "cm_RereadSource"}}}}}}}})
                      .toJson());
        QSettings().setValue("menu/file", file);
        MainWindow window({directory.path(), directory.path()});
        window.show();
        QCOMPARE(window.menuBar()->actions().size(), 1);
        QCOMPARE(window.menuBar()->actions()[0]->text(), QString("Tools"));
        QSettings().setValue(
            "nativePlugins",
            QVariantList{QVariantMap{{"name", "field"}, {"kind", "content"}, {"program", "/bin/true"}},
                         QVariantMap{{"name", "packer"}, {"kind", "packer"}, {"program", "/bin/true"}}});
        QTimer::singleShot(0, &window, [] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(editor);
            auto *records = editor->findChild<QTableWidget *>("records");
            QCOMPARE(records->rowCount(), 1);
            records->item(0, 0)->setText("new-field");
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        NativeExtensions::configurePlugins(&window, "content");
        QCOMPARE(NativeExtensions::plugins("content")[0].toMap().value("name").toString(),
                 QString("new-field"));
        QCOMPARE(NativeExtensions::plugins("packer").size(), 1);
    }
};
QTEST_MAIN(CommandDialogTests)
#include "command_dialog_tests.moc"
