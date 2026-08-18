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
#include <filesystem>
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
      << "  hotkey [SPEC]         manual convert combo (default Control+Alt+k)\n"
      << "  doctor                session / IM / layouts / extension / paths\n"
      << "  version\n";
}

static std::string env_or(const char *k) {
  const char *v = std::getenv(k);
  return v ? v : "";
}

static bool path_ok(const std::string &p) {
  std::error_code ec;
  return !p.empty() && std::filesystem::exists(p, ec);
}

static bool have_bin(const char *name) {
  const char *path = std::getenv("PATH");
  if (!path || !name || !name[0])
    return false;
  std::string paths(path);
  size_t i = 0;
  while (i < paths.size()) {
    size_t sep = paths.find(':', i);
    if (sep == std::string::npos)
      sep = paths.size();
    std::string dir = paths.substr(i, sep - i);
    if (!dir.empty() && path_ok(dir + "/" + name))
      return true;
    i = sep + 1;
  }
  return false;
}

static std::string detect_im_module() {
  auto gtk = env_or("GTK_IM_MODULE");
  auto qt = env_or("QT_IM_MODULE");
  auto xmod = env_or("XMODIFIERS");
  auto blob = gtk + " " + qt + " " + xmod;
  if (blob.find("fcitx") != std::string::npos)
    return "fcitx";
  if (blob.find("ibus") != std::string::npos)
    return "ibus";
  if (!env_or("IBUS_ADDRESS").empty())
    return "ibus";
  return "unknown";
}

static std::string recommend_backend(const std::string &desktop,
                                     const std::string &im) {
  if (desktop.find("GNOME") != std::string::npos)
    return "ibus";
  if (im == "fcitx" || have_bin("fcitx5"))
    return "fcitx";
  if (im == "ibus" || have_bin("ibus"))
    return "ibus";
  return "none";
}

static int cmd_doctor() {
  const std::string session = env_or("XDG_SESSION_TYPE");
  const std::string desktop = env_or("XDG_CURRENT_DESKTOP");
  const std::string wayland = env_or("WAYLAND_DISPLAY");
  const std::string display = env_or("DISPLAY");
  const std::string im = detect_im_module();
  const std::string backend = recommend_backend(desktop, im);

  std::cout << "session: " << (session.empty() ? "unknown" : session) << "\n";
  std::cout << "desktop: " << (desktop.empty() ? "unknown" : desktop) << "\n";
  std::cout << "display: "
            << (!wayland.empty() ? "wayland" : (!display.empty() ? "x11" : "none"))
            << "\n";
  std::cout << "im: " << im << " gtk=" << env_or("GTK_IM_MODULE")
            << " qt=" << env_or("QT_IM_MODULE") << "\n";
  std::cout << "backend: " << backend << "\n";

  auto sources = keyboop::read_gnome_input_sources();
  std::cout << "layouts:";
  if (sources.empty()) {
    std::cout << " (none / not GNOME)\n";
  } else {
    std::cout << "\n";
    for (const auto &s : sources) {
      const char *k = s.kind == keyboop::InputSource::Kind::Xkb         ? "xkb"
                      : s.kind == keyboop::InputSource::Kind::IBusKeyboop
                          ? "keyboop"
                          : "other";
      std::cout << "  " << k << " " << s.id << "\n";
    }
    auto layouts = keyboop::layout_ids_from_sources(sources);
    auto pair = keyboop::pick_latin_cyrillic_pair(layouts, nullptr);
    if (pair.ok)
      std::cout << "pair: " << pair.latin.id() << " <-> " << pair.cyrillic.id()
                << "\n";
    else
      std::cout << "pair: none\n";
  }

  auto ext = keyboop::gnome_switch_extension_status();
  std::cout << "extension: " << (ext.ok ? "ok" : "fail") << " " << ext.detail
            << "\n";

  {
    auto us = keyboop::load_user_settings();
    auto cfg = keyboop::user_config_path();
    std::cout << "auto: " << (us.auto_enabled ? "on" : "off")
              << " hotkey=" << us.hotkey << " " << cfg
              << (path_ok(cfg) ? " (ok)" : " (missing)") << "\n";
  }

  std::string sel = "none";
  if (!wayland.empty() && have_bin("wl-paste"))
    sel = "wl-paste";
  else if (!display.empty() && have_bin("xclip"))
    sel = "xclip";
  else if (have_bin("wl-paste"))
    sel = "wl-paste";
  else if (have_bin("xclip"))
    sel = "xclip";
  std::cout << "selection: " << sel << "\n";

  std::cout << "paths:\n";
  for (const auto &d : keyboop::layout_data_search_paths()) {
    std::cout << "  data: " << d << (path_ok(d + "/words_en.json") ? " (ok)" : " (missing)")
              << "\n";
  }
#ifdef KEYBOOP_INSTALL_LIBEXECDIR
  {
    std::string p = std::string(KEYBOOP_INSTALL_LIBEXECDIR) +
                    "/ibus-engine-keyboop";
    std::cout << "  libexec: " << p << (path_ok(p) ? " (ok)" : " (missing)")
              << "\n";
  }
#endif
  for (const auto &p : keyboop::ibus_component_candidate_paths()) {
    std::cout << "  ibus-component: " << p
              << (path_ok(p) ? " (ok)" : " (missing)") << "\n";
  }
  for (const auto &p : keyboop::gnome_extension_candidate_paths()) {
    std::cout << "  extension-dir: " << p
              << (path_ok(p + "/extension.js") ? " (ok)" : " (missing)")
              << "\n";
  }
  return 0;
}

static void load_data() {
  keyboop::LayoutData::shared().load_from_search_path();
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
  if (cmd == "doctor")
    return cmd_doctor();

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
              << " hotkey=" << s.hotkey << "\n"
              << "config: " << keyboop::user_config_path() << "\n"
              << "Takes effect on the next key.\n";
    return 0;
  }
  if (cmd == "hotkey") {
    auto s = keyboop::load_user_settings();
    if (argc >= 3) {
      keyboop::HotkeySpec spec;
      if (!keyboop::parse_hotkey(argv[2], &spec) || !spec.ok()) {
        std::cerr << "usage: keyboopctl hotkey Control+Alt+k\n";
        return 1;
      }
      s.hotkey = keyboop::format_hotkey(spec);
      std::string err;
      if (!keyboop::save_user_settings(s, &err)) {
        std::cerr << "hotkey: " << err << "\n";
        return 1;
      }
    }
    std::cout << "hotkey=" << s.hotkey << "\n"
              << "config: " << keyboop::user_config_path() << "\n"
              << "Takes effect on the next key.\n";
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
