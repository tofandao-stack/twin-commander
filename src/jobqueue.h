#pragma once
#include "fileoperations.h"
#include <QDialog>
#include <QObject>
#include <functional>

class QTableWidget;
class JobQueue : public QObject {
    Q_OBJECT
  public:
    enum class State { Waiting, Running, Paused, Completed, Failed, Cancelled };
    struct Job {
        quint64 id;
        QString label;
        State state = State::Waiting;
        std::shared_ptr<LocalFiles::Control> control;
        LocalFiles::Result result;
        std::function<LocalFiles::Result(const std::shared_ptr<LocalFiles::Control> &)> work;
        std::function<void(const LocalFiles::Result &)> completion;
        QString reportName;
        qint64 startedAt = 0;
    };
    explicit JobQueue(QObject *parent = nullptr);
    ~JobQueue() override;
    quint64 enqueue(const QString &label,
                    std::function<LocalFiles::Result(const std::shared_ptr<LocalFiles::Control> &)> work,
                    std::function<void(const LocalFiles::Result &)> completion = {});
    const std::vector<Job> &jobs() const;
    bool busy() const;
    void cancel(quint64 id);
    void pause(quint64 id, bool paused);
    void moveWaiting(quint64 id, int direction);
    void clearFinished();
    void setJournalDirectory(const QString &directory);
    static QString stateText(State state);
  signals:
    void changed();
    void jobFinished(quint64 id);

  private:
    void next();
    void persist(Job &job);
    QString journalDirectory_;
    std::vector<Job> jobs_;
    bool running_ = false;
    quint64 serial_ = 0;
};
class QueueDialog : public QDialog {
    Q_OBJECT
  public:
    explicit QueueDialog(JobQueue *queue, QWidget *parent = nullptr);

  private:
    quint64 selected() const;
    void refresh();
    JobQueue *queue_;
    QTableWidget *table_;
};
