#pragma once
#include <string>
#include <string_view>

namespace keyboop {

struct HotkeySpec {
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
  bool super = false;
  std::string key; // lowercase name: "k", "space", "f9"
  bool ok() const { return !key.empty(); }
};

struct UserSettings {
  bool auto_enabled = true; // space/enter/tab auto-convert
  std::string hotkey = "Control+Alt+k";
};

std::string user_config_path();
UserSettings load_user_settings();
bool save_user_settings(const UserSettings &s, std::string *err = nullptr);

bool parse_hotkey(std::string_view spec, HotkeySpec *out);
std::string format_hotkey(const HotkeySpec &h);

} // namespace keyboop
