#include "presentation/qt/mapping/note_content_document_mapper.hpp"

#include <QFont>
#include <QTextBlock>
#include <QTextFragment>

#include <variant>

namespace notes::presentation {
namespace {

domain::TextSpan spanFromFormat(const QString& text, const QTextCharFormat& fmt) {
  domain::TextSpan span;
  span.text = text.toStdString();
  span.bold = fmt.fontWeight() >= QFont::Bold;
  span.italic = fmt.fontItalic();
  span.underline = fmt.fontUnderline();
  return span;
}

void applySpanFormat(QTextCharFormat& fmt, const domain::TextSpan& span) {
  fmt.setFontWeight(span.bold ? QFont::Bold : QFont::Normal);
  fmt.setFontItalic(span.italic);
  fmt.setFontUnderline(span.underline);
}

void toggleFlag(QTextCursor& cursor, auto get, auto set) {
  if (!cursor.hasSelection()) {
    QTextCharFormat fmt = cursor.charFormat();
    set(fmt, !get(fmt));
    cursor.setCharFormat(fmt);
    return;
  }
  const int start = cursor.selectionStart();
  const int end = cursor.selectionEnd();
  cursor.setPosition(start);
  QTextCharFormat probe = cursor.charFormat();
  const bool turn_on = !get(probe);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
  QTextCharFormat fmt;
  set(fmt, turn_on);
  cursor.mergeCharFormat(fmt);
}

}  // namespace

domain::NoteContent NoteContentDocumentMapper::fromPlain(const QString& plain) {
  return domain::NoteContent::from_plain_text(plain.toStdString());
}

domain::NoteContent NoteContentDocumentMapper::fromHtml(const QString& html) {
  QTextDocument doc;
  doc.setHtml(html);
  return fromDocument(doc);
}

domain::NoteContent NoteContentDocumentMapper::fromDocument(
    const QTextDocument& doc) {
  std::vector<domain::ContentBlock> blocks;
  for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
    domain::ParagraphBlock para;
    for (auto it = block.begin(); !(it.atEnd()); ++it) {
      const QTextFragment frag = it.fragment();
      if (!frag.isValid()) {
        continue;
      }
      const QString t = frag.text();
      if (t.isEmpty()) {
        continue;
      }
      para.spans.push_back(spanFromFormat(t, frag.charFormat()));
    }
    if (para.spans.empty()) {
      para.spans.push_back(domain::TextSpan{});
    }
    blocks.emplace_back(std::move(para));
  }
  if (blocks.empty()) {
    return domain::NoteContent::from_plain_text("");
  }
  return domain::NoteContent{std::move(blocks)};
}

void NoteContentDocumentMapper::applyToDocument(
    const domain::NoteContent& content, QTextDocument& doc) {
  doc.clear();
  QTextCursor cursor(&doc);
  cursor.beginEditBlock();
  bool first = true;
  for (const auto& block : content.blocks()) {
    if (!first) {
      cursor.insertBlock();
    }
    first = false;
    if (const auto* para = std::get_if<domain::ParagraphBlock>(&block)) {
      for (const auto& span : para->spans) {
        QTextCharFormat fmt;
        applySpanFormat(fmt, span);
        cursor.insertText(QString::fromStdString(span.text), fmt);
      }
    } else if (const auto* check = std::get_if<domain::ChecklistBlock>(&block)) {
      const auto& items = check->items();
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
          cursor.insertBlock();
        }
        const auto& item = items[i];
        const QString line =
            QStringLiteral("%1 %2")
                .arg(item.done ? QStringLiteral("[x]") : QStringLiteral("[ ]"),
                     QString::fromStdString(item.text));
        cursor.insertText(line);
      }
    } else if (const auto* att =
                   std::get_if<domain::AttachmentRefBlock>(&block)) {
      cursor.insertText(
          QStringLiteral("[attachment:%1 %2]")
              .arg(QString::fromStdString(att->attachment_id.value()),
                   QString::fromStdString(att->display_name)));
    }
  }
  cursor.endEditBlock();
}

QString NoteContentDocumentMapper::toHtml(const domain::NoteContent& content) {
  QTextDocument doc;
  applyToDocument(content, doc);
  return doc.toHtml();
}

QString NoteContentDocumentMapper::toPlain(const domain::NoteContent& content) {
  return QString::fromStdString(content.plain_text());
}

void NoteContentDocumentMapper::toggleBold(QTextCursor& cursor) {
  toggleFlag(
      cursor,
      [](const QTextCharFormat& f) { return f.fontWeight() >= QFont::Bold; },
      [](QTextCharFormat& f, bool on) {
        f.setFontWeight(on ? QFont::Bold : QFont::Normal);
      });
}

void NoteContentDocumentMapper::toggleItalic(QTextCursor& cursor) {
  toggleFlag(
      cursor, [](const QTextCharFormat& f) { return f.fontItalic(); },
      [](QTextCharFormat& f, bool on) { f.setFontItalic(on); });
}

void NoteContentDocumentMapper::toggleUnderline(QTextCursor& cursor) {
  toggleFlag(
      cursor, [](const QTextCharFormat& f) { return f.fontUnderline(); },
      [](QTextCharFormat& f, bool on) { f.setFontUnderline(on); });
}

}  // namespace notes::presentation
