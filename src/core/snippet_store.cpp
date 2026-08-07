#include "snippet_store.hpp"
#include "keymap.hpp"
#include "utf8.hpp"

#include <cctype>

namespace keyboop {

SnippetStore &SnippetStore::shared() {
  static SnippetStore s;
  return s;
}

std::string SnippetStore::canonical(std::string_view s) {
  return to_lower_utf8(Keymap::convert(s, false));
}

void SnippetStore::rebuild_index() {
  by_canonical_.clear();
  for (auto &[t, e] : ordered_) {
    auto c = canonical(t);
    if (!c.empty())
      by_canonical_[c] = e;
  }
}

std::optional<std::string>
SnippetStore::expansion_for_typed(std::string_view word) const {
  auto it = by_canonical_.find(canonical(word));
  if (it == by_canonical_.end())
    return std::nullopt;
  return it->second;
}

void SnippetStore::set_all(
    std::vector<std::pair<std::string, std::string>> pairs) {
  std::vector<std::pair<std::string, std::string>> out;
  for (auto &[t, e] : pairs) {
    // trim
    size_t a = 0, b = t.size();
    while (a < b && std::isspace(static_cast<unsigned char>(t[a])))
      ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1])))
      --b;
    std::string trig = t.substr(a, b - a);
    if (trig.empty())
      continue;
    auto c = canonical(trig);
    bool found = false;
    for (auto &p : out) {
      if (canonical(p.first) == c) {
        p.second = e;
        found = true;
        break;
      }
    }
    if (!found)
      out.emplace_back(trig, e);
  }
  ordered_ = std::move(out);
  rebuild_index();
}

} // namespace keyboop
