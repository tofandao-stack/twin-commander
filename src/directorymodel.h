#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QImage>
#include <QMap>
#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

struct DirectoryEntry {
    std::filesystem::path path;
    QString name;
    QImage thumbnail;
    QString thumbnailText;
    QMap<QString, QString> fields;
    bool directory = false;
    bool symlink = false;
    quint64 size = 0, enumerationOrder = 0;
    QDateTime modified;
    bool parent = false;
    bool directorySizeKnown = false;
    std::filesystem::perms permissions = std::filesystem::perms::unknown;
};

class DirectoryModel : public QAbstractTableModel {
    Q_OBJECT

  public:
    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    void load(const std::filesystem::path &path);
    void setShowHidden(bool enabled);
    void setNameFilter(const QString &filter);
    void setShowParent(bool enabled);
    void setSecondarySort(int column);
    void setDirectorySize(const std::filesystem::path &path, quint64 size);
    void setColumns(const QStringList &fields);
    void setThumbnails(bool enabled);
    void setResultPaths(const std::vector<std::filesystem::path> *paths);
    void setViewFilter(const QString &mask = {}, bool programs = false,
                       const std::vector<std::filesystem::path> *only = nullptr);
    const DirectoryEntry *entry(int row) const;
    std::filesystem::path path() const;
    bool showHidden() const;

  signals:
    void loaded();
    void loadFailed(const QString &message);
    void loading();
    void batchReceived(int count, int total);

  private:
    std::vector<DirectoryEntry> entries_;
    std::filesystem::path path_;
    quint64 generation_ = 0;
    int sortColumn_ = 0, secondarySort_ = 0;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    bool showHidden_ = false;
    bool showParent_ = false;
    QString nameFilter_, mask_;
    bool programs_ = false, restrictPaths_ = false;
    std::vector<std::filesystem::path> onlyPaths_, resultPaths_;
    bool resultMode_ = false, thumbnails_ = false;
    QStringList columns_;
    std::shared_ptr<std::atomic_bool> cancellation_;
};

QString displayPath(const std::filesystem::path &path);
std::filesystem::path nativePath(const QString &path);
