#pragma once

#include "domain/notes/note_content.hpp"

#include <QString>
#include <QTextCursor>
#include <QTextDocument>

namespace notes::presentation {

class NoteContentDocumentMapper {
public:
  static domain::NoteContent fromPlain(const QString& plain);
  static domain::NoteContent fromHtml(const QString& html);
  static domain::NoteContent fromDocument(const QTextDocument& doc);
  static void applyToDocument(const domain::NoteContent& content, QTextDocument& doc);
  static QString toHtml(const domain::NoteContent& content);
  static QString toPlain(const domain::NoteContent& content);
  static void toggleBold(QTextCursor& cursor);
  static void toggleItalic(QTextCursor& cursor);
  static void toggleUnderline(QTextCursor& cursor);
};

}  // namespace notes::presentation
