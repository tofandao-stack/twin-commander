#include "jobqueue.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QtTest>

class QueueTests : public QObject {
    Q_OBJECT
private slots:
    void fifoWaitingCancellationAndReorder()
    {
        JobQueue queue; std::vector<int> order; int completions = 0;
        const auto work = [&](int number) { return [&, number](const auto &) { order.push_back(number); return LocalFiles::Result{}; }; };
        queue.enqueue("one", work(1), [&](const auto &) { ++completions; });
        const auto two = queue.enqueue("two", work(2)); const auto three = queue.enqueue("three", work(3));
        queue.moveWaiting(three, -1); queue.cancel(two);
        QTRY_VERIFY(!queue.busy());
        QCOMPARE(order, (std::vector<int>{1, 3})); QCOMPARE(completions, 1);
        QVERIFY(queue.jobs()[1].state == JobQueue::State::Completed);
        queue.clearFinished(); QVERIFY(queue.jobs().empty());
    }
    void pauseResumeAndCancelRunning()
    {
        JobQueue queue;
        auto entered = std::make_shared<std::atomic_bool>(false);
        auto proceed = std::make_shared<std::atomic_bool>(false);
        const auto id = queue.enqueue("controlled", [entered, proceed](const auto &control) {
            *entered = true;
            while (!*proceed) { LocalFiles::checkpoint(control); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            return LocalFiles::Result{};
        });
        QTRY_VERIFY(entered->load()); queue.pause(id, true);
        QVERIFY(queue.jobs().front().state == JobQueue::State::Paused);
        queue.pause(id, false); QVERIFY(queue.jobs().front().state == JobQueue::State::Running);
        queue.pause(id, true); queue.cancel(id);
        QTRY_VERIFY(!queue.busy()); QVERIFY(queue.jobs().front().state == JobQueue::State::Cancelled);
    }
    void failedJobDoesNotBlockNextAndReportsPersist()
    {
        QTemporaryDir directory; JobQueue queue; queue.setJournalDirectory(directory.path());
        queue.enqueue("throws", [](const auto &) -> LocalFiles::Result { throw std::runtime_error("fixture error"); });
        bool next = false; queue.enqueue("next", [](const auto &) { return LocalFiles::Result{}; }, [&](const auto &) { next = true; });
        QTRY_VERIFY(!queue.busy()); QVERIFY(next);
        QVERIFY(queue.jobs().front().state == JobQueue::State::Failed);
        QVERIFY(queue.jobs().back().state == JobQueue::State::Completed);
        QCOMPARE(QDir(directory.path()).entryList({"queue-*.json"}, QDir::Files).size(), 2);
    }
};
QTEST_GUILESS_MAIN(QueueTests)
#include "queue_tests.moc"
