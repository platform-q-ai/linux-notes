#include "presentation/qt/app_exit_gate.hpp"

namespace notes::presentation {

AppExitGate::AppExitGate(EditorViewModel* editor, UseCaseDispatcher* dispatcher,
                         QObject* parent)
    : QObject(parent), editor_(editor), dispatcher_(dispatcher) {}

void AppExitGate::setCloseBlocked(bool blocked, const QString& reason) {
  const bool changed = (close_blocked_ != blocked) || (block_reason_ != reason);
  close_blocked_ = blocked;
  block_reason_ = reason;
  if (changed) {
    emit closeBlockedChanged();
    emit blockReasonChanged();
  }
  if (blocked) {
    emit closeFailed(reason);
  }
}

void AppExitGate::setQuitAuthorized(bool authorized) {
  if (quit_authorized_ == authorized) return;
  quit_authorized_ = authorized;
  emit quitAuthorizedChanged();
}

bool AppExitGate::requestClose() {
  if (quit_authorized_ && !close_blocked_) {
    return true;
  }

  if (!editor_) {
    setCloseBlocked(false, {});
    setQuitAuthorized(true);
    emit closeSucceeded();
    return true;
  }

  // Flush pending dirty/in-flight work on the IO strand (no GUI BlockingQueued).
  const bool ok = editor_->flushSync();
  if (!ok) {
    QString reason = editor_->errorString();
    if (reason.isEmpty()) {
      reason = QStringLiteral(
          "Could not save your note. The window will stay open so you can retry.");
    } else {
      reason = QStringLiteral("Could not save your note: %1").arg(reason);
    }
    setQuitAuthorized(false);
    setCloseBlocked(true, reason);
    return false;
  }

  // Successful flush: document is clean (or empty); authorize close.
  setCloseBlocked(false, {});
  setQuitAuthorized(true);
  emit closeSucceeded();
  return true;
}

void AppExitGate::acknowledgeBlock() {
  // Keep dirty state and error on the editor; only dismiss the gate banner.
  if (close_blocked_) {
    setCloseBlocked(false, {});
  }
}

void AppExitGate::completeShutdown() {
  if (shutdown_completed_) return;
  shutdown_completed_ = true;
  if (dispatcher_) {
    dispatcher_->flushAndShutdown();
  }
}

bool AppExitGate::flushBestEffort() {
  if (!editor_) {
    return true;
  }
  return editor_->flushSync();
}

}  // namespace notes::presentation
