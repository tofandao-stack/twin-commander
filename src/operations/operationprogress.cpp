#include "operations/fileoperations.h"
namespace LocalFiles {
Progress Control::progressSnapshot() const {
    std::lock_guard lock(progressMutex_);
    return progress_;
}
void Control::startProgress() {
    std::lock_guard lock(progressMutex_);
    progress_ = {};
    progress_.phase = Progress::Phase::Scanning;
}
void Control::setTotals(uint64_t bytes, uint64_t entries, bool known, bool byteBased) {
    std::lock_guard lock(progressMutex_);
    progress_.totalBytes = bytes;
    progress_.totalEntries = entries;
    progress_.totalsKnown = known;
    progress_.byteBased = byteBased;
    progress_.phase = Progress::Phase::Working;
}
void Control::beginFile(const std::filesystem::path &path, uint64_t size) {
    std::lock_guard lock(progressMutex_);
    progress_.currentPath = path;
    progress_.fileBytes = 0;
    progress_.fileTotal = size;
    progress_.fileComplete = false;
}
void Control::advanceFile(uint64_t count) {
    std::lock_guard lock(progressMutex_);
    progress_.fileBytes += count;
    progress_.doneBytes += count;
}
void Control::finishFile() {
    std::lock_guard lock(progressMutex_);
    progress_.fileComplete = true;
    ++progress_.doneEntries;
}
void Control::finishProgress() {
    std::lock_guard lock(progressMutex_);
    progress_.phase = Progress::Phase::Finished;
}
} // namespace LocalFiles
