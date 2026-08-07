#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace keyboop {

/// US ↔ Russian (ЙЦУКЕН) static pairs — port of Sources/Keyboop/Keymap.swift.
class Keymap {
public:
  static const std::unordered_map<std::string, std::string> &en_to_ru();
  static const std::unordered_map<std::string, std::string> &ru_to_en();

  static std::string convert(std::string_view text, bool to_cyrillic);

  /// Smart convert: keep trailing punctuation unless full word is valid target.
  static std::string smart_convert(
      std::string_view word, bool to_cyrillic,
      const std::function<bool(const std::string &)> &is_valid_target = {});

  static std::string core(std::string_view word);

  static bool is_trailing_punct(uint32_t cp);
};

} // namespace keyboop
