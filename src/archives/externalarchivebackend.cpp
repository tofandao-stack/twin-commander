#include "filesystem/pathencoding.h"
#include "filesystem/filenamepresentation.h"
#include "archives/externalarchivebackend.h"
#include "filesystem/filetransforms.h"
#include "settings/applicationpreferences.h"
#include "platform/nativeextensions.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <sys/resource.h>
namespace {
using namespace NativeFs;
QString engine(const std::filesystem::path &path) {
    const auto ext = displayPath(path.extension()).toLower();
    const auto key = ext == ".arj"                    ? "arj_packer"
                     : ext == ".lzh" || ext == ".lha" ? "lha_packer"
                     : ext == ".rar"                  ? "rar_packer"
                     : ext == ".ace"                  ? "ace_packer"
                                                      : "z7_packer";
    auto tool = QSettings().value("tools/" + QString(key)).toString();
    if (tool.isEmpty())
        tool = QStandardPaths::findExecutable("7z");
    if (tool.isEmpty())
        tool = QStandardPaths::findExecutable("7zz");
    if (tool.isEmpty())
        throw Error("Install 7z/7zz or configure a native archive tool");
    return tool;
}
void validate(const std::filesystem::path &stage, const Archives::Limits &limits,
              std::vector<Archives::Entry> &entries) {
    quint64 bytes = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(stage);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (entries.size() >= limits.entries || it.depth() > 128)
            throw Error("External archive entry/depth limit exceeded");
        const auto st = inspect(it->path());
        if ((!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) || (S_ISREG(st.st_mode) && st.st_nlink != 1))
            throw Error("External archive contains a link or special entry");
        if (S_ISREG(st.st_mode)) {
            if (quint64(st.st_size) > limits.fileBytes || quint64(st.st_size) > limits.bytes - bytes)
                throw Error("External archive expanded-size limit exceeded");
            bytes += st.st_size;
        }
        const auto relative = it->path().lexically_relative(stage);
        for (const auto &part : relative)
            if (part == "..")
                throw Error("Unsafe archive pathname");
        entries.push_back(
            {relative.native(), S_ISREG(st.st_mode) ? quint64(st.st_size) : 0, S_ISDIR(st.st_mode), {}});
    }
}
void unpack(const std::filesystem::path &path, const QString &stage,
            const std::shared_ptr<LocalFiles::Control> &control, const Archives::Limits &limits) {
    const auto wrapper = QStandardPaths::findExecutable("bwrap");
    if (wrapper.isEmpty())
        throw Error("External extraction requires bubblewrap; no unsandboxed fallback is allowed");
    const auto before = inspect(path);
    if (!S_ISREG(before.st_mode) || quint64(before.st_size) > limits.bytes)
        throw Error("External archive must be a bounded regular file");
    FileDescriptor archive(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    verify(path, archive.value, before);
    const auto tool = engine(path);
    QStringList args{"--die-with-parent",
                     "--new-session",
                     "--unshare-all",
                     "--ro-bind",
                     "/",
                     "/",
                     "--proc",
                     "/proc",
                     "--dev",
                     "/dev",
                     "--tmpfs",
                     "/tmp",
                     "--ro-bind",
                     "/proc/self/fd/" + QString::number(archive.value),
                     "/tmp/source.archive",
                     "--bind",
                     stage,
                     "/tmp/out",
                     "--chdir",
                     "/tmp/out",
                     "--",
                     tool,
                     "x"};
    if (QFileInfo(tool).fileName().contains("7z"))
        args << "-y" << "-bd" << "-o/tmp/out" << "/tmp/source.archive";
    else
        args << "-y" << "/tmp/source.archive";
    QProcess process;
    process.setChildProcessModifier([fd = archive.value, limits] {
        ::fcntl(fd, F_SETFD, 0);
        const struct rlimit fileLimit {
            limits.fileBytes, limits.fileBytes
        };
        ::setrlimit(RLIMIT_FSIZE, &fileLimit);
    });
    process.start(wrapper, args);
    if (!process.waitForStarted(3000))
        throw Error("Archive sandbox could not start");
    process.closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    QByteArray output;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        output += process.readAllStandardOutput();
        output += process.readAllStandardError();
        bool tooLarge = false;
        try {
            std::vector<Archives::Entry> probe;
            validate(nativePath(stage), limits, probe);
        } catch (...) {
            tooLarge = true;
        }
        if (control->cancelled || timer.elapsed() > 300000 || output.size() > 1024 * 1024 || tooLarge) {
            process.kill();
            process.waitForFinished();
            if (control->cancelled)
                throw LocalFiles::Cancelled{};
            throw Error("External archive time/output/expanded-size limit exceeded");
        }
    }
    output += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw Error(("Sandboxed extractor failed: " + QString::fromUtf8(output.right(4096))).toStdString());
    verify(path, archive.value, before);
    if (displayPath(path.extension()).compare(".arj", Qt::CaseInsensitive) == 0 &&
        !ApplicationPreferences::flag("arj_longnames")) {
        std::vector<std::filesystem::path> files;
        for (const auto &it : std::filesystem::recursive_directory_iterator(nativePath(stage)))
            files.push_back(it.path());
        std::sort(files.begin(), files.end(),
                  [](const auto &a, const auto &b) { return a.native().size() > b.native().size(); });
        for (const auto &p : files) {
            auto alias = p.parent_path() / nativePath(FilenamePresentation::dosAlias(displayPath(p.filename())));
            int serial = 1;
            while (alias != p && std::filesystem::exists(alias))
                alias =
                    p.parent_path() / nativePath(FilenamePresentation::dosAlias(displayPath(p.filename()), ++serial));
            if (alias != p)
                std::filesystem::rename(p, alias);
        }
    }
}
} // namespace
bool ExternalArchiveBackend::external(const std::filesystem::path &path) {
    const auto ext = displayPath(path.extension()).toLower();
    if (ext == ".zip")
        return !ApplicationPreferences::flag("zip_useinternal") || ApplicationPreferences::flag("zip_use7z");
    const QMap<QString, QString> keys{{".arj", "arj_useinternal"},
                                      {".lzh", "lzh_useinternal"},
                                      {".lha", "lzh_useinternal"},
                                      {".rar", "rar_useinternal"},
                                      {".ace", "ace_useinternal"}};
    return keys.contains(ext) && !ApplicationPreferences::flag(qPrintable(keys.value(ext)));
}
Archives::Listing ExternalArchiveBackend::list(const std::filesystem::path &path,
                                      const std::shared_ptr<LocalFiles::Control> &control,
                                      const Archives::Limits &limits) {
    Archives::Listing result;
    try {
        QTemporaryDir temp;
        if (!temp.isValid())
            throw Error("Cannot create archive staging directory");
        unpack(path, temp.path(), control, limits);
        validate(nativePath(temp.path()), limits, result.entries);
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &e) {
        result.error = QString::fromLocal8Bit(e.what());
    }
    return result;
}
LocalFiles::Result ExternalArchiveBackend::extract(const std::filesystem::path &path,
                                          const std::filesystem::path &destination,
                                          const std::vector<std::string> &selected,
                                          const std::shared_ptr<LocalFiles::Control> &control,
                                          const Archives::Limits &limits) {
    LocalFiles::Result result;
    LocalFiles::Outcome outcome{path, destination, {}, false};
    try {
        if (!destination.is_absolute() || destination.filename().empty() || destination.filename() == "..")
            throw Error("Choose a new absolute extraction directory");
        QTemporaryDir stage(displayPath(destination.parent_path()) + "/.tc-external-XXXXXX");
        if (!stage.isValid())
            throw Error("Cannot create archive staging directory");
        unpack(path, stage.path(), control, limits);
        std::vector<Archives::Entry> entries;
        validate(nativePath(stage.path()), limits, entries);
        if (!selected.empty())
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                bool keep = false;
                for (const auto &s : selected)
                    if (it->name == s || it->name.starts_with(s + "/") || s.starts_with(it->name + "/"))
                        keep = true;
                if (!keep)
                    std::filesystem::remove_all(nativePath(stage.path()) / it->name);
            }
        LocalFiles::checkpoint(control);
        FileDescriptor parent(::open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        renameExclusive(parent.value, nativePath(stage.path()).filename().native(),
                        destination.filename().native());
        stage.setAutoRemove(false);
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &e) {
        outcome.error = QString::fromLocal8Bit(e.what());
    }
    result.items.push_back(outcome);
    return result;
}
QStringList ExternalArchiveBackend::sevenArguments(bool zip, bool encrypted) {
    QStringList args{"a", zip ? "-tzip" : "-t7z", "-bd", "-y"};
    int level = 6;
    if (zip) {
        const int choice = QSettings().value("archive/zipLevelChoice", 0).toInt();
        const int levels[]{6, 9, 1};
        level = choice < 3 && choice >= 0 ? levels[choice]
                                          : qBound(0, QSettings().value("archive/zipLevel", 6).toInt(), 9);
    } else {
        level = ApplicationPreferences::flag("z7_max")     ? 9
                : ApplicationPreferences::flag("z7_min")   ? 1
                : ApplicationPreferences::flag("z7_other") ? qBound(0, ApplicationPreferences::number("z7_value", 6), 9)
                                                  : 6;
    }
    args << "-mx=" + QString::number(level);
    if (!zip) {
        const auto method = ApplicationPreferences::text("z7_method", "LZMA2");
        const QSet<QString> methods{"LZMA2", "LZMA", "PPMd", "BZip2", "Deflate", "Copy"};
        if (!methods.contains(method))
            throw Error("Unsupported 7-Zip compression method");
        args << "-m0=" + method;
        if (method == "LZMA" || method == "LZMA2")
            args << "-md=" + ApplicationPreferences::text("z7_dict", "16m")
                 << "-mfb=" + ApplicationPreferences::text("z7_word", "32");
        if (method == "PPMd")
            args << "-mmem=" + ApplicationPreferences::text("z7_dict", "16m");
        args << "-ms=" +
                    (ApplicationPreferences::flag("z7_solid") ? ApplicationPreferences::text("z7_solidblock", "64m") : "off")
             << "-mmt=" + ApplicationPreferences::text("z7_threads", "on");
        if (encrypted)
            args << "-mhe=on";
    }
    if (encrypted)
        args << "-p";
    const auto extra = QProcess::splitCommand(ApplicationPreferences::text("z7_extraparameters"));
    if (extra.size() > 32)
        throw Error("Too many 7-Zip extra options");
    for (const auto &arg : extra) {
        if (!arg.startsWith("-m") || arg.startsWith("-mhe") || arg.contains('\n') ||
            arg.contains(QChar::Null) || arg.size() > 256)
            throw Error("Extra 7-Zip options must be bounded -m compression options; use the password prompt "
                        "for encryption");
        args << arg;
    }
    const auto root = ApplicationPreferences::flag("internalzip_otherdir") ? ApplicationPreferences::text("internalzip_tempdir")
                                                                  : QDir::tempPath();
    if (!QDir::isAbsolutePath(root) || !QDir(root).exists())
        throw Error("Archive temporary directory must exist and be absolute");
    args << "-w" + root;
    return args;
}
LocalFiles::Result ExternalArchiveBackend::pack(const std::vector<std::filesystem::path> &sources,
                                       const std::filesystem::path &output, bool zip, QByteArray password,
                                       const std::shared_ptr<LocalFiles::Control> &control) {
    try {
        if (password.contains('\n') || password.contains('\r') || password.contains('\0'))
            throw Error("Archive passwords cannot contain control separators");
        QString program;
        if (!zip && ApplicationPreferences::flag("z7_internalpack")) {
            program = QStandardPaths::findExecutable("7z");
            if (program.isEmpty())
                program = QStandardPaths::findExecutable("7zz");
            if (program.isEmpty())
                throw Error("The native 7-Zip engine requires 7z/7zz");
        } else if (!zip) {
            program = QSettings().value("tools/z7_packer").toString();
            if (program.isEmpty())
                throw Error("Configure an external 7-Zip executable first");
        } else
            program = engine(output);
        QStringList args = sevenArguments(zip, !password.isEmpty());
        if (!zip && ApplicationPreferences::flag("z7_preferplugin") && !NativeExtensions::plugins("packer").isEmpty()) {
            for (const auto &v : NativeExtensions::plugins("packer")) {
                const auto p = v.toMap();
                if (QDir::match(p.value("mask").toString(), displayPath(output.filename()))) {
                    if (!password.isEmpty())
                        throw Error("Encrypted archives require the native 7-Zip engine");
                    program = p.value("program").toString();
                    args = QProcess::splitCommand(p.value("arguments").toString());
                    break;
                }
            }
        }
        auto result = FileTransforms::packExternal(sources, output, program, args, control,
                                                  password.isEmpty() ? QByteArray()
                                                                     : password + '\n' + password + '\n');
        password.fill('\0');
        return result;
    } catch (const std::exception &e) {
        password.fill('\0');
        return {{{{}, output, QString::fromLocal8Bit(e.what()), false}}, false, {}};
    }
}
