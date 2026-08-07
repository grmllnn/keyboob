#include "gnome_sources.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace keyboop {
namespace {

namespace fs = std::filesystem;

std::string shell_quote_gsettings_sources(const std::vector<InputSource> &sources) {
  // Format: [('xkb', 'us'), ('ibus', 'keyboop:ru')]
  std::ostringstream oss;
  oss << "[";
  bool first = true;
  for (const auto &s : sources) {
    if (!first)
      oss << ", ";
    first = false;
    const char *kind = "ibus";
    std::string id = s.id;
    if (s.kind == InputSource::Kind::Xkb) {
      kind = "xkb";
    } else if (s.kind == InputSource::Kind::IBusKeyboop) {
      kind = "ibus";
      if (id.rfind("keyboop:", 0) != 0)
        id = keyboop_engine_name(XkbLayoutId::parse(id));
    } else {
      kind = "ibus";
    }
    oss << "('" << kind << "', '" << id << "')";
  }
  oss << "]";
  return oss.str();
}

std::string run_cmd_capture(const std::string &cmd, int *rc = nullptr) {
  FILE *fp = popen(cmd.c_str(), "r");
  if (!fp) {
    if (rc)
      *rc = -1;
    return {};
  }
  std::string out;
  char buf[512];
  while (fgets(buf, sizeof(buf), fp))
    out += buf;
  int st = pclose(fp);
  if (rc)
    *rc = st;
  return out;
}

} // namespace

std::string keyboop_engine_name(const XkbLayoutId &id) {
  return "keyboop:" + id.id();
}

bool is_keyboop_engine(std::string_view engine_name) {
  return engine_name.rfind("keyboop:", 0) == 0;
}

XkbLayoutId layout_from_keyboop_engine(std::string_view engine_name) {
  if (!is_keyboop_engine(engine_name))
    return {};
  return XkbLayoutId::parse(engine_name.substr(8));
}

XkbLayoutId layout_id_from_source(const InputSource &src) {
  if (src.kind == InputSource::Kind::Xkb)
    return XkbLayoutId::parse(src.id);
  if (src.kind == InputSource::Kind::IBusKeyboop)
    return layout_from_keyboop_engine(src.id.rfind("keyboop:", 0) == 0
                                          ? src.id
                                          : keyboop_engine_name(XkbLayoutId::parse(src.id)));
  if (is_keyboop_engine(src.id))
    return layout_from_keyboop_engine(src.id);
  return {};
}

std::vector<XkbLayoutId>
layout_ids_from_sources(const std::vector<InputSource> &sources) {
  std::vector<XkbLayoutId> out;
  for (const auto &s : sources) {
    auto id = layout_id_from_source(s);
    if (!id.empty())
      out.push_back(std::move(id));
  }
  return out;
}

std::vector<InputSource> parse_gnome_sources_value(std::string_view raw) {
  // Minimal parser for: [('xkb', 'us'), ('xkb', 'ru+phonetic'), ('ibus', 'keyboop:us')]
  std::vector<InputSource> out;
  std::string s(raw);
  size_t i = 0;
  auto skip = [&] {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t'))
      ++i;
  };
  skip();
  if (i < s.size() && s[i] == '@') { // @as [] empty
    return out;
  }
  if (i >= s.size() || s[i] != '[')
    return out;
  ++i;
  while (i < s.size()) {
    skip();
    if (i < s.size() && s[i] == ']')
      break;
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (i >= s.size() || s[i] != '(')
      break;
    ++i;
    skip();
    if (i >= s.size() || s[i] != '\'')
      break;
    ++i;
    size_t k0 = i;
    while (i < s.size() && s[i] != '\'')
      ++i;
    std::string kind = s.substr(k0, i - k0);
    if (i < s.size())
      ++i;
    skip();
    if (i < s.size() && s[i] == ',')
      ++i;
    skip();
    if (i >= s.size() || s[i] != '\'')
      break;
    ++i;
    size_t v0 = i;
    while (i < s.size() && s[i] != '\'')
      ++i;
    std::string val = s.substr(v0, i - v0);
    if (i < s.size())
      ++i;
    skip();
    if (i < s.size() && s[i] == ')')
      ++i;

    InputSource src;
    src.id = val;
    if (kind == "xkb")
      src.kind = InputSource::Kind::Xkb;
    else if (kind == "ibus" && is_keyboop_engine(val))
      src.kind = InputSource::Kind::IBusKeyboop;
    else
      src.kind = InputSource::Kind::Other;
    out.push_back(std::move(src));
  }
  return out;
}

std::vector<InputSource> read_gnome_input_sources() {
  int rc = 0;
  auto raw = run_cmd_capture(
      "gsettings get org.gnome.desktop.input-sources sources 2>/dev/null", &rc);
  return parse_gnome_sources_value(raw);
}

bool run_argv_wait(const char *const *argv) {
  pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    execvp(argv[0], const_cast<char *const *>(argv));
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return false;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool write_gnome_input_sources(const std::vector<InputSource> &sources) {
  auto val = shell_quote_gsettings_sources(sources);
  const char *argv[] = {"gsettings",
                        "set",
                        "org.gnome.desktop.input-sources",
                        "sources",
                        val.c_str(),
                        nullptr};
  return run_argv_wait(argv);
}

bool activate_gnome_ibus_engine(const std::string &engine_name) {
  // Safe charset only — passed as exec argv (no shell).
  if (engine_name.empty() || engine_name.size() >= 64 ||
      engine_name.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_-") !=
          std::string::npos)
    return false;
  // Must copy before fork: caller often passes a temporary std::string; after
  // parent returns that storage is gone and grandchild would UAF → ibus gets
  // garbage and SetGlobalEngine fails silently.
  char name[64];
  std::snprintf(name, sizeof(name), "%s", engine_name.c_str());
  // Double-fork + delay: `ibus engine` tears down THIS engine process.
  // Waiting on that switch from inside the engine deadlocks input (freeze).
  pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    pid_t grand = fork();
    if (grand < 0)
      _exit(127);
    if (grand > 0)
      _exit(0); // intermediate exits; parent waitpid returns immediately
    setsid();
    usleep(100000); // 100ms — let commit/idle finish before teardown
    const char *argv[] = {"ibus", "engine", name, nullptr};
    execvp("ibus", const_cast<char *const *>(argv));
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return false;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string gnome_sources_backup_path() {
  const char *home = std::getenv("HOME");
  if (!home)
    return {};
  return std::string(home) + "/.config/keyboop/sources.backup";
}

bool component_xml_looks_sane() {
  // ponytail: --xml dumps only <engines>; pasting that over the component
  // file removes <component>/<exec> and GNOME ends up with zero layouts.
  const char *paths[] = {"/usr/share/ibus/component/keyboop.xml",
                         "/usr/local/share/ibus/component/keyboop.xml"};
  for (const char *p : paths) {
    std::ifstream in(p);
    if (!in)
      continue;
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    if (raw.find("<component") != std::string::npos &&
        raw.find("ibus-engine-keyboop") != std::string::npos)
      return true;
  }
  return false;
}

bool install_keyboop_switch_extension(std::string *err) {
  const char *home = std::getenv("HOME");
  if (!home) {
    if (err)
      *err = "HOME unset";
    return false;
  }
  fs::path dst = fs::path(home) /
                 ".local/share/gnome-shell/extensions/keyboop-switch@keyboop";
  std::vector<fs::path> candidates;
  if (const char *env = std::getenv("KEYBOOP_EXTENSION_DIR"))
    candidates.emplace_back(env);
#ifdef KEYBOOP_EXTENSION_DIR
  candidates.emplace_back(KEYBOOP_EXTENSION_DIR);
#endif
  candidates.emplace_back(
      "/usr/share/keyboop/gnome-extension/keyboop-switch@keyboop");
  candidates.emplace_back(
      "/usr/local/share/keyboop/gnome-extension/keyboop-switch@keyboop");
  // Dev tree next to cwd / repo
  candidates.emplace_back(
      "gnome-extension/keyboop-switch@keyboop");

  fs::path src;
  for (const auto &c : candidates) {
    if (fs::exists(c / "metadata.json") && fs::exists(c / "extension.js")) {
      src = c;
      break;
    }
  }
  if (src.empty()) {
    if (err)
      *err = "keyboop-switch extension files not found "
             "(set KEYBOOP_EXTENSION_DIR or install package data)";
    return false;
  }
  std::error_code ec;
  fs::create_directories(dst, ec);
  fs::copy_file(src / "metadata.json", dst / "metadata.json",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (err)
      *err = "copy metadata.json failed: " + ec.message();
    return false;
  }
  fs::copy_file(src / "extension.js", dst / "extension.js",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (err)
      *err = "copy extension.js failed: " + ec.message();
    return false;
  }
  // Prefer enabled-extensions; gnome-extensions enable needs Shell to know
  // the UUID (after next login). Best-effort now.
  run_cmd_capture(
      "gsettings get org.gnome.shell enabled-extensions 2>/dev/null", nullptr);
  {
    int rc = 0;
    auto cur = run_cmd_capture(
        "gsettings get org.gnome.shell enabled-extensions 2>/dev/null", &rc);
    const char *uuid = "keyboop-switch@keyboop";
    if (cur.find(uuid) == std::string::npos) {
      std::string next;
      if (cur.find("@as []") != std::string::npos || cur == "[]\n" ||
          cur == "[]") {
        next = std::string("['") + uuid + "']";
      } else {
        // trim trailing ]
        auto pos = cur.find_last_of(']');
        if (pos == std::string::npos)
          next = std::string("['") + uuid + "']";
        else {
          next = cur.substr(0, pos);
          while (!next.empty() &&
                 (next.back() == '\n' || next.back() == ' '))
            next.pop_back();
          next += std::string(", '") + uuid + "']";
        }
      }
      const char *argv[] = {"gsettings",
                            "set",
                            "org.gnome.shell",
                            "enabled-extensions",
                            next.c_str(),
                            nullptr};
      run_argv_wait(argv);
    }
  }
  run_cmd_capture(
      "gnome-extensions enable keyboop-switch@keyboop 2>/dev/null", nullptr);
  return true;
}

bool gnome_enable_keyboop(std::string *err) {
  if (!component_xml_looks_sane()) {
    if (err)
      *err = "broken/missing /usr/share/ibus/component/keyboop.xml "
             "(must be a full <component>, not --xml engines dump). "
             "Reinstall, then: ibus write-cache && ibus restart";
    return false;
  }
  auto sources = read_gnome_input_sources();
  if (sources.empty()) {
    if (err)
      *err = "no GNOME input sources found";
    return false;
  }
  auto bak = gnome_sources_backup_path();
  if (!bak.empty()) {
    fs::create_directories(fs::path(bak).parent_path());
    // Only backup once if not present — keep original xkb set
    if (!fs::exists(bak)) {
      int rc = 0;
      auto raw = run_cmd_capture(
          "gsettings get org.gnome.desktop.input-sources sources 2>/dev/null",
          &rc);
      std::ofstream(bak) << raw;
    }
  }

  std::vector<InputSource> next;
  next.reserve(sources.size());
  for (const auto &s : sources) {
    if (s.kind == InputSource::Kind::Xkb) {
      InputSource n;
      n.kind = InputSource::Kind::IBusKeyboop;
      n.id = keyboop_engine_name(XkbLayoutId::parse(s.id));
      next.push_back(std::move(n));
    } else if (s.kind == InputSource::Kind::IBusKeyboop) {
      next.push_back(s);
    } else {
      next.push_back(s);
    }
  }
  if (!write_gnome_input_sources(next)) {
    if (err)
      *err = "gsettings set failed";
    return false;
  }
  // Registry cache keeps stale language/symbol until rewritten (en₁/en₂).
  run_cmd_capture("ibus write-cache 2>/dev/null", nullptr);
  install_keyboop_switch_extension(nullptr); // best-effort; needs session restart
  return true;
}

bool gnome_disable_keyboop(std::string *err) {
  auto bak = gnome_sources_backup_path();
  if (!bak.empty() && fs::exists(bak)) {
    std::ifstream in(bak);
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    auto restored = parse_gnome_sources_value(raw);
    if (!restored.empty()) {
      if (!write_gnome_input_sources(restored)) {
        if (err)
          *err = "failed to restore backup via gsettings";
        return false;
      }
      return true;
    }
  }
  // Fallback: convert keyboop:* back to xkb
  auto sources = read_gnome_input_sources();
  std::vector<InputSource> next;
  for (const auto &s : sources) {
    if (s.kind == InputSource::Kind::IBusKeyboop || is_keyboop_engine(s.id)) {
      InputSource n;
      n.kind = InputSource::Kind::Xkb;
      n.id = layout_from_keyboop_engine(s.id).id();
      if (n.id.empty())
        n.id = s.id;
      next.push_back(std::move(n));
    } else {
      next.push_back(s);
    }
  }
  if (!write_gnome_input_sources(next)) {
    if (err)
      *err = "gsettings set failed";
    return false;
  }
  return true;
}

} // namespace keyboop
