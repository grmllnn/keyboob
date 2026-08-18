#include "user_settings.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace keyboop {
namespace fs = std::filesystem;

namespace {

std::string trim(std::string v) {
  while (!v.empty() &&
         (v.back() == '\r' || v.back() == ' ' || v.back() == '\t'))
    v.pop_back();
  size_t i = 0;
  while (i < v.size() && (v[i] == ' ' || v[i] == '\t'))
    ++i;
  return v.substr(i);
}

std::string lower_ascii(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

std::string user_config_path() {
  const char *home = std::getenv("HOME");
  if (!home)
    return {};
  return std::string(home) + "/.config/keyboop/config";
}

bool parse_hotkey(std::string_view spec, HotkeySpec *out) {
  HotkeySpec h;
  std::string s = trim(std::string(spec));
  if (s.empty() || !out)
    return false;
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= s.size()) {
    size_t plus = s.find('+', start);
    if (plus == std::string::npos)
      plus = s.size();
    auto part = trim(s.substr(start, plus - start));
    if (!part.empty())
      parts.push_back(std::move(part));
    if (plus == s.size())
      break;
    start = plus + 1;
  }
  if (parts.empty())
    return false;
  h.key = parts.back();
  if (h.key.size() == 1)
    h.key = lower_ascii(h.key);
  if (h.key.empty())
    return false;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    auto m = lower_ascii(parts[i]);
    if (m == "control" || m == "ctrl")
      h.ctrl = true;
    else if (m == "alt" || m == "mod1" || m == "meta")
      h.alt = true;
    else if (m == "shift")
      h.shift = true;
    else if (m == "super" || m == "mod4" || m == "win")
      h.super = true;
    else
      return false;
  }
  *out = std::move(h);
  return true;
}

std::string format_hotkey(const HotkeySpec &h) {
  std::string out;
  if (h.ctrl)
    out += "Control+";
  if (h.alt)
    out += "Alt+";
  if (h.shift)
    out += "Shift+";
  if (h.super)
    out += "Super+";
  out += h.key;
  return out;
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
      auto v = lower_ascii(trim(line.substr(5)));
      out.auto_enabled = !(v == "0" || v == "off" || v == "false" || v == "no");
    } else if (line.rfind("hotkey=", 0) == 0) {
      auto v = trim(line.substr(7));
      HotkeySpec spec;
      if (parse_hotkey(v, &spec))
        out.hotkey = format_hotkey(spec);
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
  HotkeySpec spec;
  out << "hotkey="
      << (parse_hotkey(s.hotkey, &spec) ? format_hotkey(spec) : s.hotkey)
      << "\n";
  return true;
}

} // namespace keyboop
