#pragma once
#include "operations/jobqueue.h"
#include <QDialog>
class QLabel;
class QProgressBar;
class QPushButton;
class OperationProgressWidget : public QWidget {
    Q_OBJECT
  public:
    explicit OperationProgressWidget(QWidget *parent = nullptr);
    void updateProgress(const std::shared_ptr<LocalFiles::Control> &control, JobQueue::State state);
    void clear();

  private:
    QLabel *file_, *summary_;
    QProgressBar *fileBar_, *overallBar_;
};
class OperationProgressDialog : public QDialog {
    Q_OBJECT
  public:
    OperationProgressDialog(JobQueue *queue, quint64 jobId, QWidget *parent = nullptr);

  private:
    void refresh();
    JobQueue *queue_;
    quint64 jobId_;
    OperationProgressWidget *progress_;
    QPushButton *pause_, *cancel_;
};
