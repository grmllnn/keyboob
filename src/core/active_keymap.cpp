#include "active_keymap.hpp"
#include "keymap.hpp"
#include "utf8.hpp"

namespace keyboop {

ActiveKeymap &ActiveKeymap::shared() {
  static ActiveKeymap inst;
  return inst;
}

ActiveKeymap::ActiveKeymap() { use_builtin_us_ru(); }

void ActiveKeymap::use_builtin_us_ru() {
  to_cyr_ = Keymap::builtin_en_to_ru();
  to_lat_ = Keymap::builtin_ru_to_en();
  latin_ = XkbLayoutId::parse("us");
  cyr_ = XkbLayoutId::parse("ru");
  builtin_ = true;
}

void ActiveKeymap::use_pair(XkbPairMaps maps) {
  if (!maps.ok)
    return;
  to_cyr_ = std::move(maps.to_cyrillic);
  to_lat_ = std::move(maps.to_latin);
  latin_ = std::move(maps.latin);
  cyr_ = std::move(maps.cyrillic);
  builtin_ = false;
}

std::string ActiveKeymap::convert(std::string_view text, bool to_cyrillic) const {
  const auto &map = to_cyrillic ? to_cyr_ : to_lat_;
  std::string out;
  out.reserve(text.size());
  Utf8Iter it(text);
  std::string ch;
  ch.reserve(4);
  while (it.ok()) {
    const unsigned char *start = it.p;
    it.next();
    ch.assign(reinterpret_cast<const char *>(start),
              static_cast<size_t>(it.p - start));
    auto f = map.find(ch);
    if (f != map.end())
      out.append(f->second);
    else
      out.append(ch);
  }
  return out;
}

} // namespace keyboop
