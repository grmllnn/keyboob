#include "keymap.hpp"
#include "active_keymap.hpp"
#include "utf8.hpp"

namespace keyboop {
namespace {

const std::pair<const char *, const char *> kBasePairs[] = {
    {"`", "ё"}, {"q", "й"}, {"w", "ц"}, {"e", "у"}, {"r", "к"}, {"t", "е"},
    {"y", "н"}, {"u", "г"}, {"i", "ш"}, {"o", "щ"}, {"p", "з"}, {"[", "х"},
    {"]", "ъ"}, {"a", "ф"}, {"s", "ы"}, {"d", "в"}, {"f", "а"}, {"g", "п"},
    {"h", "р"}, {"j", "о"}, {"k", "л"}, {"l", "д"}, {";", "ж"}, {"'", "э"},
    {"z", "я"}, {"x", "ч"}, {"c", "с"}, {"v", "м"}, {"b", "и"}, {"n", "т"},
    {"m", "ь"}, {",", "б"}, {".", "ю"}, {"/", "."},
};

const std::pair<const char *, const char *> kShiftedPunctPairs[] = {
    {"~", "Ё"}, {"{", "Х"}, {"}", "Ъ"}, {":", "Ж"},
    {"\"", "Э"}, {"<", "Б"}, {">", "Ю"}, {"?", ","},
    // Number row (US Shift+2..7). Do not overwrite unshifted ; , .
    {"@", "\""}, {"#", "№"}, {"$", ";"}, {"^", ":"}, {"&", "?"},
};

std::unordered_map<std::string, std::string> build_en_to_ru() {
  std::unordered_map<std::string, std::string> d;
  for (auto [e, r] : kBasePairs) {
    d[e] = r;
    if (e[0] >= 'a' && e[0] <= 'z') {
      std::string Eu(1, static_cast<char>(e[0] - 'a' + 'A'));
      d[Eu] = utf8_encode(utf8_to_upper(Utf8Iter(r).next()));
    }
  }
  for (auto [e, r] : kShiftedPunctPairs) {
    d[e] = r;
  }
  return d;
}

std::unordered_map<std::string, std::string> build_ru_to_en() {
  std::unordered_map<std::string, std::string> d;
  for (auto [e, r] : kBasePairs) {
    d[r] = e;
    if (e[0] >= 'a' && e[0] <= 'z') {
      std::string Eu(1, static_cast<char>(e[0] - 'a' + 'A'));
      d[utf8_encode(utf8_to_upper(Utf8Iter(r).next()))] = Eu;
    }
  }
  for (auto [e, r] : kShiftedPunctPairs) {
    d[r] = e;
  }
  return d;
}

} // namespace

const std::unordered_map<std::string, std::string> &Keymap::builtin_en_to_ru() {
  static const auto m = build_en_to_ru();
  return m;
}

const std::unordered_map<std::string, std::string> &Keymap::builtin_ru_to_en() {
  static const auto m = build_ru_to_en();
  return m;
}

const std::unordered_map<std::string, std::string> &Keymap::en_to_ru() {
  return ActiveKeymap::shared().to_cyrillic();
}

const std::unordered_map<std::string, std::string> &Keymap::ru_to_en() {
  return ActiveKeymap::shared().to_latin();
}

bool Keymap::is_trailing_punct(uint32_t cp) {
  return cp == '.' || cp == ',' || cp == '!' || cp == '?' || cp == ';' ||
         cp == ':' || cp == 0x2026 || cp == '\'' || cp == '"' ||
         cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
         cp == '{' || cp == '}' || cp == '<' || cp == '>' || cp == '/' ||
         cp == '\\' || cp == '-' || cp == '_' || cp == 0x00AB || cp == 0x00BB ||
         cp == 0x201C || cp == 0x201D || cp == 0x2018 || cp == 0x2019;
}

std::string Keymap::convert(std::string_view text, bool to_cyrillic) {
  return ActiveKeymap::shared().convert(text, to_cyrillic);
}

std::string Keymap::core(std::string_view word) {
  std::string s(word);
  while (!s.empty() && is_trailing_punct(utf8_back(s)))
    utf8_pop_back(s);
  return s;
}

std::string Keymap::smart_convert(
    std::string_view word, bool to_cyrillic,
    const std::function<bool(const std::string &)> &is_valid_target) {
  if (to_cyrillic && is_valid_target) {
    auto full = convert(word, true);
    if (is_valid_target(to_lower_utf8(full)))
      return full;
  }
  std::string core_s(word);
  std::string trailing;
  while (!core_s.empty() && is_trailing_punct(utf8_back(core_s))) {
    auto ch = utf8_encode(utf8_back(core_s));
    trailing = ch + trailing;
    utf8_pop_back(core_s);
  }
  if (core_s.empty())
    return std::string(word);
  return convert(core_s, to_cyrillic) + trailing;
}

} // namespace keyboop
