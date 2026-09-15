#include "presentation/qt/execution/use_case_dispatcher.hpp"

#include <QMetaObject>
#include <QMutex>
#include <QWaitCondition>

#include <memory>

namespace notes::presentation {

class UseCaseDispatcher::IoWorker final : public QObject {
  Q_OBJECT
public:
  explicit IoWorker(UseCaseDispatcher* owner) : owner_(owner) {}

  void execute(UseCaseDispatcher::Work work) {
    if (work) {
      work();
    }
    if (owner_) {
      owner_->pending_.fetch_sub(1, std::memory_order_acq_rel);
    }
  }

private:
  UseCaseDispatcher* owner_{nullptr};
};

namespace {

class SyncPoint final {
public:
  void signal() {
    QMutexLocker lock(&mu_);
    ready_ = true;
    cv_.wakeAll();
  }
  void wait() {
    QMutexLocker lock(&mu_);
    while (!ready_) {
      cv_.wait(&mu_);
    }
  }

private:
  QMutex mu_;
  QWaitCondition cv_;
  bool ready_{false};
};

}  // namespace

UseCaseDispatcher::UseCaseDispatcher(QObject* parent) : QObject(parent) {
  thread_ = new QThread(this);
  thread_->setObjectName(QStringLiteral("notes.io"));
  worker_ = new IoWorker(nullptr);  // no cross-thread parent before moveToThread
  worker_->moveToThread(thread_);
  connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
  thread_->start();
}

UseCaseDispatcher::~UseCaseDispatcher() { flushAndShutdown(); }

void UseCaseDispatcher::post(Work work) {
  if (!work || isShuttingDown() || worker_ == nullptr || thread_ == nullptr) {
    return;
  }
  pending_.fetch_add(1, std::memory_order_acq_rel);
  // QueuedConnection to object living on worker thread — no main-thread I/O.
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, work = std::move(work)]() mutable {
        if (worker) {
          worker->execute(std::move(work));
        }
      },
      Qt::QueuedConnection);
}

void UseCaseDispatcher::runBlocking(Work work) {
  if (worker_ == nullptr || thread_ == nullptr || !thread_->isRunning()) {
    if (work) {
      work();
    }
    return;
  }
  auto gate = std::make_shared<SyncPoint>();
  QMetaObject::invokeMethod(
      worker_,
      [work = std::move(work), gate]() mutable {
        if (work) {
          work();
        }
        gate->signal();
      },
      Qt::QueuedConnection);
  gate->wait();
}

void UseCaseDispatcher::flushAndShutdown(Work flush_work,
                                         Work after_flush_on_caller) {
  bool expected = false;
  if (!shutting_down_.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
    if (thread_ && thread_->isRunning()) {
      thread_->quit();
      thread_->wait(10000);
    }
    return;
  }

  if (worker_ == nullptr || thread_ == nullptr || !thread_->isRunning()) {
    if (after_flush_on_caller) {
      after_flush_on_caller();
    }
    return;
  }

  auto gate = std::make_shared<SyncPoint>();
  // Do not touch pending_ here: flush runs after previously posted work because
  // the worker thread's event queue is FIFO.
  QMetaObject::invokeMethod(
      worker_,
      [flush_work = std::move(flush_work), gate]() mutable {
        if (flush_work) {
          flush_work();
        }
        gate->signal();
      },
      Qt::QueuedConnection);

  // Caller (typically GUI) blocks only during shutdown, not during edits.
  gate->wait();

  if (after_flush_on_caller) {
    after_flush_on_caller();
  }

  thread_->quit();
  thread_->wait(15000);
  worker_ = nullptr;
}

}  // namespace notes::presentation

#include "use_case_dispatcher.moc"
