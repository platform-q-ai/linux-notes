#pragma once

#include "application/result.hpp"

#include <QString>

namespace notes::presentation {

inline QString errorText(const application::Error& err) {
  const QString detail = err.message.empty()
                             ? QString()
                             : QStringLiteral(": %1").arg(
                                   QString::fromStdString(err.message));
  switch (err.kind) {
    case application::ErrorKind::NotFound:
      return QStringLiteral("Not found") + detail;
    case application::ErrorKind::ValidationFailed:
      return QStringLiteral("Invalid data") + detail;
    case application::ErrorKind::StorageFailure:
      return QStringLiteral("Storage error") + detail;
    case application::ErrorKind::RevisionConflict:
      return QStringLiteral("Edit conflict") + detail;
  }
  return QStringLiteral("Unknown error");
}

}  // namespace notes::presentation
