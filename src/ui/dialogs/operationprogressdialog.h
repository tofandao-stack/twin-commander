#pragma once
#include "operations/jobqueue.h"
#include <QDialog>
#include <QElapsedTimer>
class QLabel;
class QProgressBar;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class OperationProgressWidget : public QWidget {
    Q_OBJECT
  public:
    explicit OperationProgressWidget(QWidget *parent = nullptr);
    void updateProgress(const std::shared_ptr<LocalFiles::Control> &control, JobQueue::State state);
    void clear();

  private:
    void updateCounters();
    QLabel *header_, *file_, *destination_, *summary_, *filePercent_, *overallPercent_, *entryCount_,
        *byteCount_;
    QProgressBar *fileBar_, *overallBar_;
    LocalFiles::Progress last_;
    std::weak_ptr<LocalFiles::Control> source_;
    QElapsedTimer clock_, elapsed_;
    qint64 lastTick_ = 0, activeMs_ = 0, fileActiveMs_ = 0, finalElapsed_ = -1;
    uint64_t startBytes_ = 0, startEntries_ = 0, fileStartBytes_ = 0;
    std::filesystem::path sampledFile_;
    bool wasActive_ = false, remainingEntries_ = false, remainingBytes_ = false;
};
class OperationProgressDialog : public QDialog {
    Q_OBJECT
  public:
    OperationProgressDialog(JobQueue *queue, quint64 jobId, QWidget *parent = nullptr);
    void reject() override;

  private:
    void refresh();
    void toggleBackground();
    void applySpeedLimit();
    JobQueue *queue_;
    quint64 jobId_;
    OperationProgressWidget *progress_;
    QPushButton *pause_, *cancel_, *background_;
    QWidget *limitRow_;
    QCheckBox *limitEnabled_;
    QDoubleSpinBox *limitValue_;
    bool backgroundMode_ = false;
};
