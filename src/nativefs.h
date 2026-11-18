#pragma once
#include "fileoperations.h"
#include <QCryptographicHash>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace NativeFs {
namespace fs = std::filesystem;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
inline void check(int code) { if (code < 0) throw Error(std::strerror(errno)); }
struct Fd {
    int value = -1;
    explicit Fd(int fd) : value(fd) { check(fd); }
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
inline struct stat inspect(const fs::path &path) { struct stat st{}; check(::lstat(path.c_str(), &st)); return st; }
inline bool same(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_size == b.st_size
        && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
inline struct stat inspectFd(int fd) { struct stat st{}; check(::fstat(fd, &st)); return st; }
inline void verify(const fs::path &path, int fd, const struct stat &before)
{
    if (!same(before, inspectFd(fd)) || !same(before, inspect(path))) throw Error("Entry changed during the operation");
}
inline void renameExclusive(int parent, const std::string &oldName, const std::string &newName)
{
    check(static_cast<int>(::syscall(SYS_renameat2, parent, oldName.c_str(), parent, newName.c_str(), 1)));
}
inline bool inside(const fs::path &child, const fs::path &parent)
{
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c) if (c == child.end() || *c != *p) return false;
    return true;
}
inline QByteArray hash(const fs::path &path, const std::shared_ptr<LocalFiles::Control> &control, const struct stat *expected = nullptr)
{
    LocalFiles::checkpoint(control);
    const auto before = inspect(path);
    if (expected && !same(before, *expected)) throw Error("File changed since directory scan");
    if (!S_ISREG(before.st_mode)) throw Error("Requires a regular file, not a link or directory");
    Fd file(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (!same(before, inspectFd(file.value))) throw Error("File changed before opening");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    std::array<char, 128 * 1024> buffer{};
    while (true) {
        LocalFiles::checkpoint(control);
        const auto size = ::read(file.value, buffer.data(), buffer.size());
        if (size < 0 && errno == EINTR) continue;
        check(static_cast<int>(size));
        if (!size) break;
        digest.addData(QByteArrayView(buffer.data(), size));
        control->bytes += static_cast<uint64_t>(size);
    }
    verify(path, file.value, before);
    return digest.result();
}
}
