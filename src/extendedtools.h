#pragma once
#include "productivitytools.h"
namespace ExtendedTools {
void split(const std::filesystem::path &source, const std::filesystem::path &newDirectory, quint64 pieceBytes,
           const std::shared_ptr<LocalFiles::Control> &control);
void combine(const std::vector<std::filesystem::path> &pieces, const std::filesystem::path &output,
             const std::shared_ptr<LocalFiles::Control> &control);
QByteArray encode(const QByteArray &data, const QString &format, const QByteArray &filename);
QByteArray decode(const QByteArray &data, const QString &format);
LocalFiles::Result packExternal(const std::vector<std::filesystem::path> &sources,
                                const std::filesystem::path &output, const QString &program,
                                const QStringList &prefix,
                                const std::shared_ptr<LocalFiles::Control> &control,
                                const QByteArray &standardInput = {});
QString comment(const std::filesystem::path &path);
void setComment(const std::filesystem::path &path, const QString &text);
} // namespace ExtendedTools
