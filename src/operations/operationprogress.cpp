#include "operations/fileoperations.h"
#include <algorithm>
namespace LocalFiles {
Progress Control::progressSnapshot() const {
    std::lock_guard lock(progressMutex_);
    return progress_;
}
void Control::startProgress(Operation operation) {
    std::lock_guard lock(progressMutex_);
    progress_ = {};
    progress_.phase = Progress::Phase::Scanning;
    progress_.operation = operation;
    throttleRate_ = throttleBytes_ = 0;
}
void Control::setTotals(uint64_t bytes, uint64_t entries, bool known, bool byteBased) {
    std::lock_guard lock(progressMutex_);
    progress_.totalBytes = bytes;
    progress_.totalEntries = entries;
    progress_.totalsKnown = known;
    progress_.byteBased = byteBased;
    progress_.phase = Progress::Phase::Working;
}
void Control::beginFile(const std::filesystem::path &path, uint64_t size,
                        const std::filesystem::path &destination) {
    std::lock_guard lock(progressMutex_);
    progress_.currentPath = path;
    progress_.currentDestination = destination;
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

void Control::throttle(uint64_t count) {
    using Clock = std::chrono::steady_clock;
    const auto rate = bytesPerSecond.load();
    if (!rate) {
        throttleRate_ = 0;
        return;
    }
    if (rate != throttleRate_) {
        throttleRate_ = rate;
        throttleBytes_ = 0;
        throttleEpoch_ = Clock::now();
    }
    throttleBytes_ += count;
    const auto deadline = [&] {
        return throttleEpoch_ +
               std::chrono::duration_cast<Clock::duration>(
                   std::chrono::duration<long double>(static_cast<long double>(throttleBytes_) / rate));
    };
    while (true) {
        if (cancelled)
            throw Cancelled{};
        if (bytesPerSecond.load() != rate) {
            throttleRate_ = 0;
            return;
        }
        if (paused) {
            const auto since = Clock::now();
            while (paused && !cancelled && bytesPerSecond.load() == rate)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throttleEpoch_ += Clock::now() - since;
            continue;
        }
        const auto left = deadline() - Clock::now();
        if (left <= Clock::duration::zero())
            return;
        std::this_thread::sleep_for(
            std::min(left, std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(10))));
    }
}
} // namespace LocalFiles
