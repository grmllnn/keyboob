#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keyboop {

struct XkbLayoutId {
  std::string layout;  // "us", "ru"
  std::string variant; // "", "phonetic"

  static XkbLayoutId parse(std::string_view id);
  std::string id() const; // "ru" or "ru+phonetic"
  bool empty() const { return layout.empty(); }
};

enum class LayoutScript { Unknown, Latin, Cyrillic };

LayoutScript detect_layout_script(const XkbLayoutId &id);

struct XkbPairMaps {
  std::unordered_map<std::string, std::string> to_cyrillic;
  std::unordered_map<std::string, std::string> to_latin;
  XkbLayoutId latin;
  XkbLayoutId cyrillic;
  bool ok = false;
};

/// Build char↔char maps by matching keycodes across two xkb layouts.
XkbPairMaps build_xkb_pair(const XkbLayoutId &latin, const XkbLayoutId &cyrillic);

/// Among layout ids, pick first Latin + first Cyrillic (relative to active if set).
XkbPairMaps pick_latin_cyrillic_pair(const std::vector<XkbLayoutId> &layouts,
                                     const XkbLayoutId *active = nullptr);

/// Fcitx5 `keyboard-us` / `keyboard-ru-phonetic` → XkbLayoutId.
XkbLayoutId parse_fcitx_im_name(std::string_view name);

/// Exact IM name for `target` in the current Fcitx group (layout+variant, then layout).
std::string fcitx_im_for_layout(const std::vector<std::string> &im_names,
                                const XkbLayoutId &target);

} // namespace keyboop
