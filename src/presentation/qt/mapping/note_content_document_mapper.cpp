#include "presentation/qt/mapping/note_content_document_mapper.hpp"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFragment>

#include <optional>
#include <utility>
#include <variant>

namespace notes::presentation {
namespace {

// Custom block markers survive plain/rich editing as ordinary text/anchors.
// Checklist lines stay human-readable; attachments use a private anchor scheme
// so identity is not lost when the user edits adjacent paragraphs.
constexpr int kPropStructuredKind = QTextFormat::UserProperty + 41;
constexpr int kPropChecklistDone = QTextFormat::UserProperty + 42;
constexpr int kPropAttachmentId = QTextFormat::UserProperty + 43;

constexpr int kKindParagraph = 0;
constexpr int kKindChecklistItem = 1;
constexpr int kKindAttachment = 2;

constexpr QLatin1String kAttachmentScheme{"notes-attachment:"};

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

QString checklistMarker(bool done) {
  return done ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
}

std::optional<domain::ChecklistItem> parseChecklistLine(const QString& text) {
  static const QRegularExpression re(
      QStringLiteral(R"(^\s*\[([ xX])\]\s?(.*)$)"));
  const auto m = re.match(text);
  if (!m.hasMatch()) {
    return std::nullopt;
  }
  domain::ChecklistItem item;
  const QString mark = m.captured(1);
  item.done = (mark == QLatin1String("x") || mark == QLatin1String("X"));
  item.text = m.captured(2).toStdString();
  return item;
}

bool attachmentIdUsable(const QString& id) {
  // Domain opaque-safe gate: forged markers/anchors with path separators,
  // traversal, absolute forms, or non att- tokens must not become
  // AttachmentRefBlock (which would later hit store get/remove/purge GC).
  return domain::AttachmentId::is_opaque_safe(id.toStdString());
}

std::optional<domain::AttachmentRefBlock> parseAttachmentPlain(
    const QString& text) {
  // Legacy/plain form written by older mapper builds:
  // [attachment:<id> <display name>]
  // Id capture still allows broad tokens so we can *detect* forgeries; only
  // opaque-safe ids become structured attachment refs.
  static const QRegularExpression re(
      QStringLiteral(R"(^\s*\[attachment:([^\s\]]+)(?:\s+([^\]]*?))?\s*\]\s*$)"));
  const auto m = re.match(text);
  if (!m.hasMatch()) {
    return std::nullopt;
  }
  const QString id = m.captured(1);
  if (!attachmentIdUsable(id)) {
    return std::nullopt;
  }
  domain::AttachmentRefBlock att;
  att.attachment_id = domain::AttachmentId{id.toStdString()};
  att.display_name = m.captured(2).trimmed().toStdString();
  return att;
}

std::optional<domain::AttachmentRefBlock> attachmentFromBlock(
    const QTextBlock& block) {
  const QTextBlockFormat bf = block.blockFormat();
  if (bf.intProperty(kPropStructuredKind) == kKindAttachment) {
    const QString id = bf.stringProperty(kPropAttachmentId);
    if (!id.isEmpty() && attachmentIdUsable(id)) {
      domain::AttachmentRefBlock att;
      att.attachment_id = domain::AttachmentId{id.toStdString()};
      att.display_name = block.text().toStdString();
      return att;
    }
    // Forged/unsafe structured property: fall through; do not trust id.
  }

  QString anchor_id;
  QString label;
  for (auto it = block.begin(); !(it.atEnd()); ++it) {
    const QTextFragment frag = it.fragment();
    if (!frag.isValid()) {
      continue;
    }
    const QTextCharFormat cf = frag.charFormat();
    if (cf.isAnchor()) {
      const QString href = cf.anchorHref();
      if (href.startsWith(kAttachmentScheme)) {
        anchor_id = href.mid(QString(kAttachmentScheme).size());
        label += frag.text();
        continue;
      }
    }
    // Non-anchor text in an attachment row is treated as display name tail.
    if (!anchor_id.isEmpty()) {
      label += frag.text();
    }
  }
  if (!anchor_id.isEmpty() && attachmentIdUsable(anchor_id)) {
    domain::AttachmentRefBlock att;
    att.attachment_id = domain::AttachmentId{anchor_id.toStdString()};
    att.display_name = label.toStdString();
    return att;
  }

  return parseAttachmentPlain(block.text());
}

domain::ParagraphBlock paragraphFromBlock(const QTextBlock& block) {
  domain::ParagraphBlock para;
  for (auto it = block.begin(); !(it.atEnd()); ++it) {
    const QTextFragment frag = it.fragment();
    if (!frag.isValid()) {
      continue;
    }
    const QString t = frag.text();
    // QTextDocument uses U+FFFC for object replacement; drop empties.
    if (t.isEmpty()) {
      continue;
    }
    para.spans.push_back(spanFromFormat(t, frag.charFormat()));
  }
  if (para.spans.empty()) {
    para.spans.push_back(domain::TextSpan{});
  }
  return para;
}

void flushChecklist(std::vector<domain::ChecklistItem>& items,
                    std::vector<domain::ContentBlock>& blocks) {
  if (items.empty()) {
    return;
  }
  blocks.emplace_back(domain::ChecklistBlock{std::move(items)});
  items.clear();
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
  std::vector<domain::ChecklistItem> checklist_items;

  for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
    // Trailing empty document block is common after insertBlock sequences.
    if (!block.next().isValid() && block.text().isEmpty() &&
        block.blockFormat().intProperty(kPropStructuredKind) == 0 &&
        !blocks.empty() && checklist_items.empty()) {
      // Keep a final empty paragraph only when it is the sole content.
      continue;
    }

    if (auto att = attachmentFromBlock(block)) {
      flushChecklist(checklist_items, blocks);
      blocks.emplace_back(std::move(*att));
      continue;
    }

    const QTextBlockFormat bf = block.blockFormat();
    const int kind = bf.intProperty(kPropStructuredKind);
    if (kind == kKindChecklistItem) {
      domain::ChecklistItem item;
      item.done = bf.boolProperty(kPropChecklistDone);
      // Prefer marker-stripped text when present so visible edits stick.
      if (auto parsed = parseChecklistLine(block.text())) {
        item = std::move(*parsed);
      } else {
        item.text = block.text().toStdString();
      }
      checklist_items.push_back(std::move(item));
      continue;
    }

    if (auto parsed = parseChecklistLine(block.text())) {
      checklist_items.push_back(std::move(*parsed));
      continue;
    }

    flushChecklist(checklist_items, blocks);
    blocks.emplace_back(paragraphFromBlock(block));
  }
  flushChecklist(checklist_items, blocks);

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

  auto ensure_block = [&](const QTextBlockFormat& bf) {
    if (first) {
      cursor.setBlockFormat(bf);
      first = false;
    } else {
      cursor.insertBlock(bf);
    }
  };

  for (const auto& block : content.blocks()) {
    if (const auto* para = std::get_if<domain::ParagraphBlock>(&block)) {
      QTextBlockFormat bf;
      bf.setProperty(kPropStructuredKind, kKindParagraph);
      ensure_block(bf);
      if (para->spans.empty()) {
        continue;
      }
      for (const auto& span : para->spans) {
        QTextCharFormat fmt;
        applySpanFormat(fmt, span);
        cursor.insertText(QString::fromStdString(span.text), fmt);
      }
    } else if (const auto* check = std::get_if<domain::ChecklistBlock>(&block)) {
      const auto& items = check->items();
      if (items.empty()) {
        QTextBlockFormat bf;
        bf.setProperty(kPropStructuredKind, kKindChecklistItem);
        bf.setProperty(kPropChecklistDone, false);
        ensure_block(bf);
        cursor.insertText(checklistMarker(false));
        continue;
      }
      for (const auto& item : items) {
        QTextBlockFormat bf;
        bf.setProperty(kPropStructuredKind, kKindChecklistItem);
        bf.setProperty(kPropChecklistDone, item.done);
        ensure_block(bf);
        cursor.insertText(checklistMarker(item.done) +
                          QString::fromStdString(item.text));
      }
    } else if (const auto* att =
                   std::get_if<domain::AttachmentRefBlock>(&block)) {
      QTextBlockFormat bf;
      bf.setProperty(kPropStructuredKind, kKindAttachment);
      bf.setProperty(kPropAttachmentId,
                     QString::fromStdString(att->attachment_id.value()));
      ensure_block(bf);

      QTextCharFormat fmt;
      fmt.setAnchor(true);
      fmt.setAnchorHref(QString(kAttachmentScheme) +
                        QString::fromStdString(att->attachment_id.value()));
      fmt.setForeground(QColor(0x15, 0x65, 0xc0));
      fmt.setFontUnderline(true);
      const QString label =
          att->display_name.empty()
              ? QString::fromStdString(att->attachment_id.value())
              : QString::fromStdString(att->display_name);
      cursor.insertText(label, fmt);
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
