#include "active_keymap.hpp"
#include "engine.hpp"
#include "gnome_sources.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"
#include "layout_detector.hpp"
#include "user_settings.hpp"
#include "utf8.hpp"
#include "xkb_pair.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void usage() {
  std::cerr
      << "keyboopctl — helpers for Keyboop (Linux)\n"
      << "  convert <word>        convert via active keymap\n"
      << "  decide <word>         detector decision (keep|to-ru|to-en)\n"
      << "  gnome-enable          switch GNOME sources to Keyboop IBus engines\n"
      << "  gnome-disable         restore previous GNOME xkb sources\n"
      << "  layouts               show detected GNOME layouts + pair\n"
      << "  auto [on|off]         auto-convert on space (default on)\n"
      << "  version\n";
}

static void load_data() {
  const char *dir = std::getenv("KEYBOOP_DATA_DIR");
  std::string path = dir ? dir : KEYBOOP_DEFAULT_DATA_DIR;
  keyboop::LayoutData::shared().load(path);
}

static void sync_keymap_from_gnome() {
  auto layouts =
      keyboop::layout_ids_from_sources(keyboop::read_gnome_input_sources());
  if (layouts.empty())
    return;
  auto pair = keyboop::pick_latin_cyrillic_pair(layouts, nullptr);
  if (pair.ok)
    keyboop::ActiveKeymap::shared().use_pair(std::move(pair));
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 1;
  }
  std::string cmd = argv[1];
  if (cmd == "version") {
    std::cout << "keyboopctl 0.1.0\n";
    return 0;
  }

  if (cmd == "gnome-enable") {
    std::string err;
    if (!keyboop::gnome_enable_keyboop(&err)) {
      std::cerr << "gnome-enable failed: " << err << "\n";
      return 1;
    }
    std::string ext_err;
    if (!keyboop::install_keyboop_switch_extension(&ext_err))
      std::cerr << "note: switch extension: " << ext_err << "\n";
    std::cout << "GNOME input sources → Keyboop IBus engines.\n"
              << "Installed keyboop-switch@keyboop (layout switch after convert).\n"
              << "One-time: log out and back in (or reboot) so GNOME loads it.\n"
              << "Then: ibus restart\n";
    return 0;
  }
  if (cmd == "gnome-disable") {
    std::string err;
    if (!keyboop::gnome_disable_keyboop(&err)) {
      std::cerr << "gnome-disable failed: " << err << "\n";
      return 1;
    }
    std::cout << "GNOME input sources restored.\n";
    return 0;
  }
  if (cmd == "auto") {
    auto s = keyboop::load_user_settings();
    if (argc >= 3) {
      std::string v = argv[2];
      if (v == "on" || v == "1" || v == "true")
        s.auto_enabled = true;
      else if (v == "off" || v == "0" || v == "false")
        s.auto_enabled = false;
      else {
        std::cerr << "usage: keyboopctl auto [on|off]\n";
        return 1;
      }
      std::string err;
      if (!keyboop::save_user_settings(s, &err)) {
        std::cerr << "auto: " << err << "\n";
        return 1;
      }
    }
    std::cout << "auto=" << (s.auto_enabled ? "on" : "off")
              << " (hotkey Ctrl+Alt+K always works)\n"
              << "config: " << keyboop::user_config_path() << "\n"
              << "Reload: focus another window or ibus restart\n";
    return 0;
  }
  if (cmd == "layouts") {
    auto sources = keyboop::read_gnome_input_sources();
    std::cout << "sources (" << sources.size() << "):\n";
    for (const auto &s : sources) {
      const char *k = s.kind == keyboop::InputSource::Kind::Xkb       ? "xkb"
                      : s.kind == keyboop::InputSource::Kind::IBusKeyboop
                          ? "keyboop"
                          : "other";
      std::cout << "  " << k << " " << s.id << "\n";
    }
    auto layouts = keyboop::layout_ids_from_sources(sources);
    auto pair = keyboop::pick_latin_cyrillic_pair(layouts, nullptr);
    if (pair.ok) {
      std::cout << "pair: " << pair.latin.id() << " ↔ " << pair.cyrillic.id()
                << " (map " << pair.to_cyrillic.size() << ")\n";
      auto sample = keyboop::ActiveKeymap::shared();
      sample.use_pair(pair);
      std::cout << "sample: ghbdtn → " << sample.convert("ghbdtn", true)
                << "\n";
    } else {
      std::cout << "pair: none (need Latin + Cyrillic among sources)\n";
    }
    return 0;
  }

  load_data();
  sync_keymap_from_gnome();

  if (cmd == "convert" && argc >= 3) {
    std::string w = argv[2];
    bool to_cyr = keyboop::has_latin_letter(w) && !keyboop::has_cyrillic(w);
    if (keyboop::has_cyrillic(w) && !keyboop::has_latin_letter(w))
      to_cyr = false;
    std::cout << keyboop::Keymap::convert(w, to_cyr) << "\n";
    return 0;
  }
  if (cmd == "decide" && argc >= 3) {
    keyboop::ExceptionStore exc;
    auto d = keyboop::LayoutDetector::decide(argv[2], exc);
    if (d.is_keep())
      std::cout << "keep\n";
    else
      std::cout << (d.to_cyrillic ? "to-ru\n" : "to-en\n");
    return 0;
  }
  usage();
  return 1;
}
