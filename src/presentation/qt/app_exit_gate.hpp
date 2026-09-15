#pragma once

#include "presentation/qt/execution/use_case_dispatcher.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

namespace notes::presentation {

// Gates normal application/window close on a successful editor flush.
// aboutToQuit is too late to veto; callers must use requestClose() before quit.
class AppExitGate : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool closeBlocked READ closeBlocked NOTIFY closeBlockedChanged)
  Q_PROPERTY(QString blockReason READ blockReason NOTIFY blockReasonChanged)
  Q_PROPERTY(bool quitAuthorized READ quitAuthorized NOTIFY quitAuthorizedChanged)

public:
  AppExitGate(EditorViewModel* editor, UseCaseDispatcher* dispatcher,
              QObject* parent = nullptr);

  [[nodiscard]] bool closeBlocked() const { return close_blocked_; }
  [[nodiscard]] QString blockReason() const { return block_reason_; }
  [[nodiscard]] bool quitAuthorized() const { return quit_authorized_; }

  // Attempt a blocking flush and authorize quit only on success.
  // On failure: keeps the dirty editable document, surfaces a reason, returns false.
  Q_INVOKABLE bool requestClose();

  // Clear the blocked banner after the user chooses to keep editing.
  Q_INVOKABLE void acknowledgeBlock();

  // Retry path used by the close-failed dialog.
  Q_INVOKABLE bool retryClose() { return requestClose(); }

  // Final dispatcher drain after an authorized close (or process teardown).
  Q_INVOKABLE void completeShutdown();

  // Best-effort flush used from aboutToQuit safety net (cannot veto).
  // Returns whether the editor flush succeeded.
  bool flushBestEffort();

signals:
  void closeBlockedChanged();
  void blockReasonChanged();
  void quitAuthorizedChanged();
  void closeFailed(const QString& reason);
  void closeSucceeded();

private:
  void setCloseBlocked(bool blocked, const QString& reason);
  void setQuitAuthorized(bool authorized);

  QPointer<EditorViewModel> editor_;
  QPointer<UseCaseDispatcher> dispatcher_;
  bool close_blocked_{false};
  bool quit_authorized_{false};
  bool shutdown_completed_{false};
  QString block_reason_;
};

}  // namespace notes::presentation
