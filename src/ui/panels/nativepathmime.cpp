#include "ui/panels/nativepathmime.h"
#include <QDataStream>
#include <QIODevice>

QByteArray NativePathMime::mimeType() { return "application/x-twincommander-native-paths"; }
QMimeData *NativePathMime::encode(const std::vector<std::filesystem::path> &paths) {
    auto *mime = new QMimeData;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << quint32(paths.size());
    for (const auto &path : paths)
        stream << QByteArray::fromStdString(path.native());
    mime->setData(mimeType(), data);
    return mime;
}
std::vector<std::filesystem::path> NativePathMime::decode(const QMimeData *data) {
    std::vector<std::filesystem::path> paths;
    const auto bytes = data->data(mimeType());
    if (bytes.size() > 4 * 1024 * 1024)
        return {};
    QDataStream stream(bytes);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 10000)
        return {};
    for (quint32 i = 0; i < count; ++i) {
        QByteArray path;
        stream >> path;
        if (stream.status() != QDataStream::Ok || path.contains('\0') || !path.startsWith('/'))
            return {};
        paths.emplace_back(path.toStdString());
    }
    return paths;
}
