#include "layout_detector.hpp"
#include "extra_words.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"
#include "utf8.hpp"

#include <algorithm>

namespace keyboop {
namespace {

const std::unordered_set<std::string> &force_swap() {
  static const std::unordered_set<std::string> s = {
      "http", "https", "url",  "uri",  "api",  "rest", "json", "xml",  "yaml",
      "csv",  "html",  "css",  "sdk",  "cli",  "gui",  "ide",  "ssh",  "ftp",
      "tcp",  "udp",   "ip",   "dns",  "vpn",  "ssl",  "tls",  "smtp", "jwt",
      "cors", "sql",   "nosql","git",  "npm",  "yarn", "k8s",  "aws",  "gcp",
      "kpi",  "crm",   "seo",  "smm",  "mvp",  "cpu",  "gpu",  "ram",  "ssd",
      "hdd",  "usb",   "pdf",  "mp3",  "mp4",  "png",  "jpg",  "jpeg", "svg",
      "gif",  "ddos",  "iot",  "llm",  "gpt",  "ml",   "ai",   "ui",   "ux",
      "db",   "os",    "io",   "qa",   "ci",   "cd",   "webp", "mvc",  "orm",
      "cdn",  "dom"};
  return s;
}

const std::unordered_set<std::string> &ru_single_letter() {
  static const std::unordered_set<std::string> s = {"а", "в", "и", "к",
                                                    "о", "с", "у", "я"};
  return s;
}

bool all_layout_letters(std::string_view w) {
  Utf8Iter it(w);
  while (it.ok()) {
    if (!LayoutDetector::is_layout_letter(it.next()))
      return false;
  }
  return !w.empty();
}

bool all_letters_or_apos(std::string_view w) {
  Utf8Iter it(w);
  while (it.ok()) {
    uint32_t cp = it.next();
    if (!(utf8_is_letter(cp) || cp == '\''))
      return false;
  }
  return !w.empty();
}

bool all_letters(std::string_view w) {
  Utf8Iter it(w);
  while (it.ok()) {
    if (!utf8_is_letter(it.next()))
      return false;
  }
  return !w.empty();
}

bool contains_char(std::string_view s, uint32_t needle) {
  Utf8Iter it(s);
  while (it.ok()) {
    if (it.next() == needle)
      return true;
  }
  return false;
}

size_t utf8_cp_count(std::string_view s) { return utf8_length(s); }

} // namespace

ContextHint context_hint_of(std::optional<std::string_view> word) {
  if (!word || word->empty())
    return ContextHint::None;
  if (has_cyrillic(*word))
    return ContextHint::Cyrillic;
  if (has_latin_letter(*word))
    return ContextHint::Latin;
  return ContextHint::None;
}

bool LayoutDetector::is_layout_letter(uint32_t cp) {
  if (utf8_is_letter(cp))
    return true;
  std::string s = utf8_encode(cp);
  auto &en = Keymap::en_to_ru();
  auto &ru = Keymap::ru_to_en();
  if (auto f = en.find(s); f != en.end()) {
    Utf8Iter it(f->second);
    if (it.ok() && utf8_is_letter(it.next()))
      return true;
  }
  if (auto f = ru.find(s); f != ru.end()) {
    Utf8Iter it(f->second);
    if (it.ok() && utf8_is_letter(it.next()))
      return true;
  }
  return false;
}

std::string LayoutDetector::letter_core(std::string_view raw) {
  std::string s(raw);
  while (!s.empty()) {
    uint32_t f = utf8_front(s);
    if (is_layout_letter(f) || utf8_is_digit(f))
      break;
    utf8_drop_front(s);
  }
  while (!s.empty()) {
    uint32_t l = utf8_back(s);
    if (is_layout_letter(l) || utf8_is_digit(l))
      break;
    utf8_pop_back(s);
  }
  return s;
}

bool LayoutDetector::is_exception_or_prefix(std::string_view w, bool cyrillic) {
  if (utf8_cp_count(w) < 2)
    return false;
  auto hit = [&](const std::unordered_set<std::string> &set) {
    std::string ws(w);
    if (set.count(ws))
      return true;
    for (auto &e : set) {
      if (e.size() > ws.size() && e.compare(0, ws.size(), ws) == 0)
        return true;
    }
    return false;
  };
  auto &exc = ExceptionStore::shared();
  if (hit(exc.learned()) || hit(exc.ignored()) || hit(ExtraWords::default_keep()))
    return true;
  return cyrillic
             ? (hit(ExtraWords::ru()) || hit(ExtraWords::ru_abbr()) ||
                hit(ExtraWords::ru_short()))
             : (hit(ExtraWords::en()) || hit(ExtraWords::en_keep_short()));
}

SwapDecision LayoutDetector::live_decide(std::string_view raw) {
  std::string core = letter_core(raw);
  while (!core.empty() && utf8_is_digit(utf8_front(core)))
    utf8_drop_front(core);
  while (!core.empty() && utf8_is_digit(utf8_back(core)))
    utf8_pop_back(core);
  std::string w = to_lower_utf8(core);
  if (utf8_cp_count(w) < 4 || !all_layout_letters(w))
    return SwapDecision::keep();
  bool source_cyr = has_cyrillic(w);
  bool source_lat = has_latin_letter(w);
  if (source_cyr == source_lat)
    return SwapDecision::keep();
  bool to_cyr = !source_cyr;
  std::string swapped = to_lower_utf8(Keymap::convert(core, to_cyr));
  if (swapped == w || !all_letters(swapped))
    return SwapDecision::keep();
  auto &data = LayoutData::shared();
  if (source_lat && data.has_word_en(w))
    return SwapDecision::keep();
  if (!source_lat && data.has_word_ru(w))
    return SwapDecision::keep();
  if (is_exception_or_prefix(w, source_cyr))
    return SwapDecision::keep();
  double orig = data.plausibility(w, source_cyr);
  double swap = data.plausibility(swapped, to_cyr);
  if (orig <= live_impossible && swap > orig + live_margin)
    return SwapDecision::convert(to_cyr);
  return SwapDecision::keep();
}

SwapDecision LayoutDetector::mixed_rescue(std::string_view raw) {
  if (!(has_cyrillic(raw) && has_latin_letter(raw)))
    return SwapDecision::keep();
  std::string core = letter_core(raw);
  if (utf8_cp_count(core) < 2 || !all_layout_letters(core))
    return SwapDecision::keep();
  auto to_ru = Keymap::convert(core, true);
  auto to_en = Keymap::convert(core, false);
  auto &data = LayoutData::shared();
  bool ru_ok = !has_latin_letter(to_ru) &&
               data.has_word_ru(to_lower_utf8(to_ru));
  bool en_ok =
      !has_cyrillic(to_en) && data.has_word_en(to_lower_utf8(to_en));
  if (ru_ok && !en_ok)
    return SwapDecision::convert(true);
  if (en_ok && !ru_ok)
    return SwapDecision::convert(false);
  return SwapDecision::keep();
}

SwapDecision LayoutDetector::decide(std::string_view raw,
                                    const ExceptionStore &exceptions,
                                    std::optional<std::string_view> prev,
                                    bool after_caret_jump) {
  auto context = context_hint_of(prev);
  std::string core = letter_core(raw);
  bool had_digits = false;
  {
    Utf8Iter it(core);
    while (it.ok()) {
      if (utf8_is_digit(it.next())) {
        had_digits = true;
        break;
      }
    }
  }
  if (had_digits) {
    while (!core.empty() && utf8_is_digit(utf8_front(core)))
      utf8_drop_front(core);
    while (!core.empty() && utf8_is_digit(utf8_back(core)))
      utf8_pop_back(core);
  }
  std::string w = to_lower_utf8(core);

  if (contains_char(w, '-')) {
    // hyphen terms — whole-word allowlist
    bool source_cyr = has_cyrillic(w);
    bool source_lat = has_latin_letter(w);
    if (source_cyr != source_lat) {
      bool to_cyr = !source_cyr;
      auto swapped = to_lower_utf8(Keymap::convert(core, to_cyr));
      if (ExtraWords::hyphen_terms().count(swapped))
        return SwapDecision::convert(to_cyr);
    }
    return SwapDecision::keep();
  }

  size_t wlen = utf8_cp_count(w);
  size_t min_len = had_digits ? 4 : 1;
  if (wlen < min_len || !all_layout_letters(w))
    return SwapDecision::keep();
  if (exceptions.ignored().count(w) || exceptions.learned().count(w) ||
      ExtraWords::default_keep().count(w))
    return SwapDecision::keep();

  bool source_cyr = has_cyrillic(w);
  bool source_lat = has_latin_letter(w);
  if (source_cyr == source_lat)
    return SwapDecision::keep();

  bool to_cyr = !source_cyr;
  auto swapped = to_lower_utf8(Keymap::convert(core, to_cyr));
  if (swapped == w || !all_letters_or_apos(swapped))
    return SwapDecision::keep();

  auto &data = LayoutData::shared();
  bool source_is_real =
      data.has_word_ru(w) || data.has_word_en(w);

  if (force_swap().count(swapped) && !source_is_real)
    return SwapDecision::convert(to_cyr);
  if (exceptions.force_swap().count(swapped))
    return SwapDecision::convert(to_cyr);
  if (force_swap().count(w) || exceptions.force_swap().count(w))
    return SwapDecision::keep();

  if (wlen >= 2 && source_is_real &&
      !(source_lat && ExtraWords::force_ru_amb().count(w)))
    return SwapDecision::keep();

  if (wlen == 1) {
    if (after_caret_jump && context == ContextHint::None)
      return SwapDecision::keep();
    if (source_lat && ru_single_letter().count(swapped)) {
      if (prev) {
        auto p = to_lower_utf8(*prev);
        if (context == ContextHint::Latin &&
            ExtraWords::label_classifiers().count(p))
          return SwapDecision::keep();
        if (context == ContextHint::Cyrillic &&
            ExtraWords::ru_label_classifiers().count(p))
          return SwapDecision::keep();
      }
      return SwapDecision::convert(true);
    }
    if (source_cyr && context != ContextHint::Cyrillic &&
        (swapped == "i" || swapped == "u" || swapped == "a"))
      return SwapDecision::convert(false);
    return SwapDecision::keep();
  }

  auto en_swap_not_junk = [&] {
    return wlen >= 4 ||
           data.plausibility(swapped, false) > short_en_swap_floor;
  };

  if (source_lat) {
    if (data.has_word_en(w) && !ExtraWords::force_ru_amb().count(w)) {
      if (context == ContextHint::Cyrillic && data.has_word_ru(swapped) &&
          !ExtraWords::en_keep_short().count(w))
        return SwapDecision::convert(true);
      return SwapDecision::keep();
    }
    if (data.has_word_ru(swapped)) {
      if (context == ContextHint::Latin &&
          ExtraWords::en_keep_short().count(w))
        return SwapDecision::keep();
      return SwapDecision::convert(true);
    }
  } else {
    if (ExtraWords::force_en_amb().count(swapped) && !data.has_word_ru(w))
      return SwapDecision::convert(false);
    if (data.has_word_ru(w)) {
      if (context == ContextHint::Latin && data.has_word_en(swapped))
        return SwapDecision::convert(false);
      return SwapDecision::keep();
    }
    if (data.has_word_en(swapped) && en_swap_not_junk())
      return SwapDecision::convert(false);
  }

  if (wlen < 4)
    return SwapDecision::keep();
  double orig_score = data.plausibility(w, source_cyr);
  double swap_score = data.plausibility(swapped, to_cyr);
  if (swap_score > orig_score + margin)
    return SwapDecision::convert(to_cyr);
  if (orig_score <= -19.0 && swap_score > orig_score + 1.0)
    return SwapDecision::convert(to_cyr);
  return SwapDecision::keep();
}

} // namespace keyboop
