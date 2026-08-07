#pragma once
#include "xkb_pair.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace keyboop {

/// Runtime keymap used by Keymap::convert. Defaults to builtin US↔RU.
class ActiveKeymap {
public:
  static ActiveKeymap &shared();

  void use_builtin_us_ru();
  void use_pair(XkbPairMaps maps);

  const std::unordered_map<std::string, std::string> &to_cyrillic() const {
    return to_cyr_;
  }
  const std::unordered_map<std::string, std::string> &to_latin() const {
    return to_lat_;
  }

  const XkbLayoutId &latin_layout() const { return latin_; }
  const XkbLayoutId &cyrillic_layout() const { return cyr_; }
  bool is_builtin() const { return builtin_; }
  bool ready() const { return !to_cyr_.empty() && !to_lat_.empty(); }

  std::string convert(std::string_view text, bool to_cyrillic) const;

private:
  ActiveKeymap();
  std::unordered_map<std::string, std::string> to_cyr_;
  std::unordered_map<std::string, std::string> to_lat_;
  XkbLayoutId latin_;
  XkbLayoutId cyr_;
  bool builtin_ = true;
};

} // namespace keyboop
