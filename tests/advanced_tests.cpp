#include "filesystem/pathencoding.h"
#include "archives/archives.h"
#include "operations/batchrename.h"
#include "filesystem/fileanalysis.h"
#include "ui/panels/directorymodel.h"
#include "search/filesearch.h"
#include "filesystem/fileexports.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <archive.h>
#include <archive_entry.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <fstream>
#include <future>

namespace fs = std::filesystem;
class AdvancedTests : public QObject {
    Q_OBJECT
    static std::shared_ptr<LocalFiles::Control> control() { return std::make_shared<LocalFiles::Control>(); }
    static void put(const fs::path &path, const std::string &bytes = "fixture") { std::ofstream(path, std::ios::binary) << bytes; }
    static std::string get(const fs::path &path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
    static bool success(const LocalFiles::Result &result) { return !result.cancelled && !result.items.empty() && std::all_of(result.items.begin(), result.items.end(), [](const auto &item) { return item.error.isEmpty(); }); }
    static void fixture(const fs::path &path, const std::vector<std::pair<std::string, std::string>> &entries, bool link = false)
    {
        auto *writer = archive_write_new();
        QVERIFY(writer);
        QCOMPARE(archive_write_set_format_pax_restricted(writer), ARCHIVE_OK);
        QCOMPARE(archive_write_open_filename(writer, path.c_str()), ARCHIVE_OK);
        for (const auto &[name, bytes] : entries) {
            auto *entry = archive_entry_new();
            archive_entry_set_pathname(entry, name.c_str()); archive_entry_set_perm(entry, 0644);
            archive_entry_set_filetype(entry, link ? AE_IFLNK : AE_IFREG);
            if (link) archive_entry_set_symlink(entry, "../../outside");
            archive_entry_set_size(entry, link ? 0 : bytes.size());
            QCOMPARE(archive_write_header(writer, entry), ARCHIVE_OK);
            if (!link) QCOMPARE(archive_write_data(writer, bytes.data(), bytes.size()), static_cast<la_ssize_t>(bytes.size()));
            archive_entry_free(entry);
        }
        QCOMPARE(archive_write_close(writer), ARCHIVE_OK); archive_write_free(writer);
    }
private slots:
    void selectionFilesPreserveBytesAndRejectEscapes()
    {
        QTemporaryDir directory;const auto root=nativePath(directory.path());const auto raw=fs::path(std::string("raw-")+char(0xff));
        const std::vector<fs::path> paths{root/"normal",root/raw};const auto decoded=FileExports::selectionPaths(FileExports::selectionData(paths),root);QVERIFY(decoded==paths);
        auto object=QJsonDocument::fromJson(FileExports::selectionData(paths)).object();object["namesBase64"]=QJsonArray{QString::fromLatin1(QByteArray("../outside").toBase64())};
        QVERIFY_EXCEPTION_THROWN(FileExports::selectionPaths(QJsonDocument(object).toJson(),root),NativeFs::Error);
        QVERIFY_EXCEPTION_THROWN(FileExports::selectionPaths("invalid",root),NativeFs::Error);
    }
    void exportsNeverReplaceExistingEntries()
    {
        QTemporaryDir directory;const auto root=nativePath(directory.path());FileExports::exportNew(root/"new","original");
        QVERIFY_EXCEPTION_THROWN(FileExports::exportNew(root/"new","replacement"),NativeFs::Error);QCOMPARE(get(root/"new"),std::string("original"));
        fs::create_symlink(root/"new",root/"link");QVERIFY_EXCEPTION_THROWN(FileExports::exportNew(root/"link","replacement"),NativeFs::Error);
        QVERIFY_EXCEPTION_THROWN(FileExports::readSmall(root/"link"),NativeFs::Error);QCOMPARE(fs::directory_iterator(root)!=fs::directory_iterator(),true);
        for(const auto &entry:fs::directory_iterator(root))QVERIFY(!entry.path().filename().native().starts_with(".tc-export-"));
    }
    void checksumManifestsDetectChangesAndRejectTraversal()
    {
        QTemporaryDir directory;const auto root=nativePath(directory.path());put(root/"file","abc");
        for(const auto algorithm:{QCryptographicHash::Md5,QCryptographicHash::Sha1,QCryptographicHash::Sha256}){
            const auto data=FileExports::checksumManifest({root/"file"},algorithm,control());put(root/"manifest",data.toStdString());
            QVERIFY(FileExports::verifyManifest(root/"manifest",control()).contains("1 verified; 0 failed"));put(root/"file","changed");QVERIFY(FileExports::verifyManifest(root/"manifest",control()).contains("0 verified; 1 failed"));put(root/"file","abc");
        }
        put(root/"manifest",std::string(64,'a')+"  ../outside\n");QVERIFY_EXCEPTION_THROWN(FileExports::verifyManifest(root/"manifest",control()),NativeFs::Error);
        fs::create_symlink(root/"file",root/"link");QVERIFY_EXCEPTION_THROWN(FileExports::checksumManifest({root/"link"},QCryptographicHash::Sha256,control()),NativeFs::Error);
        auto cancelled=control();cancelled->cancelled=true;QVERIFY_EXCEPTION_THROWN(FileExports::checksumManifest({root/"file"},QCryptographicHash::Sha256,cancelled),LocalFiles::Cancelled);
    }
    void recursiveFileListsDoNotTraverseLinks()
    {
        QTemporaryDir directory;const auto root=nativePath(directory.path());fs::create_directory(root/"folder");put(root/"folder/file");fs::create_directory_symlink(root/"folder",root/"alias");
        const auto list=FileExports::fileList({root/"folder",root/"alias"},true,control());QVERIFY(list.contains("folder/file"));QVERIFY(!list.contains("alias/file"));
        QVERIFY(!FileExports::fileList({root/"folder"},false,control()).contains("folder/file"));
    }
    void commanderRenameMasksUseRangesDatesAndUnicode()
    {
        QTemporaryDir directory;const auto root=nativePath(directory.path());const auto source=root/nativePath(QString::fromUtf8("😀abcd.txt"));put(source,"abc");
        BatchRename::Rule rule;rule.commanderMasks=true;rule.pattern="[N1-3]_[N-2-]_[S]_[YMD]_[C]";rule.extensionPattern="[E1-2]";
        const auto plan=BatchRename::preview({source},rule);QVERIFY2(plan.errors.isEmpty(),qPrintable(plan.errors.join("\n")));const auto date=QDateTime::fromSecsSinceEpoch(NativeFs::inspect(source).st_mtime).toString("yyyyMMdd");
        QCOMPARE(displayPath(plan.items[0].target.filename()),QString::fromUtf8("😀ab_cd_3_")+date+"_001.tx");
        rule.pattern="[N2,2]";rule.extensionPattern="";QCOMPARE(displayPath(BatchRename::preview({source},rule).items[0].target.filename()),QString("ab"));
        rule.pattern="[plugin]";QVERIFY(!BatchRename::preview({source},rule).errors.isEmpty());
    }

    void archiveRoundTrips_data()
    {
        QTest::addColumn<int>("format");
        QTest::newRow("zip") << 0; QTest::newRow("tar-gzip") << 1; QTest::newRow("tar") << 2;
    }
    void archiveRoundTrips()
    {
        QFETCH(int, format);
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fs::create_directories(root / "source/nested/empty"); put(root / "source/nested/data", std::string("data\0bytes", 10));
        auto packed = Archives::create({root / "source"}, root / "packed", static_cast<Archives::Format>(format), control());
        QVERIFY2(success(packed), qPrintable(packed.items.front().error));
        auto listed = Archives::list(root / "packed", control());
        QVERIFY2(listed.error.isEmpty(), qPrintable(listed.error)); QCOMPARE(listed.entries.size(), size_t(4));
        auto extracted = Archives::extract(root / "packed", root / "output", {}, control());
        QVERIFY2(success(extracted), qPrintable(extracted.items.front().error));
        QCOMPARE(get(root / "output/source/nested/data"), std::string("data\0bytes", 10));
        QVERIFY(fs::is_directory(root / "output/source/nested/empty"));
        QVERIFY(!success(Archives::extract(root / "packed", root / "output", {}, control())));
        QCOMPARE(get(root / "output/source/nested/data"), std::string("data\0bytes", 10));
        QVERIFY(!success(Archives::create({root / "source"}, root / "packed", static_cast<Archives::Format>(format), control())));
    }
    void archiveSelectedExtractionAndQuota()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fixture(root / "input.tar", {{"folder/a", "one"}, {"folder/b", "two"}, {"other", "skip"}});
        QVERIFY(success(Archives::extract(root / "input.tar", root / "only-a", {"folder/a"}, control())));
        QCOMPARE(get(root / "only-a/folder/a"), std::string("one")); QVERIFY(!fs::exists(root / "only-a/folder/b"));
        QVERIFY(success(Archives::extract(root / "input.tar", root / "folder", {"folder"}, control())));
        QCOMPARE(get(root / "folder/folder/b"), std::string("two")); QVERIFY(!fs::exists(root / "folder/other"));
        const auto result = Archives::extract(root / "input.tar", root / "too-large", {}, control(), {2, 2, 100});
        QVERIFY(!success(result)); QVERIFY(!fs::exists(root / "too-large"));
        for (const auto &entry : fs::directory_iterator(root)) QVERIFY(!entry.path().filename().native().starts_with(".tc-extract-"));
        QVERIFY(!Archives::list(root / "input.tar", control(), {1024, 1024, 1}).error.isEmpty());
    }
    void unsafeArchivesNeverPublish_data()
    {
        QTest::addColumn<QString>("name"); QTest::addColumn<bool>("link"); QTest::addColumn<bool>("duplicate");
        QTest::newRow("parent") << "../escape" << false << false;
        QTest::newRow("absolute") << "/tmp/escape" << false << false;
        QTest::newRow("windows") << "C:\\escape" << false << false;
        QTest::newRow("link") << "link" << true << false;
        QTest::newRow("duplicate") << "same" << false << true;
    }
    void unsafeArchivesNeverPublish()
    {
        QFETCH(QString, name); QFETCH(bool, link); QFETCH(bool, duplicate);
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        std::vector<std::pair<std::string, std::string>> entries{{"safe", "first"}, {name.toStdString(), "unsafe"}};
        if (duplicate) entries.push_back({name.toStdString(), "duplicate"});
        fixture(root / "unsafe.tar", entries, link);
        const auto listed = Archives::list(root / "unsafe.tar", control());
        QVERIFY(listed.error.isEmpty());
        QVERIFY(std::any_of(listed.entries.begin(), listed.entries.end(), [](const auto &entry) { return !entry.unsafe.isEmpty(); }));
        const auto result = Archives::extract(root / "unsafe.tar", root / "output", {}, control());
        QVERIFY(!success(result)); QVERIFY(!fs::exists(root / "output")); QVERIFY(!fs::exists(root / "escape"));
    }
    void truncatedArchiveAndHardLinksAreRejected()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fixture(root / "truncated.tar", {{"file", "payload"}}); fs::resize_file(root / "truncated.tar", 514);
        QVERIFY(!Archives::list(root / "truncated.tar", control()).error.isEmpty());
        QVERIFY(!success(Archives::extract(root / "truncated.tar", root / "truncated-output", {}, control()))); QVERIFY(!fs::exists(root / "truncated-output"));
        auto *writer = archive_write_new(); QVERIFY(writer); QCOMPARE(archive_write_set_format_pax_restricted(writer), ARCHIVE_OK);
        QCOMPARE(archive_write_open_filename(writer, (root / "hardlink.tar").c_str()), ARCHIVE_OK);
        auto *entry = archive_entry_new(); archive_entry_set_pathname(entry, "hardlink"); archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_size(entry, 0); archive_entry_set_hardlink(entry, "../../outside");
        QCOMPARE(archive_write_header(writer, entry), ARCHIVE_OK); archive_entry_free(entry); QCOMPARE(archive_write_close(writer), ARCHIVE_OK); archive_write_free(writer);
        const auto listing = Archives::list(root / "hardlink.tar", control()); QVERIFY(listing.error.isEmpty()); QVERIFY(!listing.entries.front().unsafe.isEmpty());
        QVERIFY(!success(Archives::extract(root / "hardlink.tar", root / "hardlink-output", {}, control()))); QVERIFY(!fs::exists(root / "hardlink-output"));
    }
    void archiveCancellationDuringStreamingCleansStaging()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); put(root / "large"); fs::resize_file(root / "large", 128 * 1024 * 1024);
        auto token = control();
        auto packing = std::async(std::launch::async, [=] { return Archives::create({root / "large"}, root / "cancelled.zip", Archives::Format::Zip, token); });
        while (token->bytes == 0 && packing.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::yield();
        token->cancelled = true; QVERIFY(packing.get().cancelled); QVERIFY(!fs::exists(root / "cancelled.zip"));
        QVERIFY(success(Archives::create({root / "large"}, root / "valid.tar", Archives::Format::Tar, control())));
        token = control();
        auto extracting = std::async(std::launch::async, [=] { return Archives::extract(root / "valid.tar", root / "cancelled", {}, token); });
        while (token->bytes == 0 && extracting.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::yield();
        token->cancelled = true; QVERIFY(extracting.get().cancelled); QVERIFY(!fs::exists(root / "cancelled"));
        for (const auto &entry : fs::directory_iterator(root)) {
            QVERIFY(!entry.path().filename().native().starts_with(".tc-pack-")); QVERIFY(!entry.path().filename().native().starts_with(".tc-extract-"));
        }
    }
    void archiveCancellationAndSourceRecursion()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fs::create_directory(root / "source"); put(root / "source/file");
        QVERIFY(!success(Archives::create({root / "source"}, root / "source/recursive.zip", Archives::Format::Zip, control())));
        QVERIFY(!fs::exists(root / "source/recursive.zip"));
        auto token = control(); token->cancelled = true;
        QVERIFY(Archives::create({root / "source/file"}, root / "cancelled.zip", Archives::Format::Zip, token).cancelled);
        QVERIFY(!fs::exists(root / "cancelled.zip"));
        fixture(root / "valid.tar", {{"data", "value"}});
        QVERIFY(Archives::extract(root / "valid.tar", root / "cancelled", {}, token).cancelled);
        QVERIFY(!fs::exists(root / "cancelled"));
        fs::create_symlink(root / "source/file", root / "link");
        QVERIFY(!success(Archives::create({root / "link"}, root / "link.zip", Archives::Format::Zip, control())));
    }
    void renameRulesAndConflicts()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        put(root / "photo-a.JPG", "a"); put(root / "photo-b.JPG", "b");
        BatchRename::Rule rule{"{n}_{name}{ext}", "photo-", "image-", false, BatchRename::LetterCase::Lower, 10, 2, 3};
        auto plan = BatchRename::preview({root / "photo-a.JPG", root / "photo-b.JPG"}, rule);
        QVERIFY2(plan.errors.empty(), qPrintable(plan.errors.join('\n')));
        QVERIFY(plan.items[0].target == root / "010_image-a.jpg"); QVERIFY(plan.items[1].target == root / "012_image-b.jpg");
        QVERIFY(success(BatchRename::apply(plan, control(), directory.filePath("jobs"))));
        QCOMPARE(get(root / "010_image-a.jpg"), std::string("a"));
        rule.pattern = "duplicate";
        QVERIFY(!BatchRename::preview({root / "010_image-a.jpg", root / "012_image-b.jpg"}, rule).errors.empty());
        rule.pattern = "../escape"; QVERIFY(!BatchRename::preview({root / "010_image-a.jpg"}, rule).errors.empty());
        rule.pattern = "{name}{ext}"; rule.regex = true; rule.find = "[";
        QVERIFY(!BatchRename::preview({root / "010_image-a.jpg"}, rule).errors.empty());
    }
    void renameCyclesAndStalePreview()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); put(root / "1", "one"); put(root / "2", "two");
        auto plan = BatchRename::preview({root / "1", root / "2"}, {}); QVERIFY(plan.errors.empty());
        std::swap(plan.items[0].target, plan.items[1].target);
        QVERIFY(success(BatchRename::apply(plan, control())));
        QCOMPARE(get(root / "1"), std::string("two")); QCOMPARE(get(root / "2"), std::string("one"));
        BatchRename::Rule rule; rule.pattern = "new_{name}";
        plan = BatchRename::preview({root / "1", root / "2"}, rule); put(root / "1", "changed");
        QVERIFY(!success(BatchRename::apply(plan, control()))); QVERIFY(!fs::exists(root / "new_1"));
        QCOMPARE(get(root / "1"), std::string("changed"));
        plan = BatchRename::preview({root / "1", root / "2"}, rule);
        put(root / "new_2", "external");
        QVERIFY(!success(BatchRename::apply(plan, control())));
        QCOMPARE(get(root / "new_2"), std::string("external")); QVERIFY(fs::exists(root / "1"));
    }
    void renamePublishRaceRollsBackCompletedPrefix()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        std::vector<fs::path> sources;
        for (int i = 0; i < 512; ++i) { const auto path = root / ("file-" + std::to_string(i)); put(path, std::to_string(i)); sources.push_back(path); }
        BatchRename::Rule rule; rule.pattern = "target_{n}"; rule.width = 3;
        const auto plan = BatchRename::preview(sources, rule); QVERIFY(plan.errors.empty());
        const auto token = control();
        auto future = std::async(std::launch::async, [=, journal = directory.filePath("jobs")] { return BatchRename::apply(plan, token, journal); });
        bool injected = false;
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            for (const auto &entry : fs::directory_iterator(root)) if (entry.path().filename().native().starts_with(".tc-rename-")) {
                const int fd = ::open((root / "target_512").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                if (fd >= 0) { QCOMPARE(::write(fd, "external", 8), ssize_t(8)); ::close(fd); injected = true; }
                break;
            }
            if (injected) break;
            std::this_thread::yield();
        }
        const auto result = future.get();
        QVERIFY2(injected, "Race fixture must create a destination while temporary rename entries exist");
        QVERIFY(!success(result));
        for (int i = 0; i < 512; ++i) QCOMPARE(get(sources[i]), std::to_string(i));
        QCOMPARE(get(root / "target_512"), std::string("external"));
        for (const auto &entry : fs::directory_iterator(root)) QVERIFY(!entry.path().filename().native().starts_with(".tc-rename-"));
    }
    void renameCancellationLeavesOriginalNames()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); put(root / "file");
        BatchRename::Rule rule; rule.pattern = "renamed";
        const auto plan = BatchRename::preview({root / "file"}, rule);
        auto token = control(); token->cancelled = true;
        QVERIFY(BatchRename::apply(plan, token).cancelled);
        QVERIFY(fs::exists(root / "file")); QVERIFY(!fs::exists(root / "renamed"));
    }
    void contentComparisonOffsets()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        put(root / "a", "same bytes"); put(root / "b", "same bytes");
        QVERIFY(FileAnalysis::compareFiles(root / "a", root / "b", control()).equal);
        put(root / "b", "same Bytes"); const auto compared = FileAnalysis::compareFiles(root / "a", root / "b", control());
        QVERIFY(!compared.equal); QCOMPARE(compared.offset, quint64(5)); QVERIFY(compared.error.isEmpty());
        put(root / "b", "same"); QCOMPARE(FileAnalysis::compareFiles(root / "a", root / "b", control()).offset, quint64(4));
        fs::create_symlink(root / "a", root / "link"); QVERIFY(!FileAnalysis::compareFiles(root / "a", root / "link", control()).error.isEmpty());
    }
    void directoryComparisonAndSelectiveSync()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fs::create_directories(root / "left/sub/empty"); fs::create_directory(root / "right");
        put(root / "left/sub/file", "copy"); put(root / "left/changed", "left"); put(root / "right/changed", "right"); put(root / "right/retain", "keep");
        const auto plan = FileAnalysis::compareDirectories(root / "left", root / "right", true, control());
        QVERIFY2(plan.errors.empty(), qPrintable(plan.errors.join('\n')));
        std::vector<size_t> selected;
        for (size_t i = 0; i < plan.rows.size(); ++i) if (plan.rows[i].actionable) selected.push_back(i);
        const auto result = FileAnalysis::synchronize(plan, selected, control());
        QVERIFY2(success(result), qPrintable(result.items.back().error));
        QCOMPARE(get(root / "right/sub/file"), std::string("copy")); QCOMPARE(get(root / "right/changed"), std::string("left"));
        QCOMPARE(get(root / "right/retain"), std::string("keep")); QVERIFY(fs::is_directory(root / "right/sub/empty"));
        const auto equal = FileAnalysis::compareDirectories(root / "left", root / "right", true, control());
        for (const auto &row : equal.rows) QVERIFY(row.difference == FileAnalysis::Difference::Equal || row.difference == FileAnalysis::Difference::RightOnly);
    }
    void syncRefusesStaleAndOverlappingPlans()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        fs::create_directories(root / "left"); fs::create_directory(root / "right"); put(root / "left/file", "before");
        auto plan = FileAnalysis::compareDirectories(root / "left", root / "right", true, control());
        put(root / "left/file", "changed");
        QVERIFY(!success(FileAnalysis::synchronize(plan, {0}, control()))); QVERIFY(fs::is_empty(root / "right"));
        fs::create_directory(root / "left/child");
        plan = FileAnalysis::compareDirectories(root / "left", root / "left/child", true, control());
        QVERIFY(!success(FileAnalysis::synchronize(plan, {0}, control()))); QVERIFY(fs::is_empty(root / "left/child"));
    }
    void syncDestinationChangesRefuseAllMutations()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); fs::create_directory(root / "left"); fs::create_directory(root / "right");
        put(root / "left/a-new", "new"); put(root / "left/z-existing", "source"); put(root / "right/z-existing", "original");
        const auto plan = FileAnalysis::compareDirectories(root / "left", root / "right", true, control());
        std::vector<size_t> selected; for (size_t i = 0; i < plan.rows.size(); ++i) if (plan.rows[i].actionable) selected.push_back(i);
        put(root / "right/z-existing", "external edit");
        QVERIFY(!success(FileAnalysis::synchronize(plan, selected, control())));
        QVERIFY(!fs::exists(root / "right/a-new")); QCOMPARE(get(root / "right/z-existing"), std::string("external edit"));
    }
    void folderStatisticsAndDuplicateCandidates()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); fs::create_directory(root / "sub");
        put(root / "a", "duplicate"); put(root / "sub/b", "duplicate"); put(root / "other", "different");
        fs::create_hard_link(root / "a", root / "alias"); fs::create_directory_symlink(root, root / "sub/cycle");
        const auto stats = FileAnalysis::statistics({root}, control());
        QCOMPARE(stats.files, quint64(4)); QCOMPARE(stats.directories, quint64(2)); QCOMPARE(stats.links, quint64(1)); QCOMPARE(stats.bytes, quint64(36));
        const auto duplicates = FileAnalysis::duplicates(root, control());
        QVERIFY(duplicates.errors.empty()); QCOMPARE(duplicates.groups.size(), size_t(1)); QCOMPARE(duplicates.groups.front().paths.size(), size_t(2)); QCOMPARE(duplicates.groups.front().size, quint64(9));
        fs::remove(root / "sub/cycle");
    }
    void regexSizeAndDateSearchFilters()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path());
        put(root / "a1.txt", "Hello42"); put(root / "a2.txt", "Helloaa"); put(root / "a3.txt", "Hello123456789"); put(root / "b.txt", "Hello42");
        SearchRequest request; request.root = root; request.pattern = "^a[0-9]\\.txt$"; request.nameRegex = true;
        request.content = "Hello[0-9]+"; request.contentRegex = true; request.caseSensitive = true; request.minBytes = 7; request.maxBytes = 7;
        auto result = searchFiles(request, control()); QVERIFY(result.errors.empty()); QCOMPARE(result.paths.size(), size_t(1)); QVERIFY(result.paths.front() == root / "a1.txt");
        const timespec times[]{{1000, 0}, {1000, 0}}; QCOMPARE(::utimensat(AT_FDCWD, (root / "a1.txt").c_str(), times, 0), 0);
        request.modifiedAfter = 2000; QCOMPARE(searchFiles(request, control()).paths.size(), size_t(0));
        request.modifiedAfter = -1; request.modifiedBefore = 2000; QCOMPARE(searchFiles(request, control()).paths.size(), size_t(1));
        request.pattern = "["; QVERIFY(!searchFiles(request, control()).errors.empty());
        request.pattern = "a"; request.minBytes = 8; request.maxBytes = 1; QVERIFY(!searchFiles(request, control()).errors.empty());
        request.minBytes = -1; request.maxBytes = -1; request.content = "["; QVERIFY(!searchFiles(request, control()).errors.empty());
    }
    void permissionsAndSymlinkOperations()
    {
        QTemporaryDir directory; const auto root = nativePath(directory.path()); put(root / "file", "keep");
        auto request = LocalFiles::Request{LocalFiles::Operation::SetPermissions, {root / "file"}, {}, LocalFiles::Conflict::Fail, {}, 0000};
        QVERIFY(success(LocalFiles::execute(request, control()))); QCOMPARE(static_cast<unsigned>(fs::status(root / "file").permissions()) & 0777, 0U);
        request.permissions = 0640; QVERIFY(success(LocalFiles::execute(request, control()))); QCOMPARE(get(root / "file"), std::string("keep"));
        QVERIFY(success(LocalFiles::execute({LocalFiles::Operation::SymbolicLink, {root / "file"}, root / "link"}, control())));
        QVERIFY(fs::read_symlink(root / "link") == root / "file");
        request.sources = {root / "link"}; QVERIFY(!success(LocalFiles::execute(request, control())));
        QCOMPARE(static_cast<unsigned>(fs::status(root / "file").permissions()) & 0777, 0640U);
        QVERIFY(!success(LocalFiles::execute({LocalFiles::Operation::SymbolicLink, {root / "file"}, root / "link"}, control())));
    }
};
QTEST_GUILESS_MAIN(AdvancedTests)
#include "advanced_tests.moc"
