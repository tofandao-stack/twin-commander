#pragma once

#include <QString>
#include <QByteArray>
#include <atomic>
#include <filesystem>
#include <memory>
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
struct Control {
    std::atomic_bool cancelled{false};
    std::atomic_bool paused{false};
    std::atomic_uint64_t bytes{0};
    std::atomic_uint64_t completed{0};
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
