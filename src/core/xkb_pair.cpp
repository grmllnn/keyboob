#include "xkb_pair.hpp"
#include "keymap.hpp"
#include "utf8.hpp"

#include <xkbcommon/xkbcommon.h>

#include <cstring>
#include <unordered_map>

namespace keyboop {
namespace {

xkb_keymap *load_keymap(xkb_context *ctx, const XkbLayoutId &id) {
  xkb_rule_names names{};
  names.layout = id.layout.c_str();
  names.variant = id.variant.empty() ? nullptr : id.variant.c_str();
  return xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

std::string keysym_to_utf8_str(xkb_keysym_t sym) {
  char buf[16]{};
  int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
  if (n < 1 || buf[0] == '\0')
    return {};
  // n includes trailing NUL
  return std::string(buf);
}

bool is_useful_char(const std::string &s) {
  if (s.empty())
    return false;
  uint32_t cp = utf8_front(s);
  if (cp < 0x20 || cp == 0x7f)
    return false;
  return true;
}

} // namespace

XkbLayoutId XkbLayoutId::parse(std::string_view id) {
  XkbLayoutId out;
  if (id.empty())
    return out;
  auto plus = id.find('+');
  if (plus == std::string_view::npos) {
    out.layout = std::string(id);
  } else {
    out.layout = std::string(id.substr(0, plus));
    out.variant = std::string(id.substr(plus + 1));
  }
  return out;
}

std::string XkbLayoutId::id() const {
  if (variant.empty())
    return layout;
  return layout + "+" + variant;
}

LayoutScript detect_layout_script(const XkbLayoutId &id) {
  // Cache: loading an xkb keymap is expensive; scripts don't change at runtime.
  static std::unordered_map<std::string, LayoutScript> cache;
  const std::string key = id.id();
  if (auto it = cache.find(key); it != cache.end())
    return it->second;

  auto *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!ctx)
    return LayoutScript::Unknown;
  auto *km = load_keymap(ctx, id);
  if (!km) {
    xkb_context_unref(ctx);
    return LayoutScript::Unknown;
  }
  int latin = 0, cyr = 0;
  auto min = xkb_keymap_min_keycode(km);
  auto max = xkb_keymap_max_keycode(km);
  for (auto kc = min; kc <= max; ++kc) {
    if (!xkb_keymap_key_get_name(km, kc))
      continue;
    const xkb_keysym_t *syms = nullptr;
    int n = xkb_keymap_key_get_syms_by_level(km, kc, 0, 0, &syms);
    if (n < 1 || !syms)
      continue;
    auto s = keysym_to_utf8_str(syms[0]);
    if (s.empty())
      continue;
    if (has_latin_letter(s))
      ++latin;
    if (has_cyrillic(s))
      ++cyr;
  }
  xkb_keymap_unref(km);
  xkb_context_unref(ctx);
  LayoutScript result = LayoutScript::Unknown;
  if (cyr > latin && cyr >= 5)
    result = LayoutScript::Cyrillic;
  else if (latin > cyr && latin >= 5)
    result = LayoutScript::Latin;
  cache.emplace(key, result);
  return result;
}

XkbPairMaps build_xkb_pair(const XkbLayoutId &latin, const XkbLayoutId &cyrillic) {
  XkbPairMaps out;
  out.latin = latin;
  out.cyrillic = cyrillic;
  if (latin.empty() || cyrillic.empty())
    return out;

  // Standard US↔RU: trust the builtin table. xkb level-1 on the same key as
  // «б» also defines RU «,» ↔ US «?», which overwrites «,»→«б» and turns
  // hf,jnftn into ра,отает.
  if (latin.layout == "us" && latin.variant.empty() &&
      cyrillic.layout == "ru" && cyrillic.variant.empty()) {
    out.to_cyrillic = Keymap::builtin_en_to_ru();
    out.to_latin = Keymap::builtin_ru_to_en();
    out.ok = true;
    return out;
  }

  auto *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!ctx)
    return out;
  auto *km_lat = load_keymap(ctx, latin);
  auto *km_cyr = load_keymap(ctx, cyrillic);
  if (!km_lat || !km_cyr) {
    if (km_lat)
      xkb_keymap_unref(km_lat);
    if (km_cyr)
      xkb_keymap_unref(km_cyr);
    xkb_context_unref(ctx);
    return out;
  }

  auto min = xkb_keymap_min_keycode(km_lat);
  auto max = xkb_keymap_max_keycode(km_lat);
  for (auto kc = min; kc <= max; ++kc) {
    if (!xkb_keymap_key_get_name(km_lat, kc))
      continue;
    // Level 0 first; emplace keeps the unshifted binding on conflicts.
    for (int level = 0; level < 2; ++level) {
      const xkb_keysym_t *sa = nullptr;
      const xkb_keysym_t *sb = nullptr;
      int ca = xkb_keymap_key_get_syms_by_level(km_lat, kc, 0, level, &sa);
      int cb = xkb_keymap_key_get_syms_by_level(km_cyr, kc, 0, level, &sb);
      if (ca < 1 || cb < 1 || !sa || !sb)
        continue;
      auto a = keysym_to_utf8_str(sa[0]);
      auto b = keysym_to_utf8_str(sb[0]);
      if (!is_useful_char(a) || !is_useful_char(b) || a == b)
        continue;
      out.to_cyrillic.emplace(a, b);
      out.to_latin.emplace(b, a);
    }
  }

  xkb_keymap_unref(km_lat);
  xkb_keymap_unref(km_cyr);
  xkb_context_unref(ctx);
  out.ok = out.to_cyrillic.size() >= 20;
  return out;
}

XkbPairMaps pick_latin_cyrillic_pair(const std::vector<XkbLayoutId> &layouts,
                                     const XkbLayoutId *active) {
  XkbLayoutId latin, cyr;
  // Prefer opposite of active when possible.
  LayoutScript active_script = LayoutScript::Unknown;
  if (active && !active->empty())
    active_script = detect_layout_script(*active);

  for (const auto &id : layouts) {
    auto sc = detect_layout_script(id);
    if (sc == LayoutScript::Latin && latin.empty())
      latin = id;
    if (sc == LayoutScript::Cyrillic && cyr.empty())
      cyr = id;
  }

  // If multiple, and active is set, pick first opposite as the "other" side
  // already handled by first-of-each; refine: if active is latin, prefer first cyr
  // (already). If active is cyrillic and we have multiple latins, first latin is fine.
  (void)active_script;

  if (latin.empty() || cyr.empty())
    return {};
  return build_xkb_pair(latin, cyr);
}

} // namespace keyboop
