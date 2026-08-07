#include "user_settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace keyboop {
namespace fs = std::filesystem;

std::string user_config_path() {
  const char *home = std::getenv("HOME");
  if (!home)
    return {};
  return std::string(home) + "/.config/keyboop/config";
}

UserSettings load_user_settings() {
  UserSettings out;
  auto path = user_config_path();
  if (path.empty())
    return out;
  std::ifstream in(path);
  if (!in)
    return out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("auto=", 0) == 0) {
      auto v = line.substr(5);
      out.auto_enabled = !(v == "0" || v == "off" || v == "false" || v == "no");
    }
  }
  return out;
}

bool save_user_settings(const UserSettings &s, std::string *err) {
  auto path = user_config_path();
  if (path.empty()) {
    if (err)
      *err = "HOME not set";
    return false;
  }
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    if (err)
      *err = "cannot write " + path;
    return false;
  }
  out << "auto=" << (s.auto_enabled ? "on" : "off") << "\n";
  return true;
}

} // namespace keyboop
