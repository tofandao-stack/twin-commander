#include "backgroundpool.h"
#include "jobqueue.h"
#include <QFutureWatcher>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

JobQueue::JobQueue(QObject *parent) : QObject(parent) {}
JobQueue::~JobQueue() { for (auto &job : jobs_) job.control->cancelled = true; }
quint64 JobQueue::enqueue(const QString &label, std::function<LocalFiles::Result(const std::shared_ptr<LocalFiles::Control> &)> work,
                          std::function<void(const LocalFiles::Result &)> completion)
{
    const auto id = ++serial_;
    jobs_.push_back({id, label, State::Waiting, std::make_shared<LocalFiles::Control>(), {}, std::move(work), std::move(completion), "queue-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json"});
    persist(jobs_.back());
    emit changed();
    QTimer::singleShot(0, this, &JobQueue::next);
    return id;
}
const std::vector<JobQueue::Job> &JobQueue::jobs() const { return jobs_; }
bool JobQueue::busy() const
{
    return std::any_of(jobs_.begin(), jobs_.end(), [](const auto &job) {
        return job.state == State::Waiting || job.state == State::Running || job.state == State::Paused;
    });
}
void JobQueue::cancel(quint64 id)
{
    for (auto &job : jobs_) if (job.id == id) {
        if (job.state != State::Waiting && job.state != State::Running && job.state != State::Paused) return;
        job.control->cancelled = true;
        if (job.state == State::Waiting) { job.state = State::Cancelled; job.result.cancelled = true; job.work = {}; job.completion = {}; }
        persist(job);
        emit changed();
        return;
    }
}
void JobQueue::pause(quint64 id, bool paused)
{
    for (auto &job : jobs_) if (job.id == id && (job.state == State::Running || job.state == State::Paused)) {
        job.control->paused = paused;
        job.state = paused ? State::Paused : State::Running;
        persist(job);
        emit changed(); return;
    }
}
void JobQueue::moveWaiting(quint64 id, int direction)
{
    for (size_t i = 0; i < jobs_.size(); ++i) if (jobs_[i].id == id && jobs_[i].state == State::Waiting) {
        const auto target = static_cast<qint64>(i) + (direction < 0 ? -1 : 1);
        if (target >= 0 && target < static_cast<qint64>(jobs_.size()) && jobs_[target].state == State::Waiting) {
            std::swap(jobs_[i], jobs_[target]); emit changed();
        }
        return;
    }
}
void JobQueue::clearFinished()
{
    std::erase_if(jobs_, [](const auto &job) { return job.state != State::Waiting && job.state != State::Running && job.state != State::Paused; });
    emit changed();
}
void JobQueue::setJournalDirectory(const QString &directory) { journalDirectory_ = directory; }
void JobQueue::persist(Job &job)
{
    if (journalDirectory_.isEmpty()) return;
    QJsonArray items;
    const auto path = [](const std::filesystem::path &value) { return QString::fromLatin1(QByteArray::fromStdString(value.native()).toBase64()); };
    // Bound diagnostic reports independently from the job's in-memory outcomes.
    size_t count = 0;
    for (const auto &item : job.result.items) {
        if (++count > 10000) break;
        items.append(QJsonObject{{"sourceBase64", path(item.source)}, {"destinationBase64", path(item.destination)}, {"error", item.error}, {"skipped", item.skipped}});
    }
    const bool finished = job.state == State::Completed || job.state == State::Failed || job.state == State::Cancelled;
    QJsonObject record{{"version", 1}, {"kind", "queue-summary"}, {"label", job.label}, {"state", stateText(job.state)},
        {"finished", finished}, {"cancelled", job.result.cancelled}, {"error", job.result.journalError}, {"items", items},
        {"outcomesTruncated", job.result.items.size() > 10000}};
    QDir().mkpath(journalDirectory_);
    QSaveFile file(journalDirectory_ + "/" + job.reportName);
    const auto bytes = QJsonDocument(record).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) job.result.journalError = "Queue summary could not be saved";
}
QString JobQueue::stateText(State state)
{
    switch (state) {
    case State::Waiting: return "Waiting";
    case State::Running: return "Running";
    case State::Paused: return "Paused";
    case State::Completed: return "Completed";
    case State::Failed: return "Completed with errors";
    case State::Cancelled: return "Cancelled";
    }
    return {};
}
void JobQueue::next()
{
    if (running_) return;
    auto iterator = std::find_if(jobs_.begin(), jobs_.end(), [](const auto &job) { return job.state == State::Waiting; });
    if (iterator == jobs_.end()) return;
    running_ = true;
    iterator->state = State::Running;
    persist(*iterator);
    const auto id = iterator->id;
    const auto control = iterator->control;
    auto work = iterator->work;
    auto *watcher = new QFutureWatcher<LocalFiles::Result>(this);
    connect(watcher, &QFutureWatcher<LocalFiles::Result>::finished, this, [this, watcher, id] {
        auto result = watcher->result();
        watcher->deleteLater();
        auto job = std::find_if(jobs_.begin(), jobs_.end(), [id](const auto &entry) { return entry.id == id; });
        const auto earlierJournalError = job->result.journalError;
        job->result = std::move(result);
        if (!earlierJournalError.isEmpty()) job->result.journalError += earlierJournalError;
        job->state = job->result.cancelled ? State::Cancelled
            : !job->result.journalError.isEmpty() || std::any_of(job->result.items.begin(), job->result.items.end(), [](const auto &item) { return !item.error.isEmpty(); })
                ? State::Failed : State::Completed;
        persist(*job);
        if (!job->result.journalError.isEmpty() && job->state == State::Completed) job->state = State::Failed;
        auto completion = std::move(job->completion);
        const auto completedResult = job->result;
        job->work = {};
        running_ = false;
        emit changed();
        emit jobFinished(id);
        if (completion) completion(completedResult);
        QTimer::singleShot(0, this, &JobQueue::next);
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [work = std::move(work), control] {
        try { return work(control); }
        catch (const LocalFiles::Cancelled &) { LocalFiles::Result result; result.cancelled = true; return result; }
        catch (const std::exception &error) { LocalFiles::Result result; result.items.push_back({{}, {}, QString::fromLocal8Bit(error.what()), false}); return result; }
    }));
    emit changed();
}
QueueDialog::QueueDialog(JobQueue *queue, QWidget *parent) : QDialog(parent), queue_(queue), table_(new QTableWidget(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Background operations");
    resize(950, 440);
    auto *layout = new QVBoxLayout(this);
    table_->setObjectName("jobTable");
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Job", "Operation", "State", "Items", "MiB processed"});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setColumnWidth(0, 60); table_->setColumnWidth(2, 180); table_->setColumnWidth(4, 130);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    auto *buttons = new QHBoxLayout;
    auto add = [&](const QString &label, auto callback) { auto *button = new QPushButton(label, this); button->setObjectName("queue" + label.section(' ', 0, 0)); buttons->addWidget(button); connect(button, &QPushButton::clicked, this, callback); };
    add("Pause", [this] { queue_->pause(selected(), true); });
    add("Resume", [this] { queue_->pause(selected(), false); });
    add("Cancel selected", [this] { queue_->cancel(selected()); });
    add("Earlier", [this] { queue_->moveWaiting(selected(), -1); });
    add("Later", [this] { queue_->moveWaiting(selected(), 1); });
    add("Clear finished", [this] { queue_->clearFinished(); });
    add("Details", [this] {
        for (const auto &job : queue_->jobs()) if (job.id == selected()) {
            auto *dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(job.label);
            auto *layout = new QVBoxLayout(dialog);
            auto *text = new QPlainTextEdit(dialog);
            text->setReadOnly(true);
            QStringList lines{JobQueue::stateText(job.state), job.result.journalError};
            for (const auto &item : job.result.items) lines << QString::fromLocal8Bit(item.source.c_str()) + " → " + QString::fromLocal8Bit(item.destination.c_str()) + "\n" + (item.skipped ? "Skipped" : item.error.isEmpty() ? "Completed" : item.error);
            text->setPlainText(lines.join('\n'));
            layout->addWidget(text);
            dialog->resize(800, 400); dialog->show();
            break;
        }
    });
    layout->addLayout(buttons);
    connect(queue_, &JobQueue::changed, this, &QueueDialog::refresh);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &QueueDialog::refresh);
    timer->start(200);
    refresh();
}
quint64 QueueDialog::selected() const { return table_->currentRow() >= 0 ? table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toULongLong() : 0; }
void QueueDialog::refresh()
{
    const auto id = selected();
    table_->setRowCount(static_cast<int>(queue_->jobs().size()));
    int row = 0;
    for (const auto &job : queue_->jobs()) {
        const QStringList values{QString::number(job.id), job.label, JobQueue::stateText(job.state), QString::number(job.control->completed.load()), QString::number(job.control->bytes.load() / (1024.0 * 1024), 'f', 1)};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setData(Qt::UserRole, job.id);
            table_->setItem(row, column, item);
        }
        if (job.id == id) table_->setCurrentCell(row, 0);
        ++row;
    }
}
