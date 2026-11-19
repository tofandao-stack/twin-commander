#include "extendedtools.h"
#include "finalarchives.h"
#include "finalsettings.h"
#include "mainwindow.h"
#include "preferences.h"
#include "remotetools.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
#include <fstream>
#include <sys/xattr.h>

class FinalControlsTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static void put(const QString &path, const QByteArray &value) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(value), value.size());
    }
    static QByteArray read(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return f.readAll();
    }
    static void set(const char *id, const QVariant &value) {
        QSettings().setValue("final/" + QString::fromLatin1(id), value);
    }
    static std::shared_ptr<LocalFiles::Control> control() { return std::make_shared<LocalFiles::Control>(); }
    static int row(DirectoryModel *m, const QString &name) {
        for (int i = 0; i < m->rowCount(); ++i)
            if (m->entry(i)->name == name)
                return i;
        return -1;
    }
    static QDialog *options(MainWindow &window) {
        window.findChild<QAction *>("cm_Config")->trigger();
        return window.findChild<QDialog *>("referenceConfiguration");
    }
    static void fixture(QProcess &server, const QString &root, QString &address) {
        server.start("python3", {QFINDTESTDATA("fixtures/ftp_server.py"), root});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead(5000));
        const auto port = server.readLine().trimmed().toInt();
        QVERIFY(port > 0);
        address = QString("ftp://127.0.0.1:%1/").arg(port);
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TwinCommanderFinalTests");
        QCoreApplication::setApplicationName("FinalControls");
        QCoreApplication::setApplicationVersion("0.9.0");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        QDir().mkpath(settings_.filePath("data"));
        qputenv("XDG_DATA_HOME", QFile::encodeName(settings_.filePath("data")));
        qputenv("NO_PROXY", "127.0.0.1,localhost");
    }
    void init() {
        QSettings().clear();
        FinalSettings::instanceLock();
        qApp->setProperty("nativeLanguage", QVariantMap{});
    }
    void allRemainingControlsHaveLiveBindingsAndCancel() {
        QTemporaryDir dir;
        MainWindow window({dir.path(), dir.path()});
        window.show();
        auto *dialog = options(window);
        QVERIFY(dialog);
        QFile file(QFINDTESTDATA("../docs/reference/disabled-controls-0.8.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto baseline = QJsonDocument::fromJson(file.readAll()).object()["configuration"].toArray();
        QCOMPARE(baseline.size(), 106);
        for (const auto &v : baseline) {
            const auto id = v.toObject()["id"].toString();
            auto *widget = dialog->findChild<QWidget *>(id);
            QVERIFY2(widget && widget->isEnabled(), qPrintable(id));
            QVERIFY2(!widget->toolTip().contains("not implemented"), qPrintable(id));
        }
        dialog->findChild<QCheckBox *>("ChkVerifyAfterCopy")->setChecked(true);
        dialog->findChild<QComboBox *>("z7_dict")->setCurrentText("64m");
        dialog->reject();
        QVERIFY(!QSettings().contains("final/ChkVerifyAfterCopy"));
        QVERIFY(!QSettings().contains("final/z7_dict"));
    }
    void copyMethodsVerificationAndMetadata() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("source"));
        const auto source = dir.filePath("source/item.bin");
        const auto bytes = QByteArray(300000, 'q') + "last bytes";
        put(source, bytes);
        ExtendedTools::setComment(nativePath(source), "copied comment");
        set("ChkVerifyAfterCopy", true);
        set("IdConf_comments_copy", true);
        for (int method = 0; method < 3; ++method) {
            QSettings().setValue("final/copyMethod", method);
            set("EdSmallSame", "4");
            set("EdBigSame", "8");
            const auto destination = dir.filePath(QString("target%1").arg(method));
            QDir().mkdir(destination);
            const auto result = LocalFiles::execute(
                {LocalFiles::Operation::Copy, {nativePath(source)}, nativePath(destination)}, control());
            QCOMPARE(result.items.size(), size_t(1));
            QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
            QCOMPARE(read(destination + "/item.bin"), bytes);
            QCOMPARE(ExtendedTools::comment(nativePath(destination + "/item.bin")),
                     QString("copied comment"));
        }
    }
    void legacyCommentsRoundTripEncodingAndNoFollow() {
        QTemporaryDir dir;
        put(dir.filePath("a.txt"), "a");
        put(dir.filePath("descript.ion"), "# preserved header\nunrelated.txt other\n");
        set("IdPreferredCommentFormat", "descript.ion");
        set("IdConf_comments_doscharset", true);
        FinalSettings::setComment(nativePath(dir.filePath("a.txt")), QString::fromUtf8("café"));
        QCOMPARE(FinalSettings::comment(nativePath(dir.filePath("a.txt"))), QString::fromUtf8("café"));
        QVERIFY(read(dir.filePath("descript.ion")).contains(char(0x82)));
        QVERIFY(read(dir.filePath("descript.ion")).contains("# preserved header"));
        FinalSettings::setComment(nativePath(dir.filePath("a.txt")), {});
        QVERIFY(FinalSettings::comment(nativePath(dir.filePath("a.txt"))).isEmpty());
        QVERIFY(read(dir.filePath("descript.ion")).contains("unrelated.txt"));
        QFile::remove(dir.filePath("descript.ion"));
        put(dir.filePath("sentinel"), "unchanged");
        QVERIFY(QFile::link(dir.filePath("sentinel"), dir.filePath("descript.ion")));
        QVERIFY_EXCEPTION_THROWN(FinalSettings::setComment(nativePath(dir.filePath("a.txt")), "refuse"),
                                 std::exception);
        QCOMPARE(read(dir.filePath("sentinel")), QByteArray("unchanged"));
    }
    void displayAliasesSystemAttributesAndOpaqueDeletion() {
        QTemporaryDir dir;
        put(dir.filePath("SHORT.TXT"), "short");
        put(dir.filePath("very long filename.txt"), "long");
        put(dir.filePath("system.txt"), "sys");
        QCOMPARE(::lsetxattr(QFile::encodeName(dir.filePath("system.txt")).constData(), "user.DOSATTRIB",
                             "0x4", 3, 0),
                 0);
        set("IdConfshowSystem", false);
        set("IdConfLower83Names", true);
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(dir.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QCOMPARE(row(&model, "system.txt"), -1);
        QVERIFY(row(&model, "SHORT.TXT") >= 0);
        QCOMPARE(model.index(row(&model, "SHORT.TXT"), 0).data().toString(), QString("short"));
        set("IdConf_longnames", false);
        const auto alias = model.index(row(&model, "very long filename.txt"), 0).data().toString();
        QVERIFY(alias.contains('~'));
        QVERIFY(alias.size() <= 8);
        QCOMPARE(model.entry(row(&model, "very long filename.txt"))->path,
                 nativePath(dir.filePath("very long filename.txt")));
        const auto raw = nativePath(dir.path()) / std::filesystem::path(std::string("opaque-\xff", 8));
        {
            std::ofstream f(raw);
            f << "raw";
        }
        const auto displayed = displayPath(raw);
        put(displayed, "literal replacement");
        set("ChkExplorerDelete", true);
        auto result = LocalFiles::execute({LocalFiles::Operation::Delete, {raw}, {}}, control());
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(std::filesystem::exists(raw));
        QCOMPARE(read(displayed), QByteArray("literal replacement"));
        set("ChkExplorerDelete", false);
        result = LocalFiles::execute({LocalFiles::Operation::Delete, {raw}, {}}, control());
        QVERIFY(result.items[0].error.isEmpty());
        QVERIFY(!std::filesystem::exists(raw));
        QCOMPARE(read(displayed), QByteArray("literal replacement"));
    }
    void partialAndDamagedArchivesNeverBypassPathValidation() {
        QTemporaryDir dir;
        QProcess python;
        python.start(
            "python3",
            {"-c",
             "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',compression=zipfile.ZIP_STORED); "
             "z.writestr('a.txt',b'alpha'); z.writestr('b.txt',b'bravo'); z.close()",
             dir.filePath("valid.zip")});
        QVERIFY(python.waitForFinished(5000));
        QCOMPARE(python.exitCode(), 0);
        const auto original = read(dir.filePath("valid.zip"));
        QVERIFY(!original.isEmpty());
        auto damaged = original;
        const int data = damaged.indexOf("alpha");
        QVERIFY(data >= 0);
        damaged[data] = 'X';
        put(dir.filePath("bad.zip"), damaged);
        auto result = Archives::extract(nativePath(dir.filePath("bad.zip")),
                                        nativePath(dir.filePath("strict")), {}, control());
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(!QDir(dir.filePath("strict")).exists());
        set("zip_keepdamaged", true);
        result = Archives::extract(nativePath(dir.filePath("bad.zip")), nativePath(dir.filePath("damaged")),
                                   {}, control());
        QVERIFY2(QDir(dir.filePath("damaged")).exists(), qPrintable(result.items[0].error));
        QVERIFY(!result.items[0].error.isEmpty());
        QCOMPARE(read(dir.filePath("damaged/a.txt")), QByteArray("Xlpha"));
        set("zip_keepdamaged", false);
        set("zip_openpartial", true);
        const auto central = original.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central > 0);
        put(dir.filePath("partial.zip"), original.left(central - 2));
        const auto list = Archives::list(nativePath(dir.filePath("partial.zip")), control());
        QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        QCOMPARE(list.entries.size(), size_t(1));
        QVERIFY(!list.warning.isEmpty());
        result = Archives::extract(nativePath(dir.filePath("partial.zip")),
                                   nativePath(dir.filePath("recovered")), {"a.txt"}, control());
        QVERIFY2(QFile::exists(dir.filePath("recovered/a.txt")), qPrintable(result.items[0].error));
        QCOMPARE(read(dir.filePath("recovered/a.txt")), QByteArray("alpha"));
        QVERIFY(!QFile::exists(dir.filePath("recovered/b.txt")));
    }
    void sevenZipEncryptionAndTemporaryNamingPolicies() {
        if (QStandardPaths::findExecutable("7z").isEmpty())
            QSKIP("7z is required");
        QTemporaryDir dir;
        const auto source = dir.filePath("long source filename.txt");
        put(source, QByteArray(20000, 'x'));
        set("z7_dict", "1m");
        set("z7_threads", "1");
        auto args = FinalArchives::sevenArguments(false, true);
        QVERIFY(args.contains("-p"));
        QVERIFY(!args.join(' ').contains("fixture-secret"));
        auto result = FinalArchives::pack({nativePath(source)}, nativePath(dir.filePath("secret.7z")), false,
                                          "fixture-secret", control());
        QVERIFY2(!result.items.empty() && result.items[0].error.isEmpty(),
                 qPrintable(result.items.empty() ? QString("No result") : result.items[0].error));
        QProcess test;
        test.start("7z", {"t", "-bd", "-pfixture-secret", dir.filePath("secret.7z")});
        QVERIFY(test.waitForFinished(30000));
        QVERIFY2(test.exitCode() == 0, test.readAllStandardError().constData());
        QDir().mkdir(dir.filePath("scratch"));
        QSettings().setValue("final/zipTemp", 1);
        set("internalzip_tempdir", dir.filePath("scratch"));
        set("zip_83name", true);
        set("internalzip_unicodemethod", "ASCII");
        result = Archives::create({nativePath(source)}, nativePath(dir.filePath("short.zip")),
                                  Archives::Format::Zip, control());
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        const auto list = Archives::list(nativePath(dir.filePath("short.zip")), control());
        QVERIFY(list.error.isEmpty());
        QVERIFY(QString::fromStdString(list.entries[0].name).contains('~'));
        QVERIFY(QDir(dir.filePath("scratch"))
                    .entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden)
                    .isEmpty());
    }
    void sandboxedExternalExtraction() {
        if (QStandardPaths::findExecutable("bwrap").isEmpty() ||
            QStandardPaths::findExecutable("7z").isEmpty())
            QSKIP("bwrap and 7z required");
        QTemporaryDir dir;
        put(dir.filePath("input.txt"), "safe data");
        auto created =
            Archives::create({nativePath(dir.filePath("input.txt"))}, nativePath(dir.filePath("test.zip")),
                             Archives::Format::Zip, control());
        QVERIFY(created.items[0].error.isEmpty());
        set("zip_useinternal", false);
        const auto list = Archives::list(nativePath(dir.filePath("test.zip")), control());
        if (list.error.contains("namespace") || list.error.contains("Operation not permitted"))
            QSKIP("Kernel namespace policy prevents bubblewrap");
        QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        QCOMPARE(list.entries.size(), size_t(1));
        const auto result = Archives::extract(nativePath(dir.filePath("test.zip")),
                                              nativePath(dir.filePath("out")), {}, control());
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QCOMPARE(read(dir.filePath("out/input.txt")), QByteArray("safe data"));
    }
    void modeZRoundTripAndLanOptOut() {
        QTemporaryDir root, local;
        put(root.filePath("remote.txt"), QByteArray(30000, 'z'));
        QProcess server;
        QString address;
        fixture(server, root.path(), address);
        set("FtpModeZ", true);
        RemoteTools::Credentials credentials{"user", "secret"};
        auto list = RemoteTools::list(QUrl(address), credentials, false, control());
        QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        QVERIFY(list.bytes.contains("remote.txt"));
        RemoteTools::download(QUrl(address + "remote.txt"), credentials,
                              nativePath(local.filePath("download.txt")), control());
        QCOMPARE(read(local.filePath("download.txt")), read(root.filePath("remote.txt")));
        put(local.filePath("upload.txt"), "compressed upload");
        RemoteTools::upload(QUrl(address + "uploaded.txt"), credentials,
                            nativePath(local.filePath("upload.txt")), control());
        QCOMPARE(read(root.filePath("uploaded.txt")), QByteArray("compressed upload"));
        server.waitForReadyRead(1000);
        QVERIFY(server.readAllStandardOutput().contains("MODE Z"));
        set("FtpModeZnotonLAN", true);
        list = RemoteTools::list(QUrl(address), credentials, false, control());
        QVERIFY(list.error.isEmpty());
        server.waitForReadyRead(200);
        QVERIFY(!server.readAllStandardOutput().contains("MODE Z"));
        server.kill();
        server.waitForFinished();
    }
    void automaticReuploadRequiresStableEditAndStopsOnRequest() {
        QTemporaryDir root, local;
        put(root.filePath("edit.txt"), "before");
        QProcess server;
        QString address;
        fixture(server, root.path(), address);
        set("IdAutoReUpload", true);
        QSettings().setValue("ftp/downloadTarget", 1);
        RemoteDialog browser(nativePath(local.path()));
        browser.show();
        browser.connectTo(QUrl(address), {"user", "secret"});
        auto *files = browser.findChild<QListWidget *>();
        QTRY_COMPARE_WITH_TIMEOUT(files->count(), 1, 5000);
        files->setCurrentRow(0);
        for (auto *b : browser.findChildren<QPushButton *>())
            if (b->text() == "Download")
                b->click();
        QTRY_VERIFY_WITH_TIMEOUT(!browser.property("trackedDownloads").toMap().isEmpty(), 5000);
        put(local.filePath("edit.txt"), "after edit");
        QTRY_COMPARE_WITH_TIMEOUT(read(root.filePath("edit.txt")), QByteArray("after edit"), 8000);
        for (auto *b : browser.findChildren<QPushButton *>())
            if (b->text() == "Stop")
                b->click();
        put(local.filePath("edit.txt"), "not uploaded");
        QTest::qWait(3200);
        QCOMPARE(read(root.filePath("edit.txt")), QByteArray("after edit"));
        browser.disconnectServer();
        server.kill();
        server.waitForFinished();
    }
    void lockedWrappedTabsLanguageAndProfileLock() {
        QTemporaryDir dir;
        QDir().mkdir(dir.filePath("child"));
        MainWindow window({dir.path(), dir.path()});
        window.show();
        QTRY_VERIFY(!window.activePanel()->path().empty());
        auto *tabs = qobject_cast<QTabWidget *>(window.activePanel()->parentWidget()->parentWidget());
        QVERIFY(tabs);
        set("ChkTabsMultiLines", true);
        set("ChkTabsLockedAsterisk", true);
        set("ChkTabsRevertToRoot", true);
        auto *original = window.activePanel();
        original->setProperty("lockedRoot", QFile::encodeName(dir.path()));
        window.findChild<QAction *>("cm_OpenNewTab")->trigger();
        QTRY_COMPARE(tabs->count(), 2);
        QTRY_VERIFY(tabs->parentWidget()->findChild<QWidget *>("wrappedTabs")->isVisible());
        tabs->setCurrentWidget(original);
        original->navigate(nativePath(dir.filePath("child")));
        QTRY_COMPARE(original->path(), nativePath(dir.filePath("child")));
        tabs->setCurrentIndex(1);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(original->path(), nativePath(dir.path()));
        QVERIFY(tabs->tabText(0).contains('*'));
        QSettings().setValue("final/language", ":/languages/ro.json");
        FinalSettings::apply();
        QCOMPARE(FinalSettings::translated("&Files"), QString::fromUtf8("&Fișiere"));
        bool changed = false;
        for (auto *a : window.findChildren<QAction *>())
            if (a->text().contains(QString::fromUtf8("Fișiere")))
                changed = true;
        QVERIFY(changed);
        QSettings().setValue("final/language", QString());
        FinalSettings::apply();
        const auto path = QFileInfo(QSettings().fileName()).absolutePath();
        QDir().mkpath(path);
        QLockFile external(path + "/twin-commander.lock");
        QVERIFY(external.tryLock(0));
        set("IdConf_oneinstanceonly", true);
        QVERIFY(!FinalSettings::instanceLock());
        external.unlock();
        QVERIFY(FinalSettings::instanceLock());
        set("IdConf_oneinstanceonly", false);
        QVERIFY(FinalSettings::instanceLock());
    }
    void frequencyStorageAndNativeThumbnailProvider() {
        QTemporaryDir dir;
        set("DirUsageLocationCombo", "JSON data file");
        const auto key = QString::fromLatin1(QFile::encodeName(dir.path()).toBase64());
        qApp->setProperty("frequentUsage", QVariantMap{{key, 7}});
        Preferences::saveFrequent();
        QCOMPARE(Preferences::frequent().value(key).toInt(), 7);
        set("DirUsageLocationCombo", "Session only");
        QVERIFY(Preferences::frequent().isEmpty());
        Preferences::saveFrequent();
        QCOMPARE(Preferences::frequent().value(key).toInt(), 7);
        Preferences::clearFrequent();
        QVERIFY(Preferences::frequent().isEmpty());
        QImage image(20, 10, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(dir.filePath("image.png")));
        set("ThumbsFromListerPluginsChk", true);
        set("ThumbsFromListerPluginsEdt", "*.png");
        QSettings().setValue("nativePlugins", QVariantList{QVariantMap{{"kind", "thumbnail"},
                                                                       {"name", "native test"},
                                                                       {"mask", "*.png"},
                                                                       {"program", "/bin/cat"},
                                                                       {"arguments", "{path}"}}});
        const auto preview =
            FinalSettings::externalThumbnail(nativePath(dir.filePath("image.png")), QSize(10, 10));
        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), QSize(10, 5));
    }
};
QTEST_MAIN(FinalControlsTests)
#include "final_controls_tests.moc"
