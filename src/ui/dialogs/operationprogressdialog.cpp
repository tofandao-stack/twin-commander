#include "ui/dialogs/operationprogressdialog.h"
#include "filesystem/pathencoding.h"
#include "ui/icons/commandicons.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
namespace {
bool terminal(JobQueue::State state) {
    return state == JobQueue::State::Completed || state == JobQueue::State::Failed ||
           state == JobQueue::State::Cancelled;
}
int percent(uint64_t done, uint64_t total, bool complete = false) {
    if (complete)
        return 100;
    if (!total)
        return 0;
    return int(std::min<long double>(99, 100.L * done / total));
}
} // namespace
OperationProgressWidget::OperationProgressWidget(QWidget *parent)
    : QWidget(parent), file_(new QLabel(this)), summary_(new QLabel(this)), fileBar_(new QProgressBar(this)),
      overallBar_(new QProgressBar(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    file_->setObjectName("currentFileName");
    file_->setWordWrap(true);
    file_->setTextFormat(Qt::PlainText);
    file_->setMaximumHeight(fontMetrics().height() * 3);
    file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_->setObjectName("operationProgressSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    fileBar_->setObjectName("currentFileProgress");
    fileBar_->setAccessibleName("Current file progress");
    overallBar_->setObjectName("overallProgress");
    overallBar_->setAccessibleName("Overall operation progress");
    layout->addWidget(new QLabel("Current file", this));
    layout->addWidget(file_);
    layout->addWidget(fileBar_);
    layout->addWidget(new QLabel("Overall progress", this));
    layout->addWidget(overallBar_);
    layout->addWidget(summary_);
    clear();
}
void OperationProgressWidget::clear() {
    file_->setText("No operation selected");
    fileBar_->setRange(0, 100);
    fileBar_->setValue(0);
    overallBar_->setRange(0, 100);
    overallBar_->setValue(0);
    summary_->clear();
}
void OperationProgressWidget::updateProgress(const std::shared_ptr<LocalFiles::Control> &control,
                                             JobQueue::State state) {
    const auto p = control->progressSnapshot();
    const bool ended = terminal(state), success = state == JobQueue::State::Completed;
    const bool scanning = p.phase == LocalFiles::Progress::Phase::Scanning;
    file_->setText(p.currentPath.empty() ? (ended                               ? "No current file"
                                            : state == JobQueue::State::Waiting ? "Waiting…"
                                            : scanning                          ? "Counting files…"
                                                                                : "Preparing…")
                                         : displayPath(p.currentPath));
    file_->setToolTip(p.currentPath.empty() ? QString() : displayPath(p.currentPath));
    fileBar_->setRange(0, 100);
    fileBar_->setValue(p.currentPath.empty() ? 0 : percent(p.fileBytes, p.fileTotal, p.fileComplete));
    if (!ended && (scanning || !p.totalsKnown || state == JobQueue::State::Waiting)) {
        overallBar_->setRange(0, 0);
    } else {
        overallBar_->setRange(0, 100);
        overallBar_->setValue(percent(p.byteBased && p.totalBytes ? p.doneBytes : p.doneEntries,
                                      p.byteBased && p.totalBytes ? p.totalBytes : p.totalEntries, success));
    }
    const auto status = control->cancelled && !ended ? QString("Cancelling…") : JobQueue::stateText(state);
    QString detail = p.totalsKnown ? QString("%1 / %2 entries").arg(p.doneEntries).arg(p.totalEntries)
                                   : QString("%1 entries processed").arg(p.doneEntries);
    if (p.byteBased)
        detail += p.totalsKnown ? QString(" · %1 / %2 bytes").arg(p.doneBytes).arg(p.totalBytes)
                               : QString(" · %1 bytes processed").arg(p.doneBytes);
    summary_->setText(status + " · " + detail);
}
OperationProgressDialog::OperationProgressDialog(JobQueue *queue, quint64 jobId, QWidget *parent)
    : QDialog(parent), queue_(queue), jobId_(jobId), progress_(new OperationProgressWidget(this)),
      pause_(new QPushButton("Pause", this)), cancel_(new QPushButton("Cancel", this)) {
    setObjectName("operationProgressDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("File operation");
    resize(560, 250);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(progress_);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(pause_);
    buttons->addWidget(cancel_);
    auto *background = new QPushButton("Background", this);
    background->setToolTip("Close this window and continue the operation in the background queue");
    buttons->addWidget(background);
    layout->addLayout(buttons);
    connect(background, &QPushButton::clicked, this, &QDialog::reject);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        auto job = std::find_if(queue_->jobs().begin(), queue_->jobs().end(),
                                [this](const auto &j) { return j.id == jobId_; });
        if (job == queue_->jobs().end() || terminal(job->state)) {
            reject();
            return;
        }
        queue_->cancel(jobId_);
        refresh();
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
    CommandIcons::decorate(this);
}
void OperationProgressDialog::refresh() {
    auto job = std::find_if(queue_->jobs().begin(), queue_->jobs().end(),
                            [this](const auto &j) { return j.id == jobId_; });
    if (job == queue_->jobs().end()) {
        reject();
        return;
    }
    setWindowTitle(job->label);
    progress_->updateProgress(job->control, job->state);
    pause_->setText(job->state == JobQueue::State::Paused ? "Resume" : "Pause");
    pause_->setIcon(style()->standardIcon(job->state == JobQueue::State::Paused ? QStyle::SP_MediaPlay
                                                                            : QStyle::SP_MediaPause));
    pause_->setEnabled((job->state == JobQueue::State::Running || job->state == JobQueue::State::Paused) &&
                       !job->control->cancelled);
    cancel_->setText(terminal(job->state) ? "Close" : "Cancel");
    if (job->state == JobQueue::State::Completed)
        accept();
}
