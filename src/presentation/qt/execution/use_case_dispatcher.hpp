#pragma once

#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <functional>
#include <utility>

namespace notes::presentation {

// Posts pure sync use-case work onto one shared serialized IO worker thread.
// Results hop back with Qt::QueuedConnection; late completions no-op via QPointer.
class UseCaseDispatcher : public QObject {
  Q_OBJECT
public:
  using Work = std::function<void()>;

  explicit UseCaseDispatcher(QObject* parent = nullptr);
  ~UseCaseDispatcher() override;

  UseCaseDispatcher(const UseCaseDispatcher&) = delete;
  UseCaseDispatcher& operator=(const UseCaseDispatcher&) = delete;

  [[nodiscard]] QThread* workerThread() const noexcept { return thread_; }
  [[nodiscard]] bool isShuttingDown() const noexcept {
    return shutting_down_.load(std::memory_order_acquire);
  }

  void post(Work work);

  template <typename R>
  void postResult(std::function<R()> work, QObject* receiver,
                  std::function<void(R)> on_done) {
    if (!receiver || isShuttingDown()) {
      return;
    }
    QPointer<QObject> guard(receiver);
    post([work = std::move(work), guard, on_done = std::move(on_done)]() mutable {
      R value = work();
      if (!guard) {
        return;
      }
      QObject* target = guard.data();
      QMetaObject::invokeMethod(
          target,
          [guard, on_done = std::move(on_done),
           value = std::move(value)]() mutable {
            if (!guard) {
              return;
            }
            on_done(std::move(value));
          },
          Qt::QueuedConnection);
    });
  }

  // Run work on the IO strand after prior posts and block until it finishes.
  // Does not stop the worker. For aboutToQuit dirty-save only.
  void runBlocking(Work work);

  // Run flush_work on the IO strand (after prior posts), then stop the worker.
  void flushAndShutdown(Work flush_work = {}, Work after_flush_on_caller = {});
  // Alias expected by composition_root.
  void shutdown() { flushAndShutdown(); }

private:
  class IoWorker;

  QThread* thread_{nullptr};
  IoWorker* worker_{nullptr};
  std::atomic<bool> shutting_down_{false};
  std::atomic<int> pending_{0};

  friend class IoWorker;
};

}  // namespace notes::presentation
