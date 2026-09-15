#pragma once

#include "domain/notes/note_content.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace notes::adapters::persistence {

// Length-prefixed payloads are binary-safe (may contain newlines).
// Structure tags stay line-oriented; SPAN/ITEM/ATT values are never parsed
// with getline on the payload itself.

inline void write_len(std::ostream& out, const std::string& s) {
  out << s.size() << ':' << s;
}

inline bool read_len(std::istream& in, std::string& out) {
  std::size_t n = 0;
  char colon = 0;
  if (!(in >> n)) return false;
  in.get(colon);
  if (colon != ':') return false;
  out.assign(n, '\0');
  if (n == 0) return true;
  return static_cast<bool>(
      in.read(out.data(), static_cast<std::streamsize>(n)));
}

inline void skip_record_eol(std::istream& in) {
  while (in) {
    const int ch = in.peek();
    if (ch == ' ' || ch == '\t' || ch == '\r') {
      in.get();
      continue;
    }
    if (ch == '\n') {
      in.get();
    }
    break;
  }
}

inline bool read_tag(std::istream& in, std::string& tag) {
  tag.clear();
  in >> std::ws;
  if (!in || in.peek() == EOF) return false;
  if (!(in >> tag)) return false;
  return true;
}

inline std::string encode_content(const domain::NoteContent& content) {
  std::ostringstream out;
  out << "V1\n";
  for (const auto& block : content.blocks()) {
    if (const auto* p = std::get_if<domain::ParagraphBlock>(&block)) {
      out << "PARA\n";
      for (const auto& sp : p->spans) {
        out << "SPAN " << sp.bold << ' ' << sp.italic << ' ' << sp.underline
            << ' ';
        write_len(out, sp.text);
        out << '\n';
      }
      out << "END\n";
    } else if (const auto* c = std::get_if<domain::ChecklistBlock>(&block)) {
      out << "CHECK\n";
      for (const auto& it : c->items()) {
        out << "ITEM " << (it.done ? 1 : 0) << ' ';
        write_len(out, it.text);
        out << '\n';
      }
      out << "END\n";
    } else if (const auto* a = std::get_if<domain::AttachmentRefBlock>(&block)) {
      out << "ATT " << a->attachment_id.value() << ' ';
      write_len(out, a->display_name);
      out << "\nEND\n";
    }
  }
  return out.str();
}

inline domain::NoteContent decode_content(const std::string& blob) {
  std::istringstream in(blob);
  std::string line;
  if (!std::getline(in, line) || line != "V1") {
    return domain::NoteContent::from_plain_text(blob);
  }

  std::vector<domain::ContentBlock> blocks;
  std::string tag;
  while (read_tag(in, tag)) {
    if (tag == "PARA") {
      skip_record_eol(in);
      domain::ParagraphBlock para;
      while (read_tag(in, tag)) {
        if (tag == "END") {
          skip_record_eol(in);
          break;
        }
        if (tag != "SPAN") {
          skip_record_eol(in);
          continue;
        }
        int b = 0, i = 0, u = 0;
        if (!(in >> b >> i >> u)) break;
        in >> std::ws;
        std::string text;
        if (!read_len(in, text)) break;
        skip_record_eol(in);
        para.spans.push_back(
            domain::TextSpan{std::move(text), b != 0, i != 0, u != 0});
      }
      blocks.emplace_back(std::move(para));
    } else if (tag == "CHECK") {
      skip_record_eol(in);
      std::vector<domain::ChecklistItem> items;
      while (read_tag(in, tag)) {
        if (tag == "END") {
          skip_record_eol(in);
          break;
        }
        if (tag != "ITEM") {
          skip_record_eol(in);
          continue;
        }
        int done = 0;
        if (!(in >> done)) break;
        in >> std::ws;
        std::string text;
        if (!read_len(in, text)) break;
        skip_record_eol(in);
        items.push_back(domain::ChecklistItem{done != 0, std::move(text)});
      }
      blocks.emplace_back(domain::ChecklistBlock{std::move(items)});
    } else if (tag == "ATT") {
      std::string id;
      if (!(in >> id)) break;
      in >> std::ws;
      std::string name;
      if (!read_len(in, name)) {
        name.clear();
      }
      skip_record_eol(in);
      const auto pos = in.tellg();
      std::string maybe_end;
      if (read_tag(in, maybe_end) && maybe_end == "END") {
        skip_record_eol(in);
      } else if (pos != std::streampos(-1)) {
        in.clear();
        in.seekg(pos);
      }
      blocks.emplace_back(domain::AttachmentRefBlock{
          domain::AttachmentId{std::move(id)}, std::move(name)});
    } else {
      skip_record_eol(in);
    }
  }
  return domain::NoteContent{std::move(blocks)};
}

}  // namespace notes::adapters::persistence
