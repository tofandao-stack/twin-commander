#include "directorymodel.h"
#include "backgroundpool.h"
#include "classicui.h"
#include "extendedtools.h"
#include "finalsettings.h"
#include "nativesettings.h"
#include "preferences.h"
#include <QBuffer>
#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <sys/xattr.h>
#include <unordered_set>

#include <QColor>
#include <QFile>
#include <QFutureWatcher>
#include <QIcon>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <unordered_map>

namespace {
struct Listing {
    std::vector<DirectoryEntry> entries;
    std::filesystem::path path;
    QString error;
};

// Only two metadata batches may wait for the UI thread at a time.
struct BatchQueue {
    std::mutex mutex;
    std::condition_variable available;
    std::deque<std::vector<DirectoryEntry>> batches;
};
constexpr size_t batchSize = 256;

void sortEntries(std::vector<DirectoryEntry> &entries, int column, Qt::SortOrder order,
                 const QStringList &fields = {}, int secondary = 0) {
    QSettings settings;
    const bool directoriesByName = settings.value("display/directorySort", 1).toInt() == 0;
    const int method = settings.value("display/sortMethod", 0).toInt();
    QCollator collator(QLocale(QLocale::English));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const auto compareName = [&](const QString &a, const QString &b) {
        return method == 2 ? collator.compare(a, b)
                           : QString::compare(a, b, method == 1 ? Qt::CaseInsensitive : Qt::CaseSensitive);
    };
    std::stable_sort(
        entries.begin(), entries.end(),
        [column, order, fields, secondary, directoriesByName, &compareName](const auto &left,
                                                                            const auto &right) {
            if (left.parent != right.parent)
                return left.parent;
            if (column == -1)
                return left.enumerationOrder < right.enumerationOrder;
            if (left.directory != right.directory)
                return left.directory;
            if (left.directory && right.directory && directoriesByName)
                return compareName(left.name, right.name) < 0;
            int comparison = column >= 5 ? QString::compare(left.fields.value(fields.value(column - 5)),
                                                            right.fields.value(fields.value(column - 5)),
                                                            Qt::CaseInsensitive)
                                         : 0;
            if (column == 1)
                comparison = QString::compare(displayPath(left.path.extension()),
                                              displayPath(right.path.extension()), Qt::CaseInsensitive);
            if (column == 2)
                comparison = left.size < right.size ? -1 : (left.size > right.size ? 1 : 0);
            if (column == 4)
                comparison = static_cast<int>(left.permissions) - static_cast<int>(right.permissions);
            if (column == 3)
                comparison = left.modified < right.modified ? -1 : (left.modified > right.modified ? 1 : 0);
            if (comparison == 0 && secondary != column) {
                if (secondary == 1)
                    comparison = QString::compare(displayPath(left.path.extension()),
                                                  displayPath(right.path.extension()), Qt::CaseInsensitive);
                if (secondary == 2)
                    comparison = left.size < right.size ? -1 : left.size > right.size ? 1 : 0;
                if (secondary == 3)
                    comparison = left.modified < right.modified ? -1 : left.modified > right.modified ? 1 : 0;
            }
            if (comparison == 0)
                comparison = compareName(left.name, right.name);
            return order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        });
}

Listing enumerate(const std::filesystem::path &path, bool hidden, const QString &filter, const QString &mask,
                  bool programs, bool restrictPaths, const std::vector<std::filesystem::path> &paths,
                  bool resultMode, const std::vector<std::filesystem::path> &results,
                  const QStringList &fields, bool thumbnails, const QStringList &ignore, const QString &cache,
                  const std::shared_ptr<std::atomic_bool> &cancelled,
                  const std::shared_ptr<BatchQueue> &queue) {
    const QSize thumbnailSize(qBound(16, QSettings().value("thumbnail/width", 128).toInt(), 512),
                              qBound(16, QSettings().value("thumbnail/height", 96).toInt(), 512));
    const auto typeFields = QSettings().value("thumbnail/typeFields").toList();
    Listing listing;
    std::error_code error;
    listing.path = std::filesystem::canonical(path, error);
    if (error) {
        listing.error = QString::fromStdString(error.message());
        return listing;
    }
    std::filesystem::directory_iterator iterator(listing.path, error);
    if (error) {
        listing.error = QString::fromStdString(error.message());
        return listing;
    }
    const auto deliver = [&] {
        std::unique_lock lock(queue->mutex);
        while (queue->batches.size() >= 2 && !cancelled->load())
            queue->available.wait_for(lock, std::chrono::milliseconds(25));
        if (cancelled->load())
            return false;
        queue->batches.push_back(std::move(listing.entries));
        listing.entries = {};
        return true;
    };
    const std::unordered_set<std::filesystem::path> allowed(paths.begin(), paths.end());
    quint64 ordinal = 0;
    const std::filesystem::directory_iterator end;
    size_t resultIndex = 0;
    while (resultMode ? resultIndex < results.size() : iterator != end) {
        if (cancelled->load())
            return listing;
        const auto item =
            resultMode ? std::filesystem::directory_entry(results[resultIndex], error) : *iterator;
        if (error) {
            error.clear();
            ++resultIndex;
            continue;
        }
        const auto filename = item.path().filename().native();
        char dos[64]{};
        const auto dosBytes = ::lgetxattr(item.path().c_str(), "user.DOSATTRIB", dos, sizeof(dos) - 1);
        bool isSystem = false;
        if (dosBytes > 0) {
            bool valid = false;
            isSystem = (QString::fromLatin1(dos, dosBytes).toUInt(&valid, 0) & 4) && valid;
        }
        if (isSystem && !FinalSettings::flag("IdConfshowSystem")) {
            if (resultMode)
                ++resultIndex;
            else
                iterator.increment(error);
            continue;
        }
        if ((resultMode || hidden || filename.empty() || filename.front() != '.') &&
            (filter.isEmpty() || displayPath(item.path().filename()).contains(filter, Qt::CaseInsensitive))) {
            DirectoryEntry entry;
            entry.enumerationOrder = ordinal++;
            entry.path = item.path();
            entry.name = resultMode ? displayPath(item.path().lexically_relative(listing.path))
                                    : displayPath(item.path().filename());
            entry.permissions = item.symlink_status(error).permissions();
            error.clear();
            entry.symlink = item.is_symlink(error);
            error.clear();
            entry.directory = item.is_directory(error);
            error.clear();
            const bool regular = item.is_regular_file(error);
            if (regular) {
                entry.size = item.file_size(error);
                if (error)
                    entry.size = 0;
            }
            error.clear();
            const auto modified = item.last_write_time(error);
            if (!error) {
                const auto systemTime = std::chrono::file_clock::to_sys(modified);
                entry.modified = QDateTime::fromMSecsSinceEpoch(
                    std::chrono::duration_cast<std::chrono::milliseconds>(systemTime.time_since_epoch())
                        .count());
            }
            error.clear();
            const auto executable = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                    std::filesystem::perms::others_exec;
            const bool matches = (mask.isEmpty() || entry.directory ||
                                  QDir::match(mask.split(';', Qt::SkipEmptyParts), entry.name)) &&
                                 (!programs || entry.directory ||
                                  (regular && !entry.symlink &&
                                   (entry.permissions & executable) != std::filesystem::perms::none)) &&
                                 (!restrictPaths || allowed.contains(entry.path));
            bool ignored = false;
            for (const auto &pattern : ignore)
                if (QDir::match(pattern, entry.name) || QDir::match(pattern, displayPath(entry.path)))
                    ignored = true;
            if (matches && !ignored) {
                auto requestedFields = fields;
                if (thumbnails)
                    for (const auto &value : typeFields) {
                        const auto rule = value.toMap();
                        if (QDir::match(rule.value("mask", "*").toString(), entry.name))
                            requestedFields.append(
                                rule.value("fields").toString().split(';', Qt::SkipEmptyParts));
                    }
                requestedFields.removeDuplicates();
                for (const auto &field : requestedFields) {
                    if (field == "path")
                        entry.fields[field] = displayPath(entry.path);
                    else if (field == "permissions")
                        entry.fields[field] =
                            QString::number(static_cast<unsigned>(entry.permissions) & 07777, 8);
                    else if (field == "owner") {
                        struct stat info {};
                        if (::lstat(entry.path.c_str(), &info) == 0)
                            entry.fields[field] = QString::number(info.st_uid);
                    } else
                        try {
                            entry.fields[field] = NativeSettings::field(entry.path, field);
                        } catch (const std::exception &error) {
                            entry.fields[field] = QString("ERROR: ") + QString::fromLocal8Bit(error.what());
                        }
                }
                if (thumbnails && QSettings().value("thumbnail/showFields", true).toBool())
                    for (const auto &key : requestedFields)
                        if (!entry.fields.value(key).isEmpty())
                            entry.thumbnailText +=
                                (entry.thumbnailText.isEmpty() ? QString() : "\n") + entry.fields.value(key);
                if (thumbnails && QSettings().value("thumbnail/images", true).toBool() &&
                    QDir::match(QSettings()
                                    .value("thumbnail/imageMasks",
                                           "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.tif;*.tiff;*.ico")
                                    .toString()
                                    .split(';'),
                                entry.name) &&
                    regular && !entry.symlink && entry.size <= 8 * 1024 * 1024) {
                    try {
                        const auto st = NativeFs::inspect(entry.path);
                        const auto identity =
                            QByteArray::fromStdString(entry.path.native()) + '\0' +
                            QByteArray::number(entry.size) + ":" +
                            QByteArray::number(static_cast<qint64>(st.st_mtim.tv_sec)) + ":" +
                            QByteArray::number(static_cast<qint64>(st.st_mtim.tv_nsec)) + ":" +
                            QByteArray::number(static_cast<qint64>(st.st_ctim.tv_sec)) + ":" +
                            QByteArray::number(static_cast<qint64>(st.st_ctim.tv_nsec)) + ":" +
                            QByteArray::number(thumbnailSize.width()) + "x" +
                            QByteArray::number(thumbnailSize.height());
                        const auto key = QString::fromLatin1(
                            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
                        const auto base = cache + "/tc-thumb-" + key;
                        if (!cache.isEmpty()) {
                            QImageReader cached(base + ".png");
                            if (cached.size().width() <= thumbnailSize.width() &&
                                cached.size().height() <= thumbnailSize.height())
                                entry.thumbnail = cached.read();
                        }
                        if (entry.thumbnail.isNull()) {
                            const auto bytes = Productivity::readSmall(entry.path);
                            QBuffer buffer;
                            buffer.setData(bytes);
                            buffer.open(QIODevice::ReadOnly);
                            QImageReader reader(&buffer);
                            const auto original = reader.size();
                            if (original.isValid() &&
                                static_cast<quint64>(original.width()) * original.height() <=
                                    16 * 1024 * 1024) {
                                reader.setScaledSize(original.scaled(thumbnailSize, Qt::KeepAspectRatio));
                                entry.thumbnail = reader.read();
                            }
                        }
                        if (!cache.isEmpty() && !entry.thumbnail.isNull()) {
                            QDir().mkpath(cache);
                            QSaveFile image(base + ".png");
                            if (image.open(QIODevice::WriteOnly) && entry.thumbnail.save(&image, "PNG"))
                                image.commit();
                            QSaveFile metadata(base + ".json");
                            const auto json =
                                QJsonDocument(
                                    QJsonObject{
                                        {"format", "TwinCommander-thumbnail-1"},
                                        {"width", thumbnailSize.width()},
                                        {"height", thumbnailSize.height()},
                                        {"volume", QStorageInfo(displayPath(entry.path)).rootPath()},
                                        {"path",
                                         QString::fromLatin1(
                                             QByteArray::fromStdString(entry.path.native()).toBase64())}})
                                    .toJson();
                            if (metadata.open(QIODevice::WriteOnly) && metadata.write(json) == json.size())
                                metadata.commit();
                        }
                    } catch (const std::exception &) {
                    }
                }
                if (thumbnails && QSettings().value("thumbnail/text").toBool() && regular && !entry.symlink &&
                    QDir::match(QSettings()
                                    .value("thumbnail/textMasks", "*.txt;*.md;*.log;*.csv;*.json")
                                    .toString()
                                    .split(';'),
                                entry.name)) {
                    try {
                        NativeFs::Fd input(
                            ::open(entry.path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
                        const auto st = NativeFs::inspectFd(input.value);
                        if (S_ISREG(st.st_mode)) {
                            char bytes[4096];
                            const auto n = ::read(input.value, bytes, sizeof(bytes));
                            if (n >= 0 && !QByteArray(bytes, n).contains('\0')) {
                                QImage preview(thumbnailSize, QImage::Format_ARGB32_Premultiplied);
                                preview.fill(Qt::white);
                                QPainter painter(&preview);
                                QFont font("Monospace");
                                font.setPixelSize(10);
                                painter.setFont(font);
                                painter.setPen(Qt::black);
                                painter.drawText(preview.rect().adjusted(4, 4, -4, -4),
                                                 Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                                 QString::fromUtf8(bytes, n));
                                painter.end();
                                entry.thumbnail = preview;
                            }
                        }
                    } catch (const std::exception &) {
                    }
                }
                if (thumbnails) {
                    const auto preview = FinalSettings::externalThumbnail(entry.path, thumbnailSize);
                    if (!preview.isNull())
                        entry.thumbnail = preview;
                }
                listing.entries.push_back(std::move(entry));
            }
            if (listing.entries.size() == batchSize && !deliver())
                return listing;
        }
        if (resultMode)
            ++resultIndex;
        else
            iterator.increment(error);
        if (error) {
            listing.error = QString::fromStdString(error.message());
            return listing;
        }
    }
    if (!listing.entries.empty())
        deliver();
    return listing;
}
} // namespace

QString displayPath(const std::filesystem::path &path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(QByteArray::fromStdString(path.native()));
#endif
}

std::filesystem::path nativePath(const QString &path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}

DirectoryModel::DirectoryModel(QObject *parent) : QAbstractTableModel(parent) {}
DirectoryModel::~DirectoryModel() {
    if (cancellation_)
        cancellation_->store(true);
}

int DirectoryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

int DirectoryModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 5 + columns_.size();
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const {
    const auto *item = entry(index.row());
    if (!index.isValid() || !item)
        return {};
    if (role == Qt::DecorationRole && index.column() == 0 && !item->thumbnail.isNull())
        return QIcon(QPixmap::fromImage(item->thumbnail));
    if (role == Qt::DecorationRole && index.column() == 0) {
        const int mode = QSettings().value("icons/mode", 1).toInt();
        if (mode == 2)
            return {};
        const int sizes[]{16, 20, 24, 32, 48};
        const int size = sizes[qBound(0, QSettings().value("icons/size", 0).toInt(), 4)];
        QIcon icon;
        if ((mode == 0 || mode == 3 || QSettings().value("icons/nativeDefaults").toBool() ||
             (item->directory && QSettings().value("icons/specialFolders").toBool())) &&
            nativePath(displayPath(item->path)) == item->path) {
            static QFileIconProvider provider;
            provider.setOptions(QSettings().value("icons/specialFolders").toBool()
                                    ? QFileIconProvider::Options{}
                                    : QFileIconProvider::DontUseCustomDirectoryIcons);
            icon = provider.icon(QFileInfo(displayPath(item->path)));
        }
        const QStorageInfo volume(displayPath(item->path));
        const auto fs = volume.fileSystemType();
        const bool net = fs.contains("nfs") || fs.contains("cifs") || fs.contains("sshfs");
        const bool removable =
            volume.rootPath().startsWith("/media/") || volume.rootPath().startsWith("/run/media/");
        if (mode == 3 && !item->directory && !(net && FinalSettings::flag("NotOnNet")) &&
            !(removable && FinalSettings::flag("NotOnFloppy"))) {
            if (item->path.extension() == ".desktop" && item->size <= 65536 && !item->symlink) {
                QSettings desktop(displayPath(item->path), QSettings::IniFormat);
                const auto name = desktop.value("Desktop Entry/Icon").toString();
                if (!name.contains('/'))
                    icon = QIcon::fromTheme(name, icon);
            } else if ((item->permissions & std::filesystem::perms::owner_exec) !=
                       std::filesystem::perms::none)
                icon = QIcon::fromTheme("application-x-executable", icon);
        }
        if (item->directory && net && !FinalSettings::flag("IdShowSpecialFoldersRemote"))
            icon = commanderIcon("folder", size);
        if (icon.isNull())
            icon = commanderIcon(item->parent ? "parent" : item->directory ? "folder" : "file", size);
        if ((item->symlink && QSettings().value("icons/overlay", true).toBool()) ||
            QSettings().value("icons/frame", false).toBool() ||
            (QSettings().value("icons/hiddenBadge").toBool() && item->name.startsWith('.'))) {
            auto pixmap = icon.pixmap(size, size);
            QPainter painter(&pixmap);
            if (QSettings().value("icons/frame", false).toBool()) {
                painter.setPen(Qt::gray);
                painter.drawRect(0, 0, size - 1, size - 1);
            }
            if (item->symlink && QSettings().value("icons/overlay", true).toBool()) {
                painter.setPen(Qt::blue);
                painter.drawText(QRect(0, size / 2, size, size / 2), Qt::AlignLeft | Qt::AlignBottom,
                                 QString::fromUtf8("↗"));
            }
            if (QSettings().value("icons/hiddenBadge").toBool() && item->name.startsWith('.')) {
                painter.setPen(Qt::red);
                painter.drawText(QRect(0, 0, size, size), Qt::AlignRight | Qt::AlignBottom, "!");
            }
            return QIcon(pixmap);
        }
        return icon;
    }
    if (role == Qt::ToolTipRole) {
        if (!QSettings().value("display/nameTips", true).toBool() && !FinalSettings::flag("IdConf_Win32Tips"))
            return {};
        QString tip = displayPath(item->path) + (item->symlink ? "\nSymbolic link" : "");
        if (FinalSettings::flag("IdConf_Win32Tips"))
            tip += "\n" + FinalSettings::comment(item->path);
        for (const auto &entry : QSettings().value("hintRules").toList()) {
            const auto rule = entry.toMap();
            if (QDir::match(rule.value("mask", "*").toString(), displayPath(item->path.filename()))) {
                auto text = rule.value("template").toString();
                text.replace("{name}", item->name)
                    .replace("{path}", displayPath(item->path))
                    .replace("{size}", QString::number(item->size))
                    .replace("{comment}", ExtendedTools::comment(item->path));
                tip += "\n" + text;
            }
        }
        return tip;
    }
    if (role == Qt::UserRole + 1)
        return item->thumbnailText;
    if ((role == Qt::ForegroundRole || role == Qt::BackgroundRole) &&
        QSettings().value("appearance/typeColors", true).toBool())
        for (const auto &entry : QSettings().value("colorRules").toList()) {
            const auto rule = entry.toMap();
            if (QDir::match(rule.value("mask", "*").toString(), displayPath(item->path.filename()))) {
                const QColor color(
                    rule.value(role == Qt::ForegroundRole ? "foreground" : "background").toString());
                if (color.isValid())
                    return color;
            }
        }
    const auto displayName = [&] {
        QString name = item->name;
        if (!item->parent && !FinalSettings::flag("IdConf_longnames"))
            name = FinalSettings::alias83(name);
        else if (!item->parent && FinalSettings::flag("IdConfLower83Names") &&
                 QRegularExpression("^[A-Z0-9_$~-]{1,8}(\\.[A-Z0-9_$~-]{1,3})?$").match(name).hasMatch())
            name = name.toLower();
        if (item->directory && !item->parent) {
            if (QSettings().value("display/brackets").toBool())
                name = "[" + name + "]";
            if (QSettings().value("display/directorySlash").toBool())
                name += '/';
        }
        return name;
    };
    if (role == Qt::UserRole)
        return displayName();
    if (role == Qt::ForegroundRole && !item->parent && displayPath(item->path.filename()).startsWith('.') &&
        QSettings().value("icons/dimHidden").toBool())
        return QColor(Qt::gray);
    if (role == Qt::ForegroundRole && item->symlink)
        return QColor(30, 70, 150);
    if (role == Qt::TextAlignmentRole && index.column() == 2)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    const auto basename = displayPath(item->path.filename());
    const auto shown = displayName();
    const int dot = shown.lastIndexOf('.');
    const bool split = !item->directory && dot > shown.lastIndexOf('/') + 1 &&
                       QSettings().value("format/extension", 1).toInt() == 1;
    switch (index.column()) {
    case 0:
        return split ? shown.left(dot) : shown;
    case 1:
        return split ? shown.mid(dot + 1) : QString();
    case 2:
        return item->directory && !item->directorySizeKnown ? QVariant("<DIR>")
                                                            : QVariant(Preferences::sizeText(item->size));
    case 3:
        return item->parent ? QString() : Preferences::dateText(item->modified);
    case 4: {
        if (item->parent)
            return QString();
        const auto write = std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                           std::filesystem::perms::others_write;
        const bool readonly = item->permissions != std::filesystem::perms::unknown &&
                              (item->permissions & write) == std::filesystem::perms::none;
        return QString("%1-%2-")
            .arg(QChar(readonly ? 'r' : '-'))
            .arg(QChar(basename.startsWith('.') ? 'h' : '-'));
    }
    default:
        return item->fields.value(columns_.value(index.column() - 5));
    }
}

QVariant DirectoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    QStringList labels{"Name", "Ext", "Size", "Date", "Attr"};
    labels.append(columns_);
    return section >= 0 && section < labels.size() ? QVariant(FinalSettings::translated(labels[section]))
                                                   : QVariant();
}

void DirectoryModel::sort(int column, Qt::SortOrder order) {
    if (column < -1 || column >= columnCount())
        return;
    sortColumn_ = column;
    sortOrder_ = order;
    emit layoutAboutToBeChanged();
    const auto oldIndexes = persistentIndexList();
    std::vector<std::filesystem::path> oldPaths;
    for (const auto &index : oldIndexes)
        oldPaths.push_back(entries_[index.row()].path);
    sortEntries(entries_, column, order, columns_, secondarySort_);
    QModelIndexList newIndexes;
    std::unordered_map<std::filesystem::path, int> positions;
    if (!oldIndexes.empty()) {
        positions.reserve(entries_.size());
        for (int row = 0; row < rowCount(); ++row)
            positions.emplace(entries_[row].path, row);
    }
    for (qsizetype offset = 0; offset < oldIndexes.size(); ++offset) {
        newIndexes.push_back(index(positions.at(oldPaths[offset]), oldIndexes[offset].column()));
    }
    changePersistentIndexList(oldIndexes, newIndexes);
    emit layoutChanged();
}

void DirectoryModel::load(const std::filesystem::path &path) {
    if (cancellation_)
        cancellation_->store(true);
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++generation_;
    auto oldOrder = std::make_shared<std::unordered_map<std::filesystem::path, size_t>>();
    if (path == path_ && QSettings().value("refresh/append").toBool())
        for (size_t i = 0; i < entries_.size(); ++i)
            (*oldOrder)[entries_[i].path] = i;
    auto queue = std::make_shared<BatchQueue>();
    auto staged = std::make_shared<std::vector<DirectoryEntry>>();
    struct SortTask {
        QFuture<void> future;
        bool started = false;
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;
    };
    auto sorting = std::make_shared<SortTask>();
    auto *watcher = new QFutureWatcher<Listing>(this);
    auto *pump = new QTimer(watcher);
    pump->setInterval(1);
    connect(pump, &QTimer::timeout, this,
            [this, watcher, pump, queue, staged, sorting, generation, path, oldOrder] {
                if (generation != generation_) {
                    pump->stop();
                    watcher->deleteLater();
                    return;
                }
                std::vector<DirectoryEntry> batch;
                bool empty = false;
                {
                    std::lock_guard lock(queue->mutex);
                    if (!queue->batches.empty()) {
                        batch = std::move(queue->batches.front());
                        queue->batches.pop_front();
                    }
                    empty = queue->batches.empty();
                }
                queue->available.notify_one();
                if (!batch.empty()) {
                    const int count = static_cast<int>(batch.size());
                    staged->insert(staged->end(), std::make_move_iterator(batch.begin()),
                                   std::make_move_iterator(batch.end()));
                    emit batchReceived(count, static_cast<int>(staged->size()));
                    // A signal handler may request a different directory.
                    if (generation != generation_)
                        return;
                }
                if (!empty || !watcher->isFinished())
                    return;
                // The worker can publish its final batch between the drain and finishing.
                {
                    std::lock_guard lock(queue->mutex);
                    if (!queue->batches.empty())
                        return;
                }
                auto result = watcher->result();
                if (!result.error.isEmpty()) {
                    pump->stop();
                    watcher->deleteLater();
                    emit loadFailed(QString("Cannot open %1: %2").arg(displayPath(path), result.error));
                    return;
                }
                if (sorting->started && !sorting->future.isFinished())
                    return;
                if (!sorting->started || sorting->column != sortColumn_ || sorting->order != sortOrder_) {
                    if (!sorting->started && showParent_ &&
                        (result.path != result.path.parent_path() ||
                         QSettings().value("display/rootParent").toBool())) {
                        DirectoryEntry parent;
                        parent.path = result.path.parent_path();
                        parent.name = "..";
                        parent.directory = true;
                        parent.parent = true;
                        staged->push_back(std::move(parent));
                    }
                    sorting->started = true;
                    sorting->column = sortColumn_;
                    sorting->order = sortOrder_;
                    sorting->future = QtConcurrent::run(ioPool(), [staged, column = sortColumn_,
                                                                   order = sortOrder_, fields = columns_,
                                                                   secondary = secondarySort_, oldOrder] {
                        sortEntries(*staged, column, order, fields, secondary);
                        if (!oldOrder->empty())
                            std::stable_sort(
                                staged->begin(), staged->end(), [oldOrder](const auto &a, const auto &b) {
                                    if (a.parent != b.parent)
                                        return a.parent;
                                    const auto ai = oldOrder->find(a.path), bi = oldOrder->find(b.path);
                                    const auto ar = ai == oldOrder->end() ? SIZE_MAX : ai->second,
                                               br = bi == oldOrder->end() ? SIZE_MAX : bi->second;
                                    return ar < br;
                                });
                    });
                    return;
                }
                pump->stop();
                watcher->deleteLater();
                beginResetModel();
                entries_ = std::move(*staged);
                path_ = std::move(result.path);
                endResetModel();
                emit loaded();
            });
    emit loading();
    auto fields = columns_;
    if (thumbnails_)
        fields.append(QSettings().value("thumbnail/fields").toString().split(';', Qt::SkipEmptyParts));
    fields.removeDuplicates();
    watcher->setFuture(QtConcurrent::run(
        ioPool(), enumerate, path, showHidden_, nameFilter_, mask_, programs_, restrictPaths_, onlyPaths_,
        resultMode_, resultPaths_, fields, thumbnails_,
        QSettings().value("ignore/enabled").toBool() ? QSettings().value("ignore/patterns").toStringList()
                                                     : QStringList{},
        QSettings().value("thumbnail/cache").toString(), cancellation_, queue));
    pump->start();
}

void DirectoryModel::setShowParent(bool enabled) { showParent_ = enabled; }
void DirectoryModel::setNameFilter(const QString &filter) { nameFilter_ = filter; }
void DirectoryModel::setShowHidden(bool enabled) { showHidden_ = enabled; }
bool DirectoryModel::showHidden() const { return showHidden_; }
const DirectoryEntry *DirectoryModel::entry(int row) const {
    return row >= 0 && row < rowCount() ? &entries_[row] : nullptr;
}
std::filesystem::path DirectoryModel::path() const { return path_; }

void DirectoryModel::setViewFilter(const QString &mask, bool programs,
                                   const std::vector<std::filesystem::path> *only) {
    mask_ = mask;
    programs_ = programs;
    restrictPaths_ = only != nullptr;
    onlyPaths_ = only ? *only : std::vector<std::filesystem::path>{};
}

void DirectoryModel::setResultPaths(const std::vector<std::filesystem::path> *paths) {
    resultMode_ = paths != nullptr;
    resultPaths_ = paths ? *paths : std::vector<std::filesystem::path>{};
}

void DirectoryModel::setColumns(const QStringList &fields) {
    const int before = columns_.size(), after = fields.size();
    if (after < before) {
        beginRemoveColumns({}, 5 + after, 4 + before);
        columns_ = fields;
        endRemoveColumns();
    } else if (after > before) {
        beginInsertColumns({}, 5 + before, 4 + after);
        columns_ = fields;
        endInsertColumns();
    } else
        columns_ = fields;
    if (sortColumn_ >= columnCount())
        sortColumn_ = 0;
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    if (rowCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}
void DirectoryModel::setThumbnails(bool enabled) {
    const bool previous = thumbnails_;
    thumbnails_ = enabled;
    if (previous && !enabled) {
        for (auto &entry : entries_) {
            entry.thumbnail = QImage();
            entry.thumbnailText.clear();
        }
        if (rowCount())
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DecorationRole});
    }
}

void DirectoryModel::setSecondarySort(int column) {
    secondarySort_ = column >= 0 && column < 4 ? column : 0;
    sort(sortColumn_, sortOrder_);
}

void DirectoryModel::setDirectorySize(const std::filesystem::path &path, quint64 size) {
    for (int row = 0; row < rowCount(); ++row)
        if (entries_[row].path == path) {
            entries_[row].size = size;
            entries_[row].directorySizeKnown = true;
            emit dataChanged(index(row, 2), index(row, 2));
            break;
        }
}
