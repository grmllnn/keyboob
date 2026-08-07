#pragma once
#include <string>

namespace keyboop {

struct UserSettings {
  bool auto_enabled = true; // space/enter/tab auto-convert
};

std::string user_config_path();
UserSettings load_user_settings();
bool save_user_settings(const UserSettings &s, std::string *err = nullptr);

} // namespace keyboop
