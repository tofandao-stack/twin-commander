#include "filesystem/pathencoding.h"
#include "operations/operationlog.h"
#include "network/ftptransferoptions.h"
#include "archives/archives.h"
#include "ui/panels/filepanellayout.h"
#include "settings/interfacepreferences.h"
#include "app/mainwindow.h"
#include "search/filesearch.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class InterfacePreferencesTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static void put(const QString &p, const QByteArray &bytes) {
        QFile file(p);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    static QByteArray read(const QString &p) {
        QFile file(p);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static void edit(QDialog *dialog, const char *id, const QString &text) {
        auto *w = dialog->findChild<QLineEdit *>(id);
        QVERIFY(w);
        w->setText(text);
        QMetaObject::invokeMethod(w, "textEdited", Q_ARG(QString, text));
    }
    static QDialog *options(MainWindow &w) {
        w.findChild<QAction *>("cm_Config")->trigger();
        return w.findChild<QDialog *>("referenceConfiguration");
    }
    static QAction *slot(MainWindow &w) {
        for (auto *a : w.findChildren<QAction *>())
            if (a->property("referenceSlot").toInt() == 1 && a->property("referenceToolbarCommand").isValid())
                return a;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TwinCommanderGapsTests");
        QCoreApplication::setApplicationName("Gaps");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
    }
    void init() { QSettings().clear(); }
    void resolvesAnotherHundredControls() {
        QTemporaryDir dir;
        MainWindow window({dir.path(), dir.path()});
        window.show();
        auto *dialog = options(window);
        QVERIFY(dialog);
        QFile baseline(QFINDTESTDATA("../docs/reference/disabled-controls-0.7.json"));
        QVERIFY(baseline.open(QIODevice::ReadOnly));
        const auto old = QJsonDocument::fromJson(baseline.readAll()).object();
        int changed = 0;
        for (const auto &v : old["configuration"].toArray()) {
            const auto id = v.toObject()["id"].toString();
            auto *w = dialog->findChild<QWidget *>(id);
            QVERIFY2(w, qPrintable(id));
            if (w->isEnabled())
                ++changed;
        }
        for (const auto &p : {qMakePair("cm_ButtonConfig", "horizontalBarEditor"),
                              qMakePair("cm_ButtonConfig2", "verticalBarEditor")}) {
            window.findChild<QAction *>(p.first)->trigger();
            auto *editor = window.findChild<QDialog *>(p.second);
            QVERIFY(editor);
            for (const auto &v : old["buttonBarEditors"].toArray()) {
                auto row = v.toObject();
                if (row["editor"] != p.second)
                    continue;
                auto *w = editor->findChild<QWidget *>(row["id"].toString());
                QVERIFY(w);
                if (w->isEnabled())
                    ++changed;
            }
            editor->reject();
        }
        SearchDialog search(nativePath(dir.path()));
        QVERIFY(search.findChild<QCheckBox *>("searchNativeFields")->isEnabled());
        ++changed;
        QVERIFY2(changed >= 100, qPrintable(QString("Only %1 audited controls enabled").arg(changed)));
        dialog->findChild<QCheckBox *>("FtpPassiveMode")->setChecked(false);
        dialog->reject();
        QVERIFY(!QSettings().contains("ftp/passive"));
    }
    void loggingFiltersRotationAndNoFollow() {
        QTemporaryDir dir;
        auto log = dir.filePath("ops.log");
        QSettings s;
        s.setValue("logging/enabled", true);
        s.setValue("logging/file", log);
        s.setValue("logging/transfer", false);
        OperationLog::append("transfer", "success", "ignored");
        QVERIFY(!QFile::exists(log));
        s.setValue("logging/transfer", true);
        s.setValue("logging/error", false);
        OperationLog::append("transfer", "error", "ignored");
        QVERIFY(!QFile::exists(log));
        s.setValue("logging/rotation", 2);
        s.setValue("logging/sizeKiB", 1);
        s.setValue("logging/retention", true);
        s.setValue("logging/keep", 2);
        put(dir.filePath("ops.log.tc-log.foreign"), "foreign data");
        put(dir.filePath("target"), "untouched");
        QVERIFY(QFile::link(dir.filePath("target"), dir.filePath("ops.log.tc-log.link")));
        for (int i = 0; i < 5; ++i)
            OperationLog::append("transfer", "success", QString(2000, 'x'));
        const auto names = QDir(dir.path()).entryList({"ops.log.tc-log.*"}, QDir::Files);
        QCOMPARE(names.size(), 3);
        QCOMPARE(read(dir.filePath("ops.log.tc-log.foreign")), QByteArray("foreign data"));
        QCOMPARE(read(dir.filePath("target")), QByteArray("untouched"));
        QVERIFY(read(log).startsWith("# TwinCommander log v1\n"));
        QVERIFY(QJsonDocument::fromJson(read(log).split('\n')[1]).isObject());
        QFile::remove(log);
        QVERIFY(QFile::link(dir.filePath("target"), log));
        OperationLog::append("transfer", "success", "must refuse link");
        QCOMPARE(read(dir.filePath("target")), QByteArray("untouched"));
    }
    void ftpOptionsAreBoundedAndUploadsNeverRetried() {
        QSettings s;
        s.setValue("ftp/passive", false);
        s.setValue("ftp/portRange", true);
        s.setValue("ftp/portFrom", 51000);
        s.setValue("ftp/portTo", 51010);
        s.setValue("ftp/externalIP", true);
        s.setValue("ftp/portAddress", "127.0.0.1");
        s.setValue("ftp/mode", 2);
        s.setValue("ftp/retries", 4);
        s.setValue("ftp/retryDelay", 2);
        s.setValue("ftp/stallRetry", true);
        const auto args = FtpTransferOptions::arguments(QUrl("ftp://localhost/file.txt"), true, false);
        QVERIFY(args.contains("127.0.0.1:51000-51010"));
        QVERIFY(args.contains("--use-ascii"));
        QVERIFY(args.contains("--retry-all-errors"));
        const auto upload = FtpTransferOptions::arguments(QUrl("ftp://localhost/file.bin"), true, true);
        QVERIFY(!upload.contains("--retry"));
        QVERIFY(!upload.contains("--retry-all-errors"));
        QVERIFY(!upload.contains("--use-ascii"));
        s.setValue("ftp/portFrom", 60000);
        s.setValue("ftp/portTo", 50000);
        QVERIFY_EXCEPTION_THROWN(FtpTransferOptions::arguments(QUrl("ftp://localhost/"), false, false),
                                 std::exception);
    }
    void compressionAndArchiveDate() {
        QTemporaryDir dir;
        put(dir.filePath("large.txt"), QByteArray(300000, 'x'));
        const auto source = nativePath(dir.filePath("large.txt"));
        const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        std::filesystem::last_write_time(source, old);
        QSettings().setValue("archive/zipLevelChoice", 3);
        QSettings().setValue("archive/zipLevel", 0);
        auto control = std::make_shared<LocalFiles::Control>();
        auto first =
            Archives::create({source}, nativePath(dir.filePath("zero.zip")), Archives::Format::Zip, control);
        QVERIFY2(first.items[0].error.isEmpty(), qPrintable(first.items[0].error));
        QSettings().setValue("archive/zipLevelChoice", 1);
        QSettings().setValue("archive/newestDate", true);
        auto second = Archives::create({source}, nativePath(dir.filePath("nine.zip")), Archives::Format::Zip,
                                       std::make_shared<LocalFiles::Control>());
        QVERIFY2(second.items[0].error.isEmpty(), qPrintable(second.items[0].error));
        QVERIFY(QFileInfo(dir.filePath("nine.zip")).size() < QFileInfo(dir.filePath("zero.zip")).size());
        QVERIFY(std::abs(std::chrono::duration_cast<std::chrono::seconds>(
                             std::filesystem::last_write_time(nativePath(dir.filePath("nine.zip"))) - old)
                             .count()) < 2);
        const auto listing =
            Archives::list(nativePath(dir.filePath("nine.zip")), std::make_shared<LocalFiles::Control>());
        QVERIFY(listing.error.isEmpty());
        QCOMPARE(listing.entries.size(), size_t(1));
    }
    void nativeFieldSearchAndTextThumbnails() {
        QTemporaryDir dir;
        put(dir.filePath("note.txt"), "hello native thumbnail");
        put(dir.filePath("other.bin"), "other");
        SearchRequest request;
        request.root = nativePath(dir.path());
        request.field = "parent";
        request.fieldValue = QFileInfo(dir.path()).fileName();
        auto result = searchFiles(request, std::make_shared<LocalFiles::Control>());
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(result.paths.size(), size_t(2));
        request.fieldValue = "not this parent";
        QVERIFY(searchFiles(request, std::make_shared<LocalFiles::Control>()).paths.empty());
        QSettings().setValue("thumbnail/text", true);
        QSettings().setValue("thumbnail/images", false);
        DirectoryModel model;
        model.setThumbnails(true);
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(dir.path()));
        QTRY_COMPARE(loaded.count(), 1);
        bool found = false;
        for (int i = 0; i < model.rowCount(); ++i)
            if (model.entry(i)->name == "note.txt") {
                found = true;
                QVERIFY(!model.entry(i)->thumbnail.isNull());
            }
        QVERIFY(found);
    }
    void externalToolbarArgumentsAndPresentation() {
        QTemporaryDir dir;
        const auto program = dir.filePath("capture");
        put(program, "#!/bin/sh\nprintf '%s\\n' \"$@\" > result\n");
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        MainWindow window({dir.path(), dir.path()});
        window.show();
        QTRY_VERIFY(!window.activePanel()->path().empty());
        window.findChild<QAction *>("cm_ButtonConfig")->trigger();
        auto *editor = window.findChild<QDialog *>("horizontalBarEditor");
        QVERIFY(editor);
        editor->findChild<QListWidget *>("id_btn_bar")->setCurrentRow(0);
        edit(editor, "W_btn_command", program);
        edit(editor, "W_btn_param", "\"two words\" \"$(touch hacked)\"");
        edit(editor, "W_btn_startpath", dir.path());
        edit(editor, "id_btn_barsize", "48");
        editor->findChild<QCheckBox *>("id_SmallIcons")->setChecked(true);
        editor->findChild<QPushButton *>("Ok")->click();
        auto *action = slot(window);
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        action->trigger();
        QTRY_VERIFY(QFile::exists(dir.filePath("result")));
        QCOMPARE(read(dir.filePath("result")), QByteArray("two words\n$(touch hacked)\n"));
        QVERIFY(!QFile::exists(dir.filePath("hacked")));
        bool size = false;
        for (auto *tool : window.findChildren<QToolButton *>())
            if (tool->defaultAction() == action) {
                QCOMPARE(tool->size(), QSize(48, 48));
                size = true;
            }
        QVERIFY(size);
    }
    void directModesThemeAndDpi() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("child"));
        QSettings s;
        s.setValue("viewModes",
                   QVariantMap{{"A", QVariantMap{{"sort", 0}}}, {"B", QVariantMap{{"sort", 0}}}});
        s.setValue("autoViewRules",
                   QVariantList{QVariantMap{{"path", dir.filePath("child")}, {"mode", "B"}}});
        MainWindow window({dir.path(), dir.path()});
        window.show();
        QTRY_VERIFY(!window.activePanel()->path().empty());
        auto *dialog = options(window);
        const int unscaledWidth = dialog->width();
        dialog->findChild<QListWidget *>("ViewModeList")->setCurrentRow(0);
        edit(dialog, "AdditionalSortEdit", "date");
        dialog->findChild<QComboBox *>("id_TabColorBg")->setCurrentText("#123456");
        dialog->findChild<QListWidget *>("ModeChangeList")->setCurrentRow(0);
        auto *choose = dialog->findChild<QComboBox *>("ModeChooseCombo");
        choose->setCurrentIndex(choose->findText("A"));
        QMetaObject::invokeMethod(choose, "activated", Q_ARG(int, choose->currentIndex()));
        dialog->findChild<QComboBox *>("id_DarkModeCombo")->setCurrentIndex(1);
        dialog->findChild<QCheckBox *>("DpiCheck")->setChecked(true);
        dialog->findChild<QComboBox *>("DpiEdit")->setCurrentText("144");
        dialog->findChild<QPushButton *>("configurationOK")->click();
        QCOMPARE(s.value("viewModes").toMap().value("A").toMap().value("secondary").toString(),
                 QString("date"));
        QCOMPARE(s.value("autoViewRules").toList()[0].toMap().value("mode").toString(), QString("A"));
        QVERIFY(qApp->palette().color(QPalette::Base).lightness() < 80);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        dialog = options(window);
        QCOMPARE(dialog->width(), qRound(unscaledWidth * 1.5));
        dialog->reject();
        window.activePanel()->navigate(nativePath(dir.filePath("child")));
        QTRY_COMPARE(window.activePanel()->path(), nativePath(dir.filePath("child")));
        QTRY_COMPARE(window.activePanel()->table()->palette().color(QPalette::Base), QColor("#123456"));
        window.findChild<QAction *>("cm_DarkNormal")->trigger();
        QCOMPARE(s.value("appearance/theme").toInt(), 0);
        s.setValue("appearance/manualDpi", false);
        applySavedAppearance();
    }
    void breadcrumbsAndMouseMarking() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("child"));
        put(dir.filePath("file.txt"), "content");
        QSettings().setValue("layout/breadcrumbs", true);
        QSettings().setValue("mouse/selection", 0);
        FilePanel panel(nativePath(dir.filePath("child")));
        panel.show();
        QTRY_VERIFY(!panel.path().empty());
        auto *parts = panel.findChild<QWidget *>("breadcrumbParts");
        QVERIFY(parts);
        QVERIFY(parts->isVisible());
        QToolButton *parent = nullptr;
        for (auto *b : parts->findChildren<QToolButton *>())
            if (b->property("breadcrumbPath").toByteArray() == QFile::encodeName(dir.path()))
                parent = b;
        QVERIFY(parent);
        parent->click();
        QTRY_COMPARE(panel.path(), nativePath(dir.path()));
        int row = -1;
        for (int i = 0; i < panel.model()->rowCount(); ++i)
            if (panel.model()->entry(i)->name == "file.txt")
                row = i;
        QVERIFY(row >= 0);
        panel.table()->clearSelection();
        QTest::mouseClick(panel.table()->viewport(), Qt::RightButton, Qt::NoModifier,
                          panel.table()->visualRect(panel.model()->index(row, 0)).center());
        QVERIFY(panel.table()->selectionModel()->isRowSelected(row, {}));
    }
};
QTEST_MAIN(InterfacePreferencesTests)
#include "interface_preferences_tests.moc"
