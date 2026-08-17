#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keyboop {

inline bool has_cyrillic(std::string_view s) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
  const unsigned char *end = p + s.size();
  while (p < end) {
    uint32_t cp;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && p + 1 < end) {
      cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && p + 2 < end) {
      cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else if ((*p & 0xF8) == 0xF0 && p + 3 < end) {
      cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) |
           (p[3] & 0x3F);
      p += 4;
    } else {
      ++p;
      continue;
    }
    if (cp >= 0x0400 && cp <= 0x04FF)
      return true;
  }
  return false;
}

inline bool has_latin_letter(std::string_view s) {
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
      return true;
  }
  return false;
}

inline std::string to_lower_utf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
  const unsigned char *end = p + s.size();
  while (p < end) {
    if (*p < 0x80) {
      char c = static_cast<char>(*p++);
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
      out.push_back(c);
    } else if ((*p & 0xE0) == 0xC0 && p + 1 < end) {
      uint32_t cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
      if (cp >= 0x401 && cp <= 0x40F)
        cp += 0x50;
      else if (cp >= 0x410 && cp <= 0x42F)
        cp += 0x20;
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if ((*p & 0xF0) == 0xE0 && p + 2 < end) {
      out.append(reinterpret_cast<const char *>(p), 3);
      p += 3;
    } else if ((*p & 0xF8) == 0xF0 && p + 3 < end) {
      out.append(reinterpret_cast<const char *>(p), 4);
      p += 4;
    } else {
      out.push_back(static_cast<char>(*p++));
    }
  }
  return out;
}

inline size_t utf8_length(std::string_view s) {
  size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80)
      ++n;
  }
  return n;
}

inline bool utf8_is_letter(uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
    return true;
  return cp >= 0x0400 && cp <= 0x04FF;
}

inline bool utf8_is_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

struct Utf8Iter {
  const unsigned char *p{};
  const unsigned char *end{};
  explicit Utf8Iter(std::string_view s)
      : p(reinterpret_cast<const unsigned char *>(s.data())),
        end(p + s.size()) {}
  bool ok() const { return p < end; }
  uint32_t peek() const {
    Utf8Iter tmp = *this;
    return tmp.next();
  }
  uint32_t next() {
    if (p >= end)
      return 0;
    if (*p < 0x80)
      return *p++;
    if ((*p & 0xE0) == 0xC0 && p + 1 < end) {
      uint32_t cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
      return cp;
    }
    if ((*p & 0xF0) == 0xE0 && p + 2 < end) {
      uint32_t cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
      return cp;
    }
    if ((*p & 0xF8) == 0xF0 && p + 3 < end) {
      uint32_t cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                    ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      p += 4;
      return cp;
    }
    return *p++;
  }
};

inline std::string utf8_encode(uint32_t cp) {
  std::string o;
  if (cp < 0x80) {
    o.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return o;
}

inline uint32_t utf8_to_upper(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z')
    return cp - 'a' + 'A';
  if (cp == 0x451)
    return 0x401;
  if (cp >= 0x430 && cp <= 0x44F)
    return cp - 0x20;
  return cp;
}

/// Drop last UTF-8 codepoint from string (mutates).
inline void utf8_pop_back(std::string &s) {
  if (s.empty())
    return;
  size_t i = s.size();
  size_t count = 0;
  do {
    --i;
    ++count;
  } while (i > 0 && count < 4 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  s.resize(i);
}

/// First UTF-8 codepoint, or 0 if empty.
inline uint32_t utf8_front(std::string_view s) {
  if (s.empty())
    return 0;
  return Utf8Iter(s).next();
}

/// Last UTF-8 codepoint, or 0 if empty.
inline uint32_t utf8_back(std::string_view s) {
  if (s.empty())
    return 0;
  size_t i = s.size();
  size_t count = 0;
  do {
    --i;
    ++count;
  } while (i > 0 && count < 4 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  return Utf8Iter(s.substr(i)).next();
}

inline void utf8_drop_front(std::string &s) {
  if (s.empty())
    return;
  Utf8Iter it(s);
  it.next();
  s.erase(0, static_cast<size_t>(it.p -
                                 reinterpret_cast<const unsigned char *>(s.data())));
}

/// Letter script: 0 = none, 1 = Latin, 2 = Cyrillic.
inline int letter_script(uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
    return 1;
  if (cp >= 0x0400 && cp <= 0x04FF)
    return 2;
  return 0;
}

/// Hotkey target: whole phrase if mono-script; else trailing run of one script
/// (so "привет андрей krfr ltkf" → "krfr ltkf", not a full re-flip).
/// Edge whitespace is stripped so the separator space is not deleted/reinserted.
inline std::string layout_flip_suffix(std::string_view phrase) {
  if (phrase.empty())
    return {};
  std::string body;
  if (!has_cyrillic(phrase) || !has_latin_letter(phrase))
    body = std::string(phrase);
  else {
    struct Cp {
      size_t off;
      size_t len;
      uint32_t cp;
    };
    std::vector<Cp> cps;
    cps.reserve(phrase.size());
    {
      Utf8Iter it(phrase);
      while (it.ok()) {
        const unsigned char *start = it.p;
        uint32_t cp = it.next();
        cps.push_back(
            {static_cast<size_t>(
                 start - reinterpret_cast<const unsigned char *>(phrase.data())),
             static_cast<size_t>(it.p - start), cp});
      }
    }
    int want = 0;
    for (int i = static_cast<int>(cps.size()) - 1; i >= 0; --i) {
      want = letter_script(cps[static_cast<size_t>(i)].cp);
      if (want)
        break;
    }
    if (!want)
      return std::string(phrase);

    size_t start_i = 0;
    for (int i = static_cast<int>(cps.size()) - 1; i >= 0; --i) {
      int sc = letter_script(cps[static_cast<size_t>(i)].cp);
      if (sc && sc != want) {
        start_i = static_cast<size_t>(i + 1);
        break;
      }
    }
    body = std::string(phrase.substr(cps[start_i].off));
  }
  while (!body.empty() && (body.front() == ' ' || body.front() == '\t'))
    body.erase(body.begin());
  while (!body.empty() && (body.back() == ' ' || body.back() == '\t'))
    body.pop_back();
  return body;
}

/// Same-script run touching the caret (char offset), for mid-phrase hotkey.
/// Spaces glue same-script words; edge spaces are trimmed out of the range.
struct LayoutFlipAt {
  size_t start_cp = 0; // inclusive, in characters
  size_t end_cp = 0;   // exclusive
  std::string text;
};

inline LayoutFlipAt layout_flip_at(std::string_view phrase, size_t cursor_cp) {
  LayoutFlipAt out;
  struct Cp {
    size_t off;
    uint32_t cp;
  };
  std::vector<Cp> cps;
  cps.reserve(phrase.size());
  {
    Utf8Iter it(phrase);
    while (it.ok()) {
      const unsigned char *start = it.p;
      uint32_t cp = it.next();
      cps.push_back({static_cast<size_t>(
                         start - reinterpret_cast<const unsigned char *>(
                                     phrase.data())),
                     cp});
    }
  }
  if (cps.empty())
    return out;
  if (cursor_cp > cps.size())
    cursor_cp = cps.size();

  int want = 0;
  size_t anchor = 0;
  bool found = false;

  // 1. Priority: check character right under cursor
  if (cursor_cp < cps.size()) {
    int sc = letter_script(cps[cursor_cp].cp);
    if (sc) {
      want = sc;
      anchor = cursor_cp;
      found = true;
    }
  }
  // 2. If character under cursor is not a letter, check character before cursor
  if (!found && cursor_cp > 0) {
    int sc = letter_script(cps[cursor_cp - 1].cp);
    if (sc) {
      want = sc;
      anchor = cursor_cp - 1;
      found = true;
    }
  }
  // 3. Fallback: look left then right within non-whitespace
  if (!found) {
    for (size_t i = cursor_cp; i > 0; --i) {
      if (cps[i - 1].cp == ' ' || cps[i - 1].cp == '\t' || cps[i - 1].cp == '\n')
        break;
      int sc = letter_script(cps[i - 1].cp);
      if (sc) {
        want = sc;
        anchor = i - 1;
        found = true;
        break;
      }
    }
  }
  if (!found) {
    for (size_t i = cursor_cp; i < cps.size(); ++i) {
      if (cps[i].cp == ' ' || cps[i].cp == '\t' || cps[i].cp == '\n')
        break;
      int sc = letter_script(cps[i].cp);
      if (sc) {
        want = sc;
        anchor = i;
        found = true;
        break;
      }
    }
  }
  if (!found)
    return out;

  size_t lo = anchor;
  size_t hi = anchor + 1;

  // Expanding left: allow spaces only if preceded by the same script (want)
  while (lo > 0) {
    uint32_t cp = cps[lo - 1].cp;
    if (cp == '\n' || cp == '\r')
      break;
    if (cp == ' ' || cp == '\t') {
      size_t k = lo - 1;
      while (k > 0 && (cps[k - 1].cp == ' ' || cps[k - 1].cp == '\t'))
        --k;
      if (k > 0) {
        int sc = letter_script(cps[k - 1].cp);
        if (sc == want) {
          lo = k;
          continue;
        }
      }
      break;
    }
    int sc = letter_script(cp);
    if (sc == want || sc == 0)
      --lo;
    else
      break;
  }

  // Expanding right: allow spaces only if followed by the same script (want)
  while (hi < cps.size()) {
    uint32_t cp = cps[hi].cp;
    if (cp == '\n' || cp == '\r')
      break;
    if (cp == ' ' || cp == '\t') {
      size_t k = hi + 1;
      while (k < cps.size() && (cps[k].cp == ' ' || cps[k].cp == '\t'))
        ++k;
      if (k < cps.size()) {
        int sc = letter_script(cps[k].cp);
        if (sc == want) {
          hi = k;
          continue;
        }
      }
      break;
    }
    int sc = letter_script(cp);
    if (sc == want || sc == 0)
      ++hi;
    else
      break;
  }

  while (lo < hi && (cps[lo].cp == ' ' || cps[lo].cp == '\t'))
    ++lo;
  while (hi > lo && (cps[hi - 1].cp == ' ' || cps[hi - 1].cp == '\t'))
    --hi;
  if (lo >= hi)
    return out;

  out.start_cp = lo;
  out.end_cp = hi;
  const size_t byte0 = cps[lo].off;
  const size_t byte1 =
      (hi < cps.size()) ? cps[hi].off : phrase.size();
  out.text = std::string(phrase.substr(byte0, byte1 - byte0));
  return out;
}

} // namespace keyboop
