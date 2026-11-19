#include "filesystem/directorysizes.h"
#include "filesystem/pathencoding.h"
#include "operations/iothreadpool.h"
#include "settings/applicationpreferences.h"
#include <QDateTime>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <map>
#include <mutex>

using ApplicationPreferences::flag;
using ApplicationPreferences::text;

FileAnalysis::Statistics DirectorySizes::calculate(const std::vector<std::filesystem::path> &paths,
                                                   const std::shared_ptr<LocalFiles::Control> &control) {
    static std::mutex mutex;
    struct Entry {
        FileAnalysis::Statistics stats;
        qint64 time;
    };
    static std::map<std::filesystem::path, Entry> cache;
    if (paths.size() == 1 &&
        (flag("IdConf_EverythingForSize") || text("IdConf_GetDirSize") == "Recent size index")) {
        std::lock_guard lock(mutex);
        auto it = cache.find(paths[0]);
        if (it != cache.end() && QDateTime::currentMSecsSinceEpoch() - it->second.time < 1000)
            return it->second.stats;
    }
    FileAnalysis::Statistics result;
    if (text("IdConf_GetDirSize") == "Local filesystems only")
        for (const auto &p : paths) {
            const auto type = QStorageInfo(displayPath(p)).fileSystemType();
            if (type.contains("nfs") || type.contains("cifs") || type.contains("sshfs")) {
                result.errors << "Network directory size scan disabled by preferences";
                return result;
            }
        }
    result = FileAnalysis::statistics(paths, control);
    if (paths.size() == 1 && !result.cancelled && !result.truncated && result.errors.isEmpty()) {
        std::lock_guard lock(mutex);
        if (cache.size() > 1024)
            cache.clear();
        cache[paths[0]] = {result, QDateTime::currentMSecsSinceEpoch()};
    }
    return result;
}
bool DirectorySizes::confirmPreflight(const std::vector<std::filesystem::path> &paths, QWidget *parent,
                                      QString &summary) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Count selected items");
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel("Counting files and bytes before the operation…", &dialog);
    layout->addWidget(label);
    auto *cancel = new QPushButton("Cancel", &dialog);
    layout->addWidget(cancel);
    auto control = std::make_shared<LocalFiles::Control>();
    QFutureWatcher<FileAnalysis::Statistics> watcher;
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        control->cancelled = true;
        cancel->setEnabled(false);
        label->setText("Cancelling…");
    });
    QObject::connect(&watcher, &QFutureWatcher<FileAnalysis::Statistics>::finished, &dialog,
                     &QDialog::accept);
    watcher.setFuture(QtConcurrent::run(ioPool(), calculate, paths, control));
    dialog.exec();
    if (!watcher.isFinished()) {
        control->cancelled = true;
        watcher.waitForFinished();
    }
    const auto result = watcher.result();
    summary = QString("%1 files, %2 directories, %3 bytes")
                  .arg(result.files)
                  .arg(result.directories)
                  .arg(result.bytes);
    if (!result.errors.isEmpty() || result.truncated) {
        QMessageBox::warning(parent, "Incomplete count", result.errors.join('\n'));
        return false;
    }
    return !result.cancelled;
}
