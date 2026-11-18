#include "extendedtools.h"
#include "directorymodel.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <sys/xattr.h>

namespace ExtendedTools {
using namespace NativeFs;
namespace {
void writeAll(int fd, QByteArrayView bytes) {
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        check(static_cast<int>(count));
        if (!count)
            throw Error("Incomplete write");
        offset += count;
    }
}
quint16 crc(QByteArrayView bytes) {
    quint16 result = 0;
    for (unsigned char byte : bytes) {
        result ^= static_cast<quint16>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit)
            result = result & 0x8000 ? static_cast<quint16>((result << 1) ^ 0x1021)
                                     : static_cast<quint16>(result << 1);
    }
    return result;
}
QByteArray binhex(const QByteArray &input) {
    static const QByteArray alphabet = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";
    auto begin = input.indexOf("\n:");
    if (begin < 0)
        begin = input.startsWith(':') ? -1 : -2;
    if (begin == -2)
        throw Error("BinHex opening delimiter missing");
    const auto end = input.indexOf(':', begin + 2);
    if (end < 0)
        throw Error("BinHex closing delimiter missing");
    QByteArray packed;
    quint32 bits = 0;
    int available = 0;
    for (qsizetype i = begin + 2; i < end; ++i) {
        const auto c = input[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
            continue;
        const auto digit = alphabet.indexOf(c);
        if (digit < 0)
            throw Error("Invalid BinHex character");
        bits = (bits << 6) | static_cast<quint32>(digit);
        available += 6;
        if (available >= 8) {
            available -= 8;
            packed += static_cast<char>((bits >> available) & 255);
        }
    }
    QByteArray bytes;
    for (qsizetype i = 0; i < packed.size(); ++i) {
        const auto c = static_cast<unsigned char>(packed[i]);
        if (c != 0x90)
            bytes += static_cast<char>(c);
        else {
            if (++i >= packed.size())
                throw Error("Truncated BinHex run");
            const auto count = static_cast<unsigned char>(packed[i]);
            if (!count)
                bytes += char(0x90);
            else {
                if (bytes.isEmpty())
                    throw Error("Invalid BinHex run");
                bytes += QByteArray(count - 1, bytes.back());
            }
        }
        if (bytes.size() > 128 * 1024 * 1024)
            throw Error("BinHex expanded-size limit exceeded");
    }
    if (bytes.isEmpty())
        throw Error("Empty BinHex stream");
    const auto nameLength = static_cast<unsigned char>(bytes[0]);
    const qsizetype header = 1 + nameLength + 1 + 4 + 4 + 2 + 4 + 4;
    if (nameLength < 1 || nameLength > 63 || bytes.size() < header + 2)
        throw Error("Invalid BinHex header");
    const auto dataSize = qFromBigEndian<quint32>(bytes.constData() + header - 8),
               resourceSize = qFromBigEndian<quint32>(bytes.constData() + header - 4);
    const quint64 required = static_cast<quint64>(header) + 2 + dataSize + 2 + resourceSize + 2;
    if (required != static_cast<quint64>(bytes.size()))
        throw Error("BinHex fork sizes do not match");
    const auto validate = [&bytes](qsizetype offset, qsizetype size) {
        if (crc(QByteArrayView(bytes.constData() + offset, size)) !=
            qFromBigEndian<quint16>(bytes.constData() + offset + size))
            throw Error("BinHex CRC mismatch");
    };
    validate(0, header);
    validate(header + 2, dataSize);
    validate(header + 2 + dataSize + 2, resourceSize);
    if (resourceSize)
        throw Error("BinHex contains a resource fork; refusing to discard it on Linux");
    return bytes.mid(header + 2, dataSize);
}
} // namespace
void split(const fs::path &source, const fs::path &newDirectory, quint64 pieceBytes,
           const std::shared_ptr<LocalFiles::Control> &control) {
    if (pieceBytes < 1 || pieceBytes > 64ULL * 1024 * 1024)
        throw Error("Piece size must be 1 byte to 64 MiB");
    const auto before = inspect(source);
    if (!S_ISREG(before.st_mode))
        throw Error("Split requires a regular file");
    if ((static_cast<quint64>(before.st_size) + pieceBytes - 1) / pieceBytes > 100000)
        throw Error("Too many pieces");
    Fd input(::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (!same(before, inspectFd(input.value)))
        throw Error("Source changed before opening");
    const auto temporary =
        newDirectory.parent_path() /
        ('.' + std::string("tc-split-") + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    check(::mkdir(temporary.c_str(), 0700));
    try {
        QJsonArray parts;
        QCryptographicHash whole(QCryptographicHash::Sha256);
        bool finished = false;
        int ordinal = 0;
        while (!finished) {
            LocalFiles::checkpoint(control);
            QByteArray chunk;
            chunk.reserve(static_cast<qsizetype>(pieceBytes));
            std::array<char, 128 * 1024> buffer{};
            while (static_cast<quint64>(chunk.size()) < pieceBytes) {
                LocalFiles::checkpoint(control);
                const auto count = ::read(input.value, buffer.data(),
                                          std::min<quint64>(buffer.size(), pieceBytes - chunk.size()));
                if (count < 0 && errno == EINTR)
                    continue;
                check(static_cast<int>(count));
                if (!count) {
                    finished = true;
                    break;
                }
                chunk.append(buffer.data(), count);
            }
            if (chunk.isEmpty() && ordinal)
                break;
            const auto name = QString("part.%1").arg(++ordinal, 6, 10, QChar('0'));
            Productivity::exportNew(temporary / fs::path(name.toStdString()), chunk);
            whole.addData(chunk);
            control->bytes += chunk.size();
            ++control->completed;
            parts.append(QJsonObject{
                {"name", name},
                {"bytes", chunk.size()},
                {"sha256",
                 QString::fromLatin1(QCryptographicHash::hash(chunk, QCryptographicHash::Sha256).toHex())}});
        }
        verify(source, input.value, before);
        Productivity::exportNew(
            temporary / "parts.tcjoin",
            QJsonDocument(QJsonObject{{"format", "TwinCommander-split-1"},
                                      {"sha256", QString::fromLatin1(whole.result().toHex())},
                                      {"parts", parts}})
                .toJson());
        LocalFiles::checkpoint(control);
        Fd parent(::open(newDirectory.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        renameExclusive(parent.value, temporary.filename().native(), newDirectory.filename().native());
    } catch (...) {
        std::error_code error;
        fs::remove_all(temporary, error);
        throw;
    }
}
void combine(const std::vector<fs::path> &requested, const fs::path &output,
             const std::shared_ptr<LocalFiles::Control> &control) {
    auto pieces = requested;
    QJsonArray checks;
    QByteArray expected;
    if (pieces.size() == 1 && pieces[0].extension() == ".tcjoin") {
        const auto manifest = pieces[0];
        const auto object =
            QJsonDocument::fromJson(Productivity::readSmall(manifest, 32 * 1024 * 1024)).object();
        if (object.value("format") != "TwinCommander-split-1" || !object.value("parts").isArray())
            throw Error("Invalid split manifest");
        checks = object.value("parts").toArray();
        if (checks.isEmpty() || checks.size() > 100000)
            throw Error("Invalid split manifest piece count");
        expected = object.value("sha256").toString().toLatin1();
        pieces.clear();
        for (const auto &value : checks) {
            const auto leaf = value.toObject().value("name").toString().toUtf8().toStdString();
            fs::path name(leaf);
            if (leaf.empty() || leaf.find('\0') != std::string::npos || name.has_parent_path() ||
                name.is_absolute() || name == "." || name == "..")
                throw Error("Unsafe split manifest");
            pieces.push_back(manifest.parent_path() / name);
        }
    }
    if (pieces.empty() || pieces.size() > 100000)
        throw Error("Select 1 to 100000 ordered pieces");
    Fd parent(::open(output.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    const auto temporary = ".tc-combine-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    try {
        Fd file(::openat(parent.value, temporary.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        QCryptographicHash whole(QCryptographicHash::Sha256);
        std::vector<std::pair<fs::path, struct stat>> snapshots;
        for (size_t i = 0; i < pieces.size(); ++i) {
            const auto before = inspect(pieces[i]);
            if (!S_ISREG(before.st_mode))
                throw Error("Combine requires regular files");
            Fd input(::open(pieces[i].c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
            if (!same(before, inspectFd(input.value)))
                throw Error("Piece changed before opening");
            QCryptographicHash digest(QCryptographicHash::Sha256);
            std::array<char, 128 * 1024> buffer{};
            while (true) {
                LocalFiles::checkpoint(control);
                const auto count = ::read(input.value, buffer.data(), buffer.size());
                if (count < 0 && errno == EINTR)
                    continue;
                check(static_cast<int>(count));
                if (!count)
                    break;
                const QByteArrayView bytes(buffer.data(), count);
                writeAll(file.value, bytes);
                whole.addData(bytes);
                digest.addData(bytes);
                control->bytes += count;
            }
            verify(pieces[i], input.value, before);
            snapshots.emplace_back(pieces[i], before);
            ++control->completed;
            if (!checks.isEmpty()) {
                const auto item = checks[static_cast<int>(i)].toObject();
                if (item.value("bytes").toInteger() != before.st_size ||
                    item.value("sha256").toString().toLatin1() != digest.result().toHex())
                    throw Error("Piece size or checksum mismatch");
            }
        }
        for (const auto &[path, before] : snapshots) {
            LocalFiles::checkpoint(control);
            if (!same(before, inspect(path)))
                throw Error("Piece changed while combining");
        }
        if (!expected.isEmpty() && expected != whole.result().toHex())
            throw Error("Combined checksum mismatch");
        check(::fsync(file.value));
        LocalFiles::checkpoint(control);
        renameExclusive(parent.value, temporary, output.filename().native());
    } catch (...) {
        ::unlinkat(parent.value, temporary.c_str(), 0);
        throw;
    }
}
QByteArray encode(const QByteArray &data, const QString &format, const QByteArray &filename) {
    if (format == "MIME") {
        auto encoded = data.toBase64();
        QByteArray result =
            "Content-Type: application/octet-stream\r\nContent-Transfer-Encoding: base64\r\n\r\n";
        for (qsizetype i = 0; i < encoded.size(); i += 76)
            result += encoded.mid(i, 76) + "\r\n";
        return result;
    }
    if (format != "UUE" && format != "XXE")
        throw Error("Unsupported encoding");
    const QByteArray alphabet =
        format == "XXE" ? QByteArray("+-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
                        : QByteArray("`!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_");
    QByteArray name = filename;
    name.replace('\r', '_');
    name.replace('\n', '_');
    QByteArray result = "begin 644 " + name + "\n";
    for (qsizetype i = 0; i < data.size(); i += 45) {
        const auto line = data.mid(i, 45);
        result += alphabet[line.size()];
        for (qsizetype j = 0; j < line.size(); j += 3) {
            const quint32 word = (static_cast<unsigned char>(line[j]) << 16) |
                                 (j + 1 < line.size() ? static_cast<unsigned char>(line[j + 1]) << 8 : 0) |
                                 (j + 2 < line.size() ? static_cast<unsigned char>(line[j + 2]) : 0);
            for (int shift : {18, 12, 6, 0})
                result += alphabet[(word >> shift) & 63];
        }
        result += '\n';
    }
    return result + alphabet[0] + "\nend\n";
}
QByteArray decode(const QByteArray &input, const QString &format) {
    if (format == "BinHex")
        return binhex(input);
    if (format == "MIME") {
        QByteArray data = input;
        data.replace("\r\n", "\n");
        if (data.startsWith("Content-") || data.startsWith("MIME-")) {
            const auto boundary = data.indexOf("\n\n");
            if (boundary < 0)
                throw Error("MIME body missing");
            data = data.mid(boundary + 2);
        }
        data.replace("\n", "");
        data.replace("\t", "");
        data.replace(" ", "");
        const auto decoded = QByteArray::fromBase64Encoding(data, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            throw Error("Invalid base64 data");
        return decoded.decoded;
    }
    if (format != "UUE" && format != "XXE")
        throw Error("Unsupported encoding");
    const QByteArray alphabet =
        format == "XXE" ? QByteArray("+-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
                        : QByteArray("`!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_");
    QByteArray result;
    bool started = false, ended = false;
    for (auto line : input.split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (!started) {
            if (line.startsWith("begin "))
                started = true;
            continue;
        }
        if (line == "end") {
            ended = true;
            break;
        }
        if (line.isEmpty())
            throw Error("Truncated encoded line");
        const auto digit = [&](char c) {
            if (format == "UUE" && c == ' ')
                return 0;
            const int value = alphabet.indexOf(c);
            if (value < 0)
                throw Error("Invalid encoded character");
            return value;
        };
        const auto count = digit(line[0]);
        if (count > 45 || line.size() < 1 + ((count + 2) / 3) * 4)
            throw Error("Invalid encoded line length");
        QByteArray bytes;
        for (int j = 1; j < 1 + ((count + 2) / 3) * 4; j += 4) {
            const auto word = (digit(line[j]) << 18) | (digit(line[j + 1]) << 12) |
                              (digit(line[j + 2]) << 6) | digit(line[j + 3]);
            bytes += char(word >> 16);
            bytes += char(word >> 8);
            bytes += char(word);
        }
        result += bytes.left(count);
        if (result.size() > 64 * 1024 * 1024)
            throw Error("Decoded-size limit exceeded");
    }
    if (!started || !ended)
        throw Error("Encoding begin/end markers missing");
    return result;
}
LocalFiles::Result packExternal(const std::vector<fs::path> &sources, const fs::path &output,
                                const QString &program, const QStringList &prefix,
                                const std::shared_ptr<LocalFiles::Control> &control) {
    LocalFiles::Result result;
    const auto stage = output.parent_path() /
                       (".tc-external-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    bool created = false;
    try {
        if (sources.empty())
            throw Error("Select source entries");
        const auto root = sources[0].parent_path();
        std::vector<std::pair<fs::path, struct stat>> before;
        quint64 total = 0;
        const auto inspectSource = [&](const fs::path &path) {
            LocalFiles::checkpoint(control);
            const auto st = inspect(path);
            if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
                throw Error("External packing refuses links and special entries");
            if (st.st_size > 512LL * 1024 * 1024 && S_ISREG(st.st_mode))
                throw Error("External packing file-size limit exceeded");
            if (S_ISREG(st.st_mode))
                total += st.st_size;
            if (total > 4ULL * 1024 * 1024 * 1024 || before.size() >= 100000)
                throw Error("External packing size/count limit exceeded");
            before.emplace_back(path, st);
        };
        for (const auto &path : sources) {
            if (path.parent_path() != root)
                throw Error("External packing requires sources in one directory");
            if (fs::is_directory(fs::symlink_status(path)) &&
                inside(fs::weakly_canonical(output.parent_path()), fs::canonical(path)))
                throw Error("Archive output must be outside selected directories");
            inspectSource(path);
            if (S_ISDIR(before.back().second.st_mode))
                for (auto it = fs::recursive_directory_iterator(path);
                     it != fs::recursive_directory_iterator(); ++it) {
                    if (it.depth() > 128)
                        throw Error("External packing depth limit exceeded");
                    inspectSource(it->path());
                }
        }
        check(::mkdir(stage.c_str(), 0700));
        created = true;
        const auto archive = stage / output.filename();
        QStringList arguments = prefix;
        bool substituted = false;
        for (auto &argument : arguments)
            if (argument.contains("{archive}")) {
                argument.replace("{archive}", displayPath(archive));
                substituted = true;
            }
        if (!substituted)
            arguments << displayPath(archive);
        for (const auto &path : sources)
            arguments << "./" + displayPath(path.filename());
        QProcess process;
        process.setWorkingDirectory(displayPath(root));
        process.start(program, arguments);
        if (!process.waitForStarted(3000))
            throw Error("External archiver could not start");
        QElapsedTimer timer;
        timer.start();
        QByteArray diagnostics;
        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(50);
            diagnostics += process.readAllStandardOutput();
            diagnostics += process.readAllStandardError();
            if (control->cancelled || timer.elapsed() > 300000 || diagnostics.size() > 1024 * 1024) {
                process.kill();
                process.waitForFinished();
                if (control->cancelled)
                    throw LocalFiles::Cancelled{};
                throw Error("External archiver time/output limit exceeded");
            }
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            throw Error(
                ("External archiver failed: " + QString::fromUtf8(diagnostics.right(8192))).toStdString());
        for (const auto &[path, st] : before)
            if (!same(st, inspect(path)))
                throw Error("Source changed during external packing");
        if (!S_ISREG(inspect(archive).st_mode))
            throw Error("External archiver did not produce the requested file");
        LocalFiles::checkpoint(control);
        Fd parent(::open(output.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        renameExclusive(parent.value, (stage.filename() / archive.filename()).native(),
                        output.filename().native());
        result.items.push_back({sources[0], output, {}, false});
    } catch (const LocalFiles::Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.items.push_back({{}, output, QString::fromLocal8Bit(error.what()), false});
    }
    if (created) {
        std::error_code error;
        fs::remove_all(stage, error);
    }
    return result;
}
QString comment(const fs::path &path) {
    const auto size = ::lgetxattr(path.c_str(), "user.twincommander.comment", nullptr, 0);
    if (size < 0)
        return {};
    if (size > 65536)
        return {};
    QByteArray data(size, 0);
    if (::lgetxattr(path.c_str(), "user.twincommander.comment", data.data(), data.size()) < 0)
        return {};
    return QString::fromUtf8(data);
}
void setComment(const fs::path &path, const QString &text) {
    const auto data = text.toUtf8();
    if (data.size() > 65536)
        throw Error("Comment limit is 64 KiB");
    check(::lsetxattr(path.c_str(), "user.twincommander.comment", data.data(), data.size(), 0));
}
} // namespace ExtendedTools
