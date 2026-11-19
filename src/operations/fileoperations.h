#pragma once

#include <QString>
#include <QByteArray>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include <chrono>

namespace LocalFiles {
enum class Operation { Copy, Move, Rename, Trash, Delete, MakeDirectory, MakeFile, SetPermissions, SymbolicLink };
enum class Conflict { Fail, Skip, ReplaceFiles };
struct Request {
    Operation operation = Operation::Copy;
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destination;
    Conflict conflict = Conflict::Fail;
    QString journalDirectory;
    unsigned permissions = 0644;
};
struct Progress {
    enum class Phase { Idle, Scanning, Working, Finished };
    Phase phase = Phase::Idle;
    std::filesystem::path currentPath, currentDestination;
    Operation operation = Operation::Copy;
    uint64_t totalBytes = 0, doneBytes = 0, totalEntries = 0, doneEntries = 0;
    uint64_t fileBytes = 0, fileTotal = 0;
    bool totalsKnown = false, byteBased = false, fileComplete = false;
};
struct Control {
    std::atomic_bool cancelled{false};
    std::atomic_bool paused{false};
    std::atomic_uint64_t bytes{0};
    std::atomic_uint64_t completed{0};
    Progress progressSnapshot() const;
    void startProgress(Operation operation = Operation::Copy);
    void setTotals(uint64_t bytes, uint64_t entries, bool known, bool byteBased);
    void beginFile(const std::filesystem::path &path, uint64_t size, const std::filesystem::path &destination = {});
    std::atomic_uint64_t bytesPerSecond{0};
    void throttle(uint64_t count);
    void advanceFile(uint64_t count);
    void finishFile();
    void finishProgress();
  private:
    mutable std::mutex progressMutex_;
    Progress progress_;
    uint64_t throttleRate_ = 0, throttleBytes_ = 0;
    std::chrono::steady_clock::time_point throttleEpoch_;

};
struct Cancelled {};
inline void checkpoint(const std::shared_ptr<Control> &control)
{
    while (control->paused && !control->cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (control->cancelled) throw Cancelled{};
}
struct Outcome {
    std::filesystem::path source;
    std::filesystem::path destination;
    QString error;
    bool skipped = false;
};
struct Result {
    std::vector<Outcome> items;
    bool cancelled = false;
    QString journalError;
};
// Blocking service: callers run it outside the UI thread.
Result execute(const Request &request, const std::shared_ptr<Control> &control);
QString saveFile(const std::filesystem::path &path, const QByteArray &expected, const QByteArray &replacement);
}
