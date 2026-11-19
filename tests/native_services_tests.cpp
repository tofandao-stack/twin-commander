#include "filesystem/pathencoding.h"
#include "archives/archives.h"
#include "operations/batchrename.h"
#include "ui/panels/directorymodel.h"
#include "filesystem/filetransforms.h"
#include "platform/nativeextensions.h"
#include "network/remoteservices.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <fstream>
#include <future>

namespace fs = std::filesystem;
class NativeServicesTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static auto control() { return std::make_shared<LocalFiles::Control>(); }
    static void put(const fs::path &path, const QByteArray &bytes) {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), bytes.size());
    }
    static QByteArray get(const fs::path &path) { return FileExports::readSmall(path, 64 * 1024 * 1024); }
    static bool success(const LocalFiles::Result &result) {
        return !result.cancelled && !result.items.empty() &&
               std::all_of(result.items.begin(), result.items.end(),
                           [](const auto &item) { return item.error.isEmpty(); });
    }
  private slots:
    void initTestCase() {
        qputenv("NO_PROXY", "127.0.0.1,localhost,::1");
        qputenv("no_proxy", "127.0.0.1,localhost,::1");
        QCoreApplication::setOrganizationName("TwinCommanderRemainingTests");
        QCoreApplication::setApplicationName("NativeServicesTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
    }
    void init() { QSettings().clear(); }
    void splitCombineValidatesPiecesAndRetainsExistingOutput() {
        QTemporaryDir temp;
        const auto root = nativePath(temp.path());
        QByteArray bytes;
        for (int i = 0; i < 140003; ++i)
            bytes += char(i % 256);
        put(root / "source", bytes);
        FileTransforms::split(root / "source", root / "parts", 16384, control());
        QVERIFY(fs::exists(root / "parts/parts.tcjoin"));
        FileTransforms::combine({root / "parts/parts.tcjoin"}, root / "joined", control());
        QCOMPARE(get(root / "joined"), bytes);
        QVERIFY_EXCEPTION_THROWN(
            FileTransforms::combine({root / "parts/parts.tcjoin"}, root / "joined", control()),
            NativeFs::Error);
        QCOMPARE(get(root / "joined"), bytes);
        put(root / "parts/part.000002", "tampered");
        QVERIFY_EXCEPTION_THROWN(
            FileTransforms::combine({root / "parts/parts.tcjoin"}, root / "bad", control()), NativeFs::Error);
        QVERIFY(!fs::exists(root / "bad"));
        auto cancelled = control();
        cancelled->cancelled = true;
        QVERIFY_EXCEPTION_THROWN(FileTransforms::split(root / "source", root / "cancelled", 1024, cancelled),
                                 LocalFiles::Cancelled);
        QVERIFY(!fs::exists(root / "cancelled"));
        auto manifest = QJsonDocument::fromJson(get(root / "parts/parts.tcjoin")).object();
        manifest["parts"] = QJsonArray{QJsonObject{{"name", "../source"}}};
        put(root / "unsafe.tcjoin", QJsonDocument(manifest).toJson());
        QVERIFY_EXCEPTION_THROWN(FileTransforms::combine({root / "unsafe.tcjoin"}, root / "unsafe", control()),
                                 NativeFs::Error);
    }
    void encodersRoundTripAndRejectMalformedInput() {
        QByteArray bytes;
        for (int i = 0; i < 1025; ++i)
            bytes += char(i % 256);
        for (const auto &format : {QString("MIME"), QString("UUE"), QString("XXE")}) {
            const auto encoded = FileTransforms::encode(bytes, format, "file.bin");
            QCOMPARE(FileTransforms::decode(encoded, format), bytes);
            QCOMPARE(FileTransforms::decode(FileTransforms::encode({}, format, "empty"), format), QByteArray());
        }
        QFile hqx(QFINDTESTDATA("fixtures/sample.hqx"));
        QVERIFY(hqx.open(QIODevice::ReadOnly));
        QCOMPARE(FileTransforms::decode(hqx.readAll(), "BinHex"), QByteArray("Hello BinHex!\n"));
        QVERIFY_EXCEPTION_THROWN(FileTransforms::decode("$invalid!", "MIME"), NativeFs::Error);
        QVERIFY_EXCEPTION_THROWN(FileTransforms::decode("begin 644 x\nMshort\nend\n", "UUE"), NativeFs::Error);
        QVERIFY_EXCEPTION_THROWN(FileTransforms::decode(":broken:", "BinHex"), NativeFs::Error);
    }
    void renameUndoRefusesChangedTargets() {
        QTemporaryDir temp;
        const auto root = nativePath(temp.path());
        put(root / "a.txt", "original");
        BatchRename::Rule rule;
        rule.pattern = "new{ext}";
        auto inverse = std::make_shared<BatchRename::Plan>();
        const auto plan = BatchRename::preview({root / "a.txt"}, rule);
        QVERIFY(success(BatchRename::apply(plan, control(), {}, inverse)));
        QCOMPARE(inverse->items.size(), size_t(1));
        QVERIFY(success(BatchRename::apply(*inverse, control())));
        QCOMPARE(get(root / "a.txt"), QByteArray("original"));
        const auto second = BatchRename::preview({root / "a.txt"}, rule);
        QVERIFY(success(BatchRename::apply(second, control(), {}, inverse)));
        put(root / "new.txt", "external edit");
        QVERIFY(!success(BatchRename::apply(*inverse, control())));
        QCOMPARE(get(root / "new.txt"), QByteArray("external edit"));
        QVERIFY(!fs::exists(root / "a.txt"));
    }
    void remainingRenameCaseAndCounterOptions() {
        QTemporaryDir temp;
        const auto root = nativePath(temp.path());
        put(root / "hELLO wORLD.TXT", "a");
        put(root / "other.TXT", "b");
        BatchRename::Rule rule;
        rule.commanderMasks = true;
        rule.pattern = "[N]";
        rule.letterCase = BatchRename::LetterCase::Title;
        auto plan = BatchRename::preview({root / "hELLO wORLD.TXT"}, rule);
        QVERIFY(plan.errors.isEmpty());
        QCOMPARE(plan.items[0].target.filename(), fs::path("Hello World.Txt"));
        rule.letterCase = BatchRename::LetterCase::Capitalize;
        QCOMPARE(BatchRename::preview({root / "hELLO wORLD.TXT"}, rule).items[0].target.filename(),
                 fs::path("Hello world.txt"));
        rule.letterCase = BatchRename::LetterCase::Preserve;
        rule.autoWidth = true;
        rule.pattern = "[C]_[N]";
        rule.start = 9;
        const auto counted = BatchRename::preview({root / "hELLO wORLD.TXT", root / "other.TXT"}, rule);
        QVERIFY(counted.errors.isEmpty());
        QVERIFY(displayPath(counted.items[0].target.filename()).startsWith("09_"));
        rule.autoWidth = false;
        rule.pattern = "[N]";
        rule.find = "txt";
        rule.replacement = "data";
        rule.caseSensitiveFind = false;
        rule.replaceExtension = true;
        const auto extension = BatchRename::preview({root / "other.TXT"}, rule);
        QVERIFY(extension.errors.isEmpty());
        QCOMPARE(extension.items[0].target.filename(), fs::path("other.data"));
    }
    void nativeFieldsCommentsAndExtensions() {
        QTemporaryDir temp;
        const auto root = nativePath(temp.path());
        put(root / "file", "abc");
        try {
            FileTransforms::setComment(root / "file", "hello");
        } catch (const std::exception &) {
            QSKIP("Filesystem does not support user extended attributes");
        }
        QCOMPARE(FileTransforms::comment(root / "file"), QString("hello"));
        QCOMPARE(NativeExtensions::field(root / "file", "size"), QString("3"));
        put(root / "field.sh", "printf 'field-value'\n");
        QSettings().setValue(
            "nativePlugins",
            QVariantList{QVariantMap{{"name", "test"},
                                     {"kind", "content"},
                                     {"mask", "*"},
                                     {"program", "/bin/sh"},
                                     {"arguments", QString::fromStdString((root / "field.sh").native())}}});
        auto providers = QSettings().value("nativePlugins").toList();
        auto other = providers[0].toMap();
        other["mask"] = "*.nomatch";
        providers.prepend(other);
        QSettings().setValue("nativePlugins", providers);
        QCOMPARE(NativeExtensions::field(root / "file", "test"), QString("field-value"));
        BatchRename::Rule rule;
        rule.commanderMasks = true;
        rule.pattern = "[=test]";
        rule.extensionPattern = "";
        const auto plan = BatchRename::preview({root / "file"}, rule);
        QVERIFY(plan.errors.isEmpty());
        QCOMPARE(plan.items[0].target.filename(), fs::path("field-value"));
        QVERIFY_EXCEPTION_THROWN(NativeExtensions::field(root / "file", "unknown"), std::runtime_error);
    }
    void nativeFilesystemAndExternalPackerAreUsable() {
        QTemporaryDir temp;
        const auto root = nativePath(temp.path());
        QVERIFY(QDir(temp.path()).mkdir("mounted space"));
        put(root / "mount.sh", ("printf '%s\n' '" + temp.filePath("mounted space") + "'\n").toUtf8());
        QSettings().setValue("nativePlugins",
                             QVariantList{QVariantMap{{"name", "test mount"},
                                                      {"kind", "filesystem"},
                                                      {"mask", "test://*"},
                                                      {"program", "/bin/sh"},
                                                      {"arguments", temp.filePath("mount.sh")}}});
        QCOMPARE(NativeExtensions::mountUrl("test://server"), temp.filePath("mounted space"));
        QVERIFY(NativeExtensions::mountUrl("other://server").isEmpty());
        const auto tar = QStandardPaths::findExecutable("tar");
        if (tar.isEmpty())
            QSKIP("tar required for external packer test");
        put(root / "source.txt", "external archive");
        const auto result = FileTransforms::packExternal({root / "source.txt"}, root / "new.tar", tar,
                                                        {"-cf", "{archive}"}, control());
        QVERIFY2(success(result),
                 qPrintable(result.items.empty() ? QString("empty result") : result.items.back().error));
        const auto archive = Archives::list(root / "new.tar", control());
        QVERIFY2(archive.error.isEmpty(), qPrintable(archive.error));
        QCOMPARE(archive.entries.size(), size_t(1));
        QVERIFY(!success(FileTransforms::packExternal({root / "source.txt"}, root / "new.tar", tar,
                                                     {"-cf", "{archive}"}, control())));
        fs::create_symlink(root / "source.txt", root / "link");
        QVERIFY(!success(FileTransforms::packExternal({root / "link"}, root / "link.tar", tar,
                                                     {"-cf", "{archive}"}, control())));
        QVERIFY(!fs::exists(root / "link.tar"));
    }
    void shareServerBlocksTraversalAndLinks() {
        QTemporaryDir temp, outside;
        const auto root = nativePath(temp.path());
        put(root / "file.txt", "shared");
        put(nativePath(outside.filePath("secret")), "private");
        fs::create_symlink(nativePath(outside.filePath("secret")), root / "link");
        RemoteServices::DirectoryShareServer server(root);
        QVERIFY(server.start());
        QNetworkAccessManager manager;
        const auto fetch = [&](QString leaf) {
            auto url = server.address();
            url.setPath(url.path() + leaf);
            auto *reply = manager.get(QNetworkRequest(url));
            QSignalSpy done(reply, &QNetworkReply::finished);
            if (!done.wait(5000)) {
                reply->abort();
                return QByteArray("TIMEOUT");
            }
            const auto result =
                QByteArray::number(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) + ":" +
                reply->readAll();
            reply->deleteLater();
            return result;
        };
        QCOMPARE(fetch("file.txt"), QByteArray("200:shared"));
        QVERIFY(fetch("link").startsWith("403:"));
        QVERIFY(fetch("../secret").startsWith("403:"));
        QVERIFY(fetch("").startsWith("200:"));
        const auto opaque = root / std::filesystem::path(std::string("raw-") + char(0xff));
        put(opaque, "opaque bytes");
        auto *raw = manager.get(QNetworkRequest(QUrl::fromEncoded(server.address().toEncoded() + "raw-%FF")));
        QSignalSpy rawDone(raw, &QNetworkReply::finished);
        QVERIFY(rawDone.wait(5000));
        QCOMPARE(raw->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(raw->readAll(), QByteArray("opaque bytes"));
        raw->deleteLater();
        QVERIFY(fs::remove(opaque));
        server.close();
    }
    void ftpsChecksCertificatesAndDoesNotLogPasswords() {
        if (QStandardPaths::findExecutable("openssl").isEmpty() ||
            QStandardPaths::findExecutable("curl").isEmpty())
            QSKIP("openssl and curl required");
        QTemporaryDir temp, certificates, downloads;
        const auto root = nativePath(temp.path());
        put(root / "secure.txt", "encrypted payload");
        const auto certificate = certificates.filePath("cert.pem"), key = certificates.filePath("key.pem");
        QProcess openssl;
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj",
                                  "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1", "-keyout", key,
                                  "-out", certificate});
        QVERIFY(openssl.waitForFinished(15000));
        QCOMPARE(openssl.exitCode(), 0);
        QProcess server;
        server.start("python3", {QFINDTESTDATA("fixtures/ftp_server.py"), temp.path(), certificate, key});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead(5000));
        const auto port = server.readLine().trimmed().toInt();
        QVERIFY(port > 0);
        const QUrl url(QString("ftps://127.0.0.1:%1/").arg(port));
        const RemoteServices::Credentials credentials{"test", "secret"};
        QVERIFY(!RemoteServices::list(url, credentials, false, control()).error.isEmpty());
        struct Restore {
            QByteArray previous = qgetenv("CURL_CA_BUNDLE");
            bool had = qEnvironmentVariableIsSet("CURL_CA_BUNDLE");
            ~Restore() {
                if (had)
                    qputenv("CURL_CA_BUNDLE", previous);
                else
                    qunsetenv("CURL_CA_BUNDLE");
            }
        } restore;
        qputenv("CURL_CA_BUNDLE", QFile::encodeName(certificate));
        QSettings().setValue("logging/ftpEnabled", true);
        QSettings().setValue("logging/ftpFile", downloads.filePath("ftp.log"));
        const auto listed = RemoteServices::list(url, credentials, false, control());
        QVERIFY2(listed.error.isEmpty(), qPrintable(listed.error));
        QVERIFY(listed.bytes.contains("secure.txt"));
        auto file = url;
        file.setPath("/secure.txt");
        RemoteServices::download(file, credentials, nativePath(downloads.filePath("secure.txt")), control());
        QCOMPARE(get(nativePath(downloads.filePath("secure.txt"))), QByteArray("encrypted payload"));
        QVERIFY(!get(nativePath(downloads.filePath("ftp.log"))).contains("secret"));
        server.terminate();
        server.waitForFinished();
    }
    void ftpListDownloadUploadAndNoClobber() {
        if (QStandardPaths::findExecutable("python3").isEmpty() ||
            QStandardPaths::findExecutable("curl").isEmpty())
            QSKIP("python3 and curl are required");
        QTemporaryDir temp, downloads;
        const auto root = nativePath(temp.path());
        put(root / "hello.txt", "ftp payload");
        put(root / ".hidden", "hidden");
        QProcess server;
        server.start("python3", {QFINDTESTDATA("fixtures/ftp_server.py"), temp.path()});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead(5000));
        const int port = server.readLine().trimmed().toInt();
        QVERIFY(port > 0);
        QUrl base(QString("ftp://127.0.0.1:%1/").arg(port));
        const RemoteServices::Credentials credentials{"test", "secret"};
        const auto names = RemoteServices::list(base, credentials, false, control());
        QVERIFY2(names.error.isEmpty(), qPrintable(names.error));
        QVERIFY(names.bytes.contains("hello.txt"));
        QVERIFY(!names.bytes.contains(".hidden"));
        QVERIFY(RemoteServices::list(base, credentials, true, control()).bytes.contains(".hidden"));
        auto target = base;
        target.setPath("/hello.txt");
        const auto output = nativePath(downloads.filePath("new.txt"));
        QVERIFY(RemoteServices::download(target, credentials, output, control()).contains("Downloaded"));
        QCOMPARE(get(output), QByteArray("ftp payload"));
        QVERIFY_EXCEPTION_THROWN(RemoteServices::download(target, credentials, output, control()),
                                 NativeFs::Error);
        target.setPath("/uploaded.txt");
        QVERIFY(RemoteServices::upload(target, credentials, output, control()).contains("Uploaded"));
        QCOMPARE(get(root / "uploaded.txt"), QByteArray("ftp payload"));
        server.terminate();
        server.waitForFinished();
    }
};
QTEST_GUILESS_MAIN(NativeServicesTests)
#include "native_services_tests.moc"
