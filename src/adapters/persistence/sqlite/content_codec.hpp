#pragma once

#include "domain/notes/note_content.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace notes::adapters::persistence {

inline void write_len(std::ostringstream& out, const std::string& s) {
  out << s.size() << ':' << s;
}

inline bool read_len(std::istringstream& in, std::string& out) {
  std::size_t n = 0;
  char colon = 0;
  if (!(in >> n)) return false;
  in.get(colon);
  if (colon != ':') return false;
  out.assign(n, '\0');
  return static_cast<bool>(
      in.read(out.data(), static_cast<std::streamsize>(n)));
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
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line == "PARA") {
      domain::ParagraphBlock para;
      while (std::getline(in, line) && line != "END") {
        if (line.rfind("SPAN ", 0) != 0) continue;
        std::istringstream ls(line.substr(5));
        int b = 0, i = 0, u = 0;
        ls >> b >> i >> u;
        ls >> std::ws;
        std::string text;
        if (!read_len(ls, text)) continue;
        para.spans.push_back(
            domain::TextSpan{std::move(text), b != 0, i != 0, u != 0});
      }
      blocks.emplace_back(std::move(para));
    } else if (line == "CHECK") {
      std::vector<domain::ChecklistItem> items;
      while (std::getline(in, line) && line != "END") {
        if (line.rfind("ITEM ", 0) != 0) continue;
        std::istringstream ls(line.substr(5));
        int done = 0;
        ls >> done;
        ls >> std::ws;
        std::string text;
        if (!read_len(ls, text)) continue;
        items.push_back(domain::ChecklistItem{done != 0, std::move(text)});
      }
      blocks.emplace_back(domain::ChecklistBlock{std::move(items)});
    } else if (line.rfind("ATT ", 0) == 0) {
      std::istringstream ls(line.substr(4));
      std::string id;
      ls >> id;
      ls >> std::ws;
      std::string name;
      (void)read_len(ls, name);
      std::getline(in, line);
      blocks.emplace_back(domain::AttachmentRefBlock{
          domain::AttachmentId{std::move(id)}, std::move(name)});
    }
  }
  return domain::NoteContent{std::move(blocks)};
}

}  // namespace notes::adapters::persistence
