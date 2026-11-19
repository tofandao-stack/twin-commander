#include "filesystem/pathencoding.h"
#include "operations/fileoperations.h"
#include "search/filesearch.h"
#include "ui/panels/directorymodel.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtTest>
#include <fstream>
#include <future>
#include <thread>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <csignal>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace LocalFiles;
class OperationTests : public QObject {
    Q_OBJECT
    static void put(const fs::path &path, const std::string &bytes = "fixture") { std::ofstream(path, std::ios::binary) << bytes; }
    static std::string get(const fs::path &path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
    static Result run(Operation kind, const std::vector<fs::path> &sources, const fs::path &destination = {}, Conflict conflict = Conflict::Fail)
    {
        return execute({kind, sources, destination, conflict, {}}, std::make_shared<Control>());
    }
private slots:
    void recursiveCopyPreservesBytesLinksAndPermissions()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directories(root / "source/nested");
        fs::create_directory(root / "destination");
        put(root / "source/nested/data", std::string("raw\0bytes", 9));
        put(root / "source" / std::string("name-\xff"), "native");
        fs::permissions(root / "source/nested/data", fs::perms::owner_read | fs::perms::owner_write);
        fs::create_symlink("../missing", root / "source/nested/dangling");
        fs::create_directory_symlink(root / "source", root / "source/cycle");
        const auto result = run(Operation::Copy, {root / "source"}, root / "destination");
        QCOMPARE(result.items.size(), size_t(1));
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QCOMPARE(get(root / "destination/source/nested/data"), std::string("raw\0bytes", 9));
        QCOMPARE(get(root / "destination/source" / std::string("name-\xff")), std::string("native"));
        QVERIFY(fs::read_symlink(root / "destination/source/nested/dangling") == fs::path("../missing"));
        QVERIFY(fs::is_symlink(root / "destination/source/cycle"));
        fs::remove(root / "destination/source/cycle");
        fs::remove(root / "source/cycle");
        QCOMPARE(fs::status(root / "destination/source/nested/data").permissions() & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);
        fs::remove_all(root);
    }
    void deepTreeCopyUsesBoundedStackAndDeletion()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::path nested;
        for (int i = 0; i < 64; ++i) nested /= "d";
        fs::create_directories(root / "source" / nested);
        fs::create_directory(root / "target");
        put(root / "source" / nested / "leaf", "deep tree");
        auto result = run(Operation::Copy, {root / "source"}, root / "target");
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QCOMPARE(get(root / "target/source" / nested / "leaf"), std::string("deep tree"));
        result = run(Operation::Delete, {root / "target/source"});
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QVERIFY(fs::is_empty(root / "target"));
    }
    void conflictsSkipReplaceAndFail()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directories(root / "destination");
        put(root / "file", "new");
        put(root / "destination/file", "old");
        auto result = run(Operation::Copy, {root / "file"}, root / "destination");
        QVERIFY(!result.items[0].error.isEmpty());
        QCOMPARE(get(root / "destination/file"), std::string("old"));
        result = run(Operation::Copy, {root / "file"}, root / "destination", Conflict::Skip);
        QVERIFY(result.items[0].skipped);
        QCOMPARE(get(root / "destination/file"), std::string("old"));
        result = run(Operation::Copy, {root / "file"}, root / "destination", Conflict::ReplaceFiles);
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QCOMPARE(get(root / "destination/file"), std::string("new"));
        fs::remove(root / "destination/file");
        fs::create_symlink(root / "file", root / "destination/file");
        result = run(Operation::Copy, {root / "file"}, root / "destination", Conflict::ReplaceFiles);
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(fs::is_symlink(root / "destination/file"));
    }
    void recursiveSelfCopyAndHardlinkCopyAreRejected()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directories(root / "source/nested");
        auto result = run(Operation::Copy, {root / "source"}, root / "source/nested");
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(!fs::exists(root / "source/nested/source"));
        put(root / "file");
        fs::create_directory(root / "target");
        fs::create_hard_link(root / "file", root / "target/file");
        result = run(Operation::Copy, {root / "file"}, root / "target", Conflict::ReplaceFiles);
        QVERIFY(!result.items[0].error.isEmpty());
    }
    void unsupportedEntryCleansStagedTree()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directories(root / "source");
        fs::create_directory(root / "target");
        put(root / "source/regular");
        QCOMPARE(::mkfifo((root / "source/pipe").c_str(), 0600), 0);
        const auto result = run(Operation::Copy, {root / "source"}, root / "target");
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(fs::is_empty(root / "target"));
        QVERIFY(fs::exists(root / "source/regular"));
    }
    void cancellationAndDestinationRaceKeepSource()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directory(root / "target");
        // A sparse file keeps fixture setup cheap while the engine exercises real chunks.
        put(root / "large");
        fs::resize_file(root / "large", 128 * 1024 * 1024);
        auto control = std::make_shared<Control>();
        control->cancelled = true;
        auto result = execute({Operation::Copy, {root / "large"}, root / "target", Conflict::Fail, {}}, control);
        QVERIFY(result.cancelled);
        QVERIFY(fs::is_empty(root / "target"));
        control = std::make_shared<Control>();
        auto job = std::async(std::launch::async, [=] { return execute({Operation::Copy, {root / "large"}, root / "target", Conflict::Fail, {}}, control); });
        while (control->bytes.load() == 0 && job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::yield();
        control->cancelled = true;
        result = job.get();
        QVERIFY(result.cancelled);
        QVERIFY(fs::is_empty(root / "target"));
        QVERIFY(fs::exists(root / "large"));
        control = std::make_shared<Control>();
        job = std::async(std::launch::async, [=] { return execute({Operation::Copy, {root / "large"}, root / "target", Conflict::Fail, {}}, control); });
        while (control->bytes.load() == 0 && job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::yield();
        put(root / "target/large", "racing destination");
        result = job.get();
        QVERIFY(!result.items[0].error.isEmpty());
        QCOMPARE(get(root / "target/large"), std::string("racing destination"));
        QCOMPARE(std::distance(fs::directory_iterator(root / "target"), fs::directory_iterator()), 1);
    }
    void sourceChangesDuringCopyRefuseCommit()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directory(root / "target");
        put(root / "large");
        fs::resize_file(root / "large", 128 * 1024 * 1024);
        auto control = std::make_shared<Control>();
        auto job = std::async(std::launch::async, [=] { return execute({Operation::Copy, {root / "large"}, root / "target", Conflict::Fail, {}}, control); });
        while (control->bytes.load() == 0 && job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::yield();
        put(root / "large", "changed");
        const auto result = job.get();
        QVERIFY(!result.items[0].error.isEmpty());
        QVERIFY(fs::is_empty(root / "target"));
        QCOMPARE(get(root / "large"), std::string("changed"));
    }
    void mkdirRenameMoveAndDeleteStayWithinSelectedTree()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        auto result = run(Operation::MakeDirectory, {}, root / "new");
        QVERIFY(result.items[0].error.isEmpty());
        result = run(Operation::MakeDirectory, {}, root / "new");
        QVERIFY(!result.items[0].error.isEmpty());
        result = run(Operation::MakeFile, {}, root / "new/file");
        QVERIFY(result.items[0].error.isEmpty());
        QVERIFY(fs::exists(root / "new/file"));
        result = run(Operation::MakeFile, {}, root / "new/file");
        QVERIFY(!result.items[0].error.isEmpty());
        put(root / "new/file");
        result = run(Operation::Rename, {root / "new/file"}, root / "new/renamed");
        QVERIFY(result.items[0].error.isEmpty());
        QVERIFY(!fs::exists(root / "new/file"));
        fs::create_directory(root / "other");
        result = run(Operation::Move, {root / "new/renamed"}, root / "other");
        QVERIFY(result.items[0].error.isEmpty());
        put(root / "new/renamed", "keep");
        result = run(Operation::Move, {root / "other/renamed"}, root / "new");
        QVERIFY(!result.items[0].error.isEmpty());
        QCOMPARE(get(root / "new/renamed"), std::string("keep"));
        fs::create_directory_symlink(root / "other", root / "new/link");
        result = run(Operation::Delete, {root / "new"});
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QVERIFY(!fs::exists(root / "new"));
        QVERIFY(fs::exists(root / "other/renamed"));
        result = run(Operation::Delete, {fs::path("/")});
        QVERIFY(!result.items[0].error.isEmpty());
    }
    void crossFilesystemMoveRetainsSource()
    {
        QTemporaryDir source;
        QTemporaryDir destination("/dev/shm/twin-commander-tests-XXXXXX");
        if (!destination.isValid()) QSKIP("No writable second filesystem");
        struct stat a{}, b{};
        QVERIFY(::stat(source.path().toLocal8Bit().constData(), &a) == 0);
        QVERIFY(::stat(destination.path().toLocal8Bit().constData(), &b) == 0);
        if (a.st_dev == b.st_dev) QSKIP("Test requires different filesystems");
        const auto path = nativePath(source.filePath("keep"));
        put(path, "keep");
        const auto result = run(Operation::Move, {path}, nativePath(destination.path()));
        QVERIFY(!result.items[0].error.isEmpty());
        QCOMPARE(get(path), std::string("keep"));
        QVERIFY(fs::is_empty(nativePath(destination.path())));
    }
    void writeFailureRemovesTemporaryOutput()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directory(root / "target");
        put(root / "file", std::string(32768, 'x'));
        const auto child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            ::signal(SIGXFSZ, SIG_IGN);
            const rlimit limit{1024, 1024};
            if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(2);
            const auto result = run(Operation::Copy, {root / "file"}, root / "target");
            ::_exit(!result.items[0].error.isEmpty() && fs::is_empty(root / "target") && fs::file_size(root / "file") == 32768 ? 0 : 1);
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        QVERIFY(WIFEXITED(status));
        QCOMPARE(WEXITSTATUS(status), 0);
    }
    void trashMovesIntoIsolatedDataDirectory()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        const auto previous = qgetenv("XDG_DATA_HOME");
        QVERIFY(QDir().mkpath(directory.filePath("data")));
        qputenv("XDG_DATA_HOME", QFile::encodeName(directory.filePath("data")));
        put(root / "discard", "trash fixture");
        const auto result = run(Operation::Trash, {root / "discard"});
        if (previous.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", previous);
        QVERIFY2(result.items[0].error.isEmpty(), qPrintable(result.items[0].error));
        QVERIFY(!fs::exists(root / "discard"));
        QVERIFY(!result.items[0].destination.empty());
        QCOMPARE(get(result.items[0].destination), std::string("trash fixture"));
        QVERIFY2(result.items[0].destination.native().starts_with((root / "data").native()), qPrintable(displayPath(result.items[0].destination)));
    }
    void journalsPersistIntentAndOutcomes()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        put(root / "file");
        fs::create_directory(root / "target");
        auto result = execute({Operation::Copy, {root / "file", root / "missing"}, root / "target", Conflict::Fail, directory.filePath("jobs")}, std::make_shared<Control>());
        QCOMPARE(result.items.size(), size_t(2));
        QVERIFY(result.journalError.isEmpty());
        const auto logs = QDir(directory.filePath("jobs")).entryList({"*.json"}, QDir::Files);
        QCOMPARE(logs.size(), 1);
        QFile file(directory.filePath("jobs/" + logs.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(json.value("finished").toBool());
        QCOMPARE(json.value("items").toArray().size(), 2);
    }
    void atomicEditorSavePreservesNativeNamesAndRefusesChangedFiles()
    {
        QTemporaryDir directory;
        const auto path = nativePath(directory.path()) / std::string("raw-\xff");
        put(path, "original");
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write);
        QVERIFY(saveFile(path, "original", "edited").isEmpty());
        QCOMPARE(get(path), std::string("edited"));
        QVERIFY(!saveFile(path, "original", "stale edit").isEmpty());
        QCOMPARE(get(path), std::string("edited"));
        const auto link = nativePath(directory.filePath("link"));
        fs::create_symlink(path, link);
        QVERIFY(!saveFile(link, "edited", "followed link").isEmpty());
        QCOMPARE(get(path), std::string("edited"));
        QCOMPARE(fs::status(path).permissions() & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);
        fs::remove_all(nativePath(directory.path()));
    }
    void searchMasksContentLimitsAndLinks()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        fs::create_directories(root / "sub");
        put(root / "alpha.txt", "Needle");
        put(root / "sub/beta.TXT", "hay needle stack");
        put(root / ".hidden.txt", "needle");
        fs::create_directory_symlink(root, root / "sub/cycle");
        auto result = searchFiles({root, "*.txt", "NEEDLE", false, false, true, 5000}, std::make_shared<Control>());
        QCOMPARE(result.paths.size(), size_t(2));
        QVERIFY(!result.truncated);
        result = searchFiles({root, "*.txt", "Needle", true, false, true, 5000}, std::make_shared<Control>());
        QCOMPARE(result.paths.size(), size_t(1));
        result = searchFiles({root, "*.txt", {}, false, true, false, 5000}, std::make_shared<Control>());
        QCOMPARE(result.paths.size(), size_t(2));
        result = searchFiles({root, "*", {}, false, true, true, 1}, std::make_shared<Control>());
        QCOMPARE(result.paths.size(), size_t(1));
        QVERIFY(result.truncated);
        auto control = std::make_shared<Control>();
        control->cancelled = true;
        QVERIFY(searchFiles({root}, control).cancelled);
    }
};
QTEST_GUILESS_MAIN(OperationTests)
#include "operation_tests.moc"
