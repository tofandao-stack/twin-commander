#include "ui/dialogs/operationprogressdialog.h"
#include "filesystem/pathencoding.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace {
bool terminal(JobQueue::State state) {
    return state == JobQueue::State::Completed || state == JobQueue::State::Failed ||
           state == JobQueue::State::Cancelled;
}
int percent(uint64_t done, uint64_t total, bool complete = false) {
    if (complete)
        return 100;
    return total ? int(std::min<long double>(99, 100.L * done / total)) : 0;
}
QString amount(long double n) {
    const QStringList units{"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (n >= 1024 && unit < units.size() - 1) {
        n /= 1024;
        ++unit;
    }
    return QString::number(double(n), 'f', unit ? 1 : 0) + " " + units[unit];
}
QString duration(qint64 ms) {
    const auto seconds = qMax<qint64>(0, ms / 1000);
    return QString("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
class ProgressCounterLabel : public QLabel {
  public:
    using QLabel::QLabel;
    std::function<void()> clicked;

  protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && clicked) {
            clicked();
            event->accept();
        } else
            QLabel::mouseReleaseEvent(event);
    }
};
class ElidedPathLabel : public QLabel {
  public:
    explicit ElidedPathLabel(QWidget *parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextFormat(Qt::PlainText);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideMiddle, contentsRect().width()));
    }
};
} // namespace
OperationProgressWidget::OperationProgressWidget(QWidget *parent)
    : QWidget(parent), header_(new QLabel(this)), file_(new ElidedPathLabel(this)),
      destination_(new ElidedPathLabel(this)), summary_(new QLabel(this)), filePercent_(new QLabel(this)),
      overallPercent_(new QLabel(this)), entryCount_(new ProgressCounterLabel(this)),
      byteCount_(new ProgressCounterLabel(this)), fileBar_(new QProgressBar(this)),
      overallBar_(new QProgressBar(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    header_->setObjectName("operationTransferRate");
    header_->setAlignment(Qt::AlignCenter);
    file_->setObjectName("currentFileName");
    destination_->setObjectName("currentDestinationName");
    summary_->setObjectName("operationProgressSummary");
    summary_->setTextFormat(Qt::PlainText);
    filePercent_->setObjectName("currentFilePercent");
    overallPercent_->setObjectName("overallPercent");
    fileBar_->setObjectName("currentFileProgress");
    overallBar_->setObjectName("overallProgress");
    fileBar_->setAccessibleName("Current file progress");
    overallBar_->setAccessibleName("Overall operation progress");
    layout->addWidget(header_);
    auto *paths = new QGridLayout;
    paths->setContentsMargins(0, 0, 0, 0);
    paths->setHorizontalSpacing(6);
    paths->setVerticalSpacing(0);
    paths->addWidget(new QLabel("From:", this), 0, 0);
    paths->addWidget(file_, 0, 1);
    paths->addWidget(new QLabel("To:", this), 1, 0);
    paths->addWidget(destination_, 1, 1);
    paths->setColumnStretch(1, 1);
    layout->addLayout(paths);
    auto *bars = new QGridLayout;
    bars->setContentsMargins(0, 3, 0, 0);
    bars->setHorizontalSpacing(5);
    bars->setVerticalSpacing(3);
    for (auto *bar : {fileBar_, overallBar_}) {
        bar->setTextVisible(false);
        bar->setFixedHeight(qMax(16, fontMetrics().height() + 2));
    }
    for (auto *label : {filePercent_, overallPercent_})
        label->setMinimumWidth(fontMetrics().horizontalAdvance("100%") + 2);
    bars->addWidget(fileBar_, 0, 0);
    bars->addWidget(filePercent_, 0, 1);
    bars->addWidget(overallBar_, 1, 0);
    bars->addWidget(overallPercent_, 1, 1);
    bars->setColumnStretch(0, 1);
    layout->addLayout(bars);
    entryCount_->setObjectName("operationFileCount");
    byteCount_->setObjectName("operationByteCount");
    for (auto *counter : {entryCount_, byteCount_}) {
        counter->setCursor(Qt::PointingHandCursor);
        counter->setToolTip("Click to toggle completed/total and remaining");
    }
    static_cast<ProgressCounterLabel *>(entryCount_)->clicked = [this] {
        remainingEntries_ = !remainingEntries_;
        updateCounters();
    };
    static_cast<ProgressCounterLabel *>(byteCount_)->clicked = [this] {
        remainingBytes_ = !remainingBytes_;
        updateCounters();
    };
    auto *counts = new QHBoxLayout;
    counts->setContentsMargins(0, 0, 0, 0);
    counts->addWidget(entryCount_);
    counts->addStretch();
    counts->addWidget(byteCount_);
    layout->addLayout(counts);
    layout->addWidget(summary_);
    clear();
}
void OperationProgressWidget::clear() {
    source_.reset();
    clock_.invalidate();
    elapsed_.invalidate();
    last_ = {};
    header_->setText("Waiting…");
    file_->setText("—");
    destination_->setText("—");
    for (auto *bar : {fileBar_, overallBar_}) {
        bar->setRange(0, 100);
        bar->setValue(0);
    }
    filePercent_->setText("0%");
    overallPercent_->setText("0%");
    summary_->clear();
    updateCounters();
}
void OperationProgressWidget::updateCounters() {
    const auto &p = last_;
    entryCount_->setText(
        p.totalsKnown ? (remainingEntries_
                             ? "-" + QString::number(p.totalEntries - std::min(p.totalEntries, p.doneEntries))
                             : QString("%1 / %2").arg(p.doneEntries).arg(p.totalEntries))
                      : QString::number(p.doneEntries));
    byteCount_->setText(!p.byteBased      ? QString()
                        : !p.totalsKnown  ? amount(p.doneBytes)
                        : remainingBytes_ ? "-" + amount(p.totalBytes - std::min(p.totalBytes, p.doneBytes))
                                          : amount(p.doneBytes) + " / " + amount(p.totalBytes));
}
void OperationProgressWidget::updateProgress(const std::shared_ptr<LocalFiles::Control> &control,
                                             JobQueue::State state) {
    const auto p = control->progressSnapshot();
    const bool ended = terminal(state), success = state == JobQueue::State::Completed;
    if (source_.lock() != control) {
        source_ = control;
        clock_.start();
        elapsed_.invalidate();
        lastTick_ = activeMs_ = fileActiveMs_ = 0;
        finalElapsed_ = -1;
        startBytes_ = p.doneBytes;
        startEntries_ = p.doneEntries;
        sampledFile_.clear();
        wasActive_ = false;
    }
    const auto now = clock_.elapsed();
    const auto delta = now - lastTick_;
    lastTick_ = now;
    const bool active = state == JobQueue::State::Running &&
                        p.phase == LocalFiles::Progress::Phase::Working && !control->paused &&
                        !control->cancelled;
    if (p.phase == LocalFiles::Progress::Phase::Working && !elapsed_.isValid())
        elapsed_.start();
    if (active && wasActive_)
        activeMs_ += delta;
    if (p.currentPath != sampledFile_) {
        sampledFile_ = p.currentPath;
        fileStartBytes_ = p.fileBytes;
        fileActiveMs_ = 0;
    } else if (active && wasActive_)
        fileActiveMs_ += delta;
    wasActive_ = active;
    if (ended && finalElapsed_ < 0)
        finalElapsed_ = elapsed_.isValid() ? elapsed_.elapsed() : 0;
    const auto elapsed = finalElapsed_ >= 0 ? finalElapsed_ : elapsed_.isValid() ? elapsed_.elapsed() : 0;
    const auto rate =
        activeMs_ > 0
            ? static_cast<long double>(p.doneBytes - std::min(p.doneBytes, startBytes_)) * 1000 / activeMs_
            : 0;
    const auto fileRate =
        fileActiveMs_ > 0 ? static_cast<long double>(p.fileBytes - std::min(p.fileBytes, fileStartBytes_)) *
                                1000 / fileActiveMs_
                          : 0;
    const auto itemRate =
        activeMs_ > 0 ? static_cast<long double>(p.doneEntries - std::min(p.doneEntries, startEntries_)) *
                            1000 / activeMs_
                      : 0;
    QString verb =
        p.operation == LocalFiles::Operation::Copy ? "Copying"
        : p.operation == LocalFiles::Operation::Delete || p.operation == LocalFiles::Operation::Trash
            ? "Deleting"
            : "Moving";
    if (state == JobQueue::State::Paused)
        verb = "Paused";
    else if (control->cancelled && !ended)
        verb = "Cancelling…";
    else if (p.phase == LocalFiles::Progress::Phase::Scanning)
        verb = "Counting files…";
    else if (state == JobQueue::State::Waiting)
        verb = "Waiting…";
    else if (ended)
        verb = JobQueue::stateText(state);
    header_->setText(verb + (p.byteBased && fileRate > 0 ? " " + amount(fileRate) + "/s" : QString()));
    file_->setText(p.currentPath.empty() ? QString("—") : displayPath(p.currentPath));
    file_->setToolTip(file_->text());
    destination_->setText(p.operation == LocalFiles::Operation::Trash ? QString("Trash")
                          : p.currentDestination.empty()              ? QString("—")
                                                                      : displayPath(p.currentDestination));
    destination_->setToolTip(destination_->text());
    fileBar_->setRange(0, 100);
    fileBar_->setValue(p.currentPath.empty() ? 0 : percent(p.fileBytes, p.fileTotal, p.fileComplete));
    filePercent_->setText(QString::number(fileBar_->value()) + "%");
    if (!ended && (p.phase == LocalFiles::Progress::Phase::Scanning || !p.totalsKnown ||
                   state == JobQueue::State::Waiting)) {
        overallBar_->setRange(0, 0);
        overallPercent_->setText("—");
    } else {
        overallBar_->setRange(0, 100);
        overallBar_->setValue(percent(p.byteBased && p.totalBytes ? p.doneBytes : p.doneEntries,
                                      p.byteBased && p.totalBytes ? p.totalBytes : p.totalEntries, success));
        overallPercent_->setText(QString::number(overallBar_->value()) + "%");
    }
    const auto remaining = p.byteBased && p.totalBytes
                               ? p.totalBytes - std::min(p.totalBytes, p.doneBytes)
                               : p.totalEntries - std::min(p.totalEntries, p.doneEntries);
    const auto pace = p.byteBased && p.totalBytes ? rate : itemRate;
    const auto eta = p.totalsKnown && pace > 0 && state == JobQueue::State::Running
                         ? duration(qint64(std::min<long double>(31536000000.L, remaining / pace * 1000)))
                     : success ? QString("0:00:00")
                               : QString("—");
    overallBar_->setTextVisible(p.byteBased && rate > 0 && p.totalsKnown);
    overallBar_->setFormat(p.byteBased && rate > 0 ? amount(rate) + "/s" : QString());
    summary_->setText(QString("Elapsed time: %1    Time left: %2").arg(duration(elapsed), eta));
    last_ = p;
    updateCounters();
}
OperationProgressDialog::OperationProgressDialog(JobQueue *queue, quint64 jobId, QWidget *parent)
    : QDialog(parent), queue_(queue), jobId_(jobId), progress_(new OperationProgressWidget(this)),
      pause_(new QPushButton("&Pause", this)), cancel_(new QPushButton("Cancel", this)),
      background_(new QPushButton("&Background", this)), limitRow_(new QWidget(this)),
      limitEnabled_(new QCheckBox("Speed limit", limitRow_)), limitValue_(new QDoubleSpinBox(limitRow_)) {
    setObjectName("operationProgressDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Twin Commander");
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(550);
    resize(550, 200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(progress_);
    auto *speed = new QHBoxLayout(limitRow_);
    speed->setContentsMargins(0, 0, 0, 0);
    speed->addStretch();
    speed->addWidget(limitEnabled_);
    speed->addWidget(limitValue_);
    speed->addWidget(new QLabel("kB/s", limitRow_));
    limitEnabled_->setObjectName("operationSpeedLimitEnabled");
    limitValue_->setObjectName("operationSpeedLimit");
    limitValue_->setRange(1, 100000000);
    limitValue_->setDecimals(1);
    limitValue_->setValue(QSettings().value("operations/copySpeedKiB", 10240).toDouble());
    limitRow_->hide();
    layout->addWidget(limitRow_);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(pause_);
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addStretch();
    buttons->addWidget(background_);
    layout->addLayout(buttons);
    pause_->setObjectName("operationPause");
    cancel_->setObjectName("operationCancel");
    background_->setObjectName("operationBackground");
    cancel_->setDefault(true);
    for (auto *button : {pause_, cancel_, background_}) {
        button->setMinimumWidth(92);
        button->setProperty("noCommandIcon", true);
    }
    connect(cancel_, &QPushButton::clicked, this, &OperationProgressDialog::reject);
    connect(background_, &QPushButton::clicked, this, &OperationProgressDialog::toggleBackground);
    connect(limitEnabled_, &QCheckBox::toggled, this, [this] { applySpeedLimit(); });
    connect(limitValue_, &QDoubleSpinBox::valueChanged, this, [this] {
        QSettings().setValue("operations/copySpeedKiB", limitValue_->value());
        applySpeedLimit();
    });
    connect(pause_, &QPushButton::clicked, this, [this] {
        for (const auto &job : queue_->jobs())
            if (job.id == jobId_) {
                queue_->pause(jobId_, job.state != JobQueue::State::Paused);
                break;
            }
        refresh();
    });
    connect(queue_, &JobQueue::changed, this, &OperationProgressDialog::refresh);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OperationProgressDialog::refresh);
    timer->start(100);
    refresh();
}
void OperationProgressDialog::reject() {
    setProperty("closing", true);
    for (const auto &job : queue_->jobs())
        if (job.id == jobId_ && !terminal(job.state)) {
            queue_->cancel(jobId_);
            break;
        }
    QDialog::reject();
}
void OperationProgressDialog::toggleBackground() {
    backgroundMode_ = !backgroundMode_;
    setProperty("backgroundMode", backgroundMode_);
    hide();
    setWindowModality(backgroundMode_ ? Qt::NonModal : Qt::WindowModal);
    show();
    background_->setText(backgroundMode_ ? "&Foreground" : "&Background");
    refresh();
    adjustSize();
    if (backgroundMode_ && parentWidget())
        parentWidget()->activateWindow();
    else
        activateWindow();
}
void OperationProgressDialog::applySpeedLimit() {
    for (const auto &job : queue_->jobs())
        if (job.id == jobId_) {
            job.control->bytesPerSecond =
                limitEnabled_->isChecked() ? uint64_t(std::llround(limitValue_->value() * 1024)) : 0;
            break;
        }
}
void OperationProgressDialog::refresh() {
    auto job = std::find_if(queue_->jobs().begin(), queue_->jobs().end(),
                            [this](const auto &j) { return j.id == jobId_; });
    if (job == queue_->jobs().end()) {
        QDialog::reject();
        return;
    }
    progress_->updateProgress(job->control, job->state);
    pause_->setText(job->state == JobQueue::State::Paused ? "&Resume" : "&Pause");
    pause_->setEnabled((job->state == JobQueue::State::Running || job->state == JobQueue::State::Paused) &&
                       !job->control->cancelled);
    cancel_->setText(terminal(job->state) ? "Close" : "Cancel");
    background_->setEnabled(!terminal(job->state));
    const auto progress = job->control->progressSnapshot();
    limitRow_->setVisible(backgroundMode_ && !terminal(job->state) && progress.phase != LocalFiles::Progress::Phase::Idle &&
                         progress.operation == LocalFiles::Operation::Copy);
    if (terminal(job->state) && job->state != JobQueue::State::Completed && isVisible() &&
        windowModality() != Qt::NonModal && !property("closing").toBool()) {
        hide();setWindowModality(Qt::NonModal);show();
    }
    if (job->state == JobQueue::State::Completed)
        accept();
}
