#pragma once
#include "xkb_pair.hpp"

#include <string>
#include <vector>

namespace keyboop {

struct InputSource {
  enum class Kind { Xkb, IBusKeyboop, Other };
  Kind kind = Kind::Other;
  std::string id; // layout id ("us") or engine name ("keyboop:us") or other
};

/// Parse GNOME gsettings value like [('xkb', 'us'), ('xkb', 'ru')].
std::vector<InputSource> parse_gnome_sources_value(std::string_view raw);

/// Read org.gnome.desktop.input-sources sources via gsettings CLI
/// (ponytail: no Gio dep in core path; fine for ctl + engine startup).
std::vector<InputSource> read_gnome_input_sources();

bool write_gnome_input_sources(const std::vector<InputSource> &sources);

/// Layout ids for xkb / keyboop engines only.
std::vector<XkbLayoutId> layout_ids_from_sources(const std::vector<InputSource> &sources);

/// keyboop:us → us; xkb us → us
XkbLayoutId layout_id_from_source(const InputSource &src);

std::string keyboop_engine_name(const XkbLayoutId &id);
bool is_keyboop_engine(std::string_view engine_name);
XkbLayoutId layout_from_keyboop_engine(std::string_view engine_name);

/// Migrate xkb → ibus keyboop engines (backup path optional).
bool gnome_enable_keyboop(std::string *err = nullptr);
bool gnome_disable_keyboop(std::string *err = nullptr);

std::string gnome_sources_backup_path();

/// Copy + enable keyboop-switch@keyboop (needs one GNOME session restart).
bool install_keyboop_switch_extension(std::string *err = nullptr);

struct GnomeSwitchStatus {
  bool ok = false;
  std::string detail;
};

/// D-Bus Status on the Shell extension (session capability, not distro).
GnomeSwitchStatus gnome_switch_extension_status();

std::vector<std::string> ibus_component_candidate_paths();
std::vector<std::string> gnome_extension_candidate_paths();

} // namespace keyboop
