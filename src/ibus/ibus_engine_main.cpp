/*
 * Keyboop IBus engine — one engine per layout (keyboop:us, keyboop:ru, …).
 * Keys pass through (no IM preedit/underline). Firefox/Qt ignore Latin
 * preedit or apply the first commit one event late.
 * Auto-convert on Space/Enter/Tab replaces the tracked word only when
 * surrounding text matches, else N× Backspace (keycode 0) — the app already
 * has those characters. Terminal: auto off, same tracking, Ctrl+Alt+K only
 * if surrounding matches. Never commit-only on top of already-typed text.
 * Modes: --xml | --ibus
 */
#include "active_keymap.hpp"
#include "engine.hpp"
#include "gnome_sources.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"
#include "user_settings.hpp"
#include "utf8.hpp"
#include "xkb_pair.hpp"

#include <ibus.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef KEYBOOP_LIBEXECDIR
#error "KEYBOOP_LIBEXECDIR must be set by CMake"
#endif

namespace {

IBusBus *g_bus = nullptr;
std::string g_sources_fp;
time_t g_settings_mtime = -1;
keyboop::UserSettings g_user_settings;

void refresh_user_settings(keyboop::Engine &eng) {
  auto path = keyboop::user_config_path();
  struct stat st {};
  time_t mtime = 0;
  if (!path.empty() && stat(path.c_str(), &st) == 0)
    mtime = st.st_mtime;
  // Cache the parsed file, but always copy onto this engine: IBus has one
  // Engine per layout (keyboop:us, keyboop:ru, …) and a process-wide mtime
  // skip used to leave the others at auto_enabled=true.
  if (mtime != g_settings_mtime || g_settings_mtime == -1) {
    g_settings_mtime = mtime;
    g_user_settings = keyboop::load_user_settings();
  }
  eng.settings().auto_enabled = g_user_settings.auto_enabled;
  eng.settings().manual_hotkeys = {g_user_settings.hotkey};
}

void ensure_data_loaded() {
  static bool once = false;
  if (once)
    return;
  once = true;
  keyboop::LayoutData::shared().load_from_search_path();
}

std::string sources_fingerprint(
    const std::vector<keyboop::InputSource> &sources) {
  std::string fp;
  fp.reserve(sources.size() * 12);
  for (const auto &s : sources) {
    fp.push_back(static_cast<char>('0' + static_cast<int>(s.kind)));
    fp += s.id;
    fp.push_back(';');
  }
  return fp;
}

void refresh_active_keymap(const keyboop::XkbLayoutId *active) {
  auto sources = keyboop::read_gnome_input_sources();
  auto fp = sources_fingerprint(sources);
  if (fp == g_sources_fp && keyboop::ActiveKeymap::shared().ready())
    return;
  g_sources_fp = std::move(fp);

  auto layouts = keyboop::layout_ids_from_sources(sources);
  if (layouts.empty()) {
    layouts.push_back(keyboop::XkbLayoutId::parse("us"));
    layouts.push_back(keyboop::XkbLayoutId::parse("ru"));
  }
  auto pair = keyboop::pick_latin_cyrillic_pair(layouts, active);
  if (pair.ok)
    keyboop::ActiveKeymap::shared().use_pair(std::move(pair));
  else
    keyboop::ActiveKeymap::shared().use_builtin_us_ru();
}

typedef struct _KeyboopEngine {
  IBusEngine parent;
  keyboop::Engine *core;
  keyboop::XkbLayoutId *layout;
  gchar *engine_name;
  gboolean muted;
  gboolean preedit_shown;
  guint purpose;
  guint caps;
} KeyboopEngine;

typedef struct _KeyboopEngineClass {
  IBusEngineClass parent;
} KeyboopEngineClass;

void commit_utf8(IBusEngine *engine, const std::string &ch) {
  if (ch.empty())
    return;
  IBusText *t = ibus_text_new_from_string(ch.c_str());
  ibus_engine_commit_text(engine, t);
}

void hide_preedit(IBusEngine *engine, KeyboopEngine *self) {
  ibus_engine_hide_preedit_text(engine);
  if (self)
    self->preedit_shown = FALSE;
}

void drop_composition(IBusEngine *engine, KeyboopEngine *self) {
  // Pass-through typing: characters are already in the app. Never commit
  // the buffer here — that stacked text (ghbdtnпривет) in Telegram/Firefox.
  hide_preedit(engine, self);
  if (self->core)
    self->core->clear_context();
}

std::string utf8_slice_chars(const gchar *txt, guint from, guint to) {
  if (!txt || to <= from)
    return {};
  if (!g_utf8_validate(txt, -1, nullptr))
    return {};
  const guint len = static_cast<guint>(g_utf8_strlen(txt, -1));
  if (from >= len || to > len)
    return {};
  const gchar *a = g_utf8_offset_to_pointer(txt, static_cast<glong>(from));
  const gchar *b = g_utf8_offset_to_pointer(txt, static_cast<glong>(to));
  if (b < a)
    return {};
  return std::string(a, b);
}

std::string keymap_flip(const std::string &phrase) {
  if (phrase.empty())
    return {};
  const bool source_cyr = keyboop::has_cyrillic(phrase);
  const bool source_lat = keyboop::has_latin_letter(phrase);
  if (!source_cyr && !source_lat)
    return {};
  const bool to_cyr = source_lat && !source_cyr;
  std::string converted = keyboop::Keymap::convert(phrase, to_cyr);
  if (converted == phrase)
    return {};
  return converted;
}

std::string run_selection_cmd(const char *cmd) {
  gchar *out = nullptr;
  gint status = 0;
  GError *err = nullptr;
  if (!g_spawn_command_line_sync(cmd, &out, nullptr, &status, &err) ||
      status != 0) {
    g_clear_error(&err);
    g_free(out);
    return {};
  }
  std::string s = out ? out : "";
  g_free(out);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

std::string read_primary_selection() {
  if (g_getenv("WAYLAND_DISPLAY")) {
    std::string s = run_selection_cmd("wl-paste -n -p");
    if (!s.empty())
      return s;
  }
  if (g_getenv("DISPLAY"))
    return run_selection_cmd("xclip -o -selection primary");
  return {};
}

void forward_backspace(IBusEngine *engine) {
  ibus_engine_forward_key_event(engine, IBUS_KEY_BackSpace, 0, 0);
  ibus_engine_forward_key_event(engine, IBUS_KEY_BackSpace, 0,
                                IBUS_RELEASE_MASK);
}

void forward_delete(IBusEngine *engine) {
  ibus_engine_forward_key_event(engine, IBUS_KEY_Delete, 0, 0);
  ibus_engine_forward_key_event(engine, IBUS_KEY_Delete, 0,
                                IBUS_RELEASE_MASK);
}

// ponytail: cap forward_backspace bursts — too many synthetic key events
// in one go can destabilize Mutter/Wayland. 64 was too low: a selected
// sentence (~77) no-op'd while its halves converted. 256 covers a tweet;
// longer highlights fall through to commit.
static constexpr int kMaxForwardBackspace = 256;

bool replace_char_range(IBusEngine *engine, KeyboopEngine *self, guint cursor,
                        guint start, guint end, const std::string &expect,
                        const std::string &insert) {
  if (end <= start || insert.empty() || expect.empty())
    return false;
  const int n = static_cast<int>(end - start);
  if (static_cast<int>(keyboop::utf8_length(expect)) != n)
    return false;
  if (n > kMaxForwardBackspace)
    return false;

  self->muted = TRUE;
  hide_preedit(engine, self);

  if (cursor > start) {
    guint n_before = cursor - start;
    for (guint i = 0; i < n_before; ++i)
      forward_backspace(engine);
  }
  if (end > cursor) {
    guint n_after = end - cursor;
    for (guint i = 0; i < n_after; ++i)
      forward_delete(engine);
  }

  commit_utf8(engine, insert);
  self->muted = FALSE;
  return true;
}

// Surrounding missing (Firefox, some Qt): keys already reached the app.
bool replace_typed_suffix(IBusEngine *engine, KeyboopEngine *self,
                          const std::string &expect,
                          const std::string &insert) {
  if (expect.empty() || insert.empty())
    return false;
  const int n = static_cast<int>(keyboop::utf8_length(expect));
  if (n <= 0 || n > kMaxForwardBackspace)
    return false;
  self->muted = TRUE;
  hide_preedit(engine, self);
  for (int i = 0; i < n; ++i)
    forward_backspace(engine);
  commit_utf8(engine, insert);
  self->muted = FALSE;
  return true;
}

bool apply_expected_replace(IBusEngine *engine, KeyboopEngine *self,
                            const std::string &expected,
                            const std::string &insert) {
  if (expected.empty() || insert.empty() || expected == insert)
    return false;
  IBusText *st = nullptr;
  guint cursor = 0, anchor = 0;
  ibus_engine_get_surrounding_text(engine, &st, &cursor, &anchor);
  const gchar *txt = (st && ibus_text_get_text(st) &&
                      ibus_text_get_text(st)[0] != '\0')
                         ? ibus_text_get_text(st)
                         : nullptr;
  if (txt && !g_utf8_validate(txt, -1, nullptr))
    txt = nullptr;
  if (txt) {
    auto span = keyboop::match_expected_at_caret(txt, cursor, expected);
    if (st)
      g_object_unref(st);
    if (span.ok)
      return replace_char_range(engine, self, cursor,
                                static_cast<guint>(span.start_cp),
                                static_cast<guint>(span.end_cp), expected,
                                insert);
    // Qt/Firefox surrounding often lags one key. Word is already in the
    // widget (pass-through) — suffix Backspace still hits the right chars.
  } else if (st) {
    g_object_unref(st);
  }
  return replace_typed_suffix(engine, self, expected, insert);
}

bool replace_selected_text(IBusEngine *engine, KeyboopEngine *self,
                           const std::string &selected,
                           const std::string &insert) {
  if (selected.empty() || insert.empty())
    return false;
  self->muted = TRUE;
  hide_preedit(engine, self);
  commit_utf8(engine, insert);
  if (self->core)
    self->core->clear_context();
  self->muted = FALSE;
  return true;
}

bool replace_highlighted(IBusEngine *engine, KeyboopEngine *self, guint cursor,
                         bool have_range, guint start, guint end,
                         const std::string &expect, const std::string &insert) {
  if (have_range &&
      replace_char_range(engine, self, cursor, start, end, expect, insert))
    return true;
  // Highlight is still in the app (we have not sent keys). Commit replaces it.
  // Used when the span is longer than kMaxForwardBackspace or surrounding
  // cannot place PRIMARY (select-all / truncated IBus text).
  return replace_selected_text(engine, self, expect, insert);
}

void maybe_switch_layout(bool to_cyrillic) {
  auto &km = keyboop::ActiveKeymap::shared();
  const auto &id = to_cyrillic ? km.cyrillic_layout() : km.latin_layout();
  if (id.empty())
    return;
  std::string name = keyboop::keyboop_engine_name(id);
  if (g_bus && ibus_bus_is_connected(g_bus)) {
    IBusEngineDesc *cur = ibus_bus_get_global_engine(g_bus);
    if (cur) {
      const gchar *cur_name = ibus_engine_desc_get_name(cur);
      const bool same = cur_name && name == cur_name;
      g_object_unref(cur);
      if (same)
        return;
    }
  }
  gchar *hold = g_strdup(name.c_str());
  g_timeout_add(
      80,
      [](gpointer data) -> gboolean {
        gchar *engine = static_cast<gchar *>(data);
        gchar *argv[] = {
            const_cast<gchar *>("gdbus"),
            const_cast<gchar *>("call"),
            const_cast<gchar *>("--session"),
            const_cast<gchar *>("--dest"),
            const_cast<gchar *>("org.gnome.Shell"),
            const_cast<gchar *>("--object-path"),
            const_cast<gchar *>("/org/gnome/Shell/Extensions/KeyboopSwitch"),
            const_cast<gchar *>("--method"),
            const_cast<gchar *>(
                "org.gnome.Shell.Extensions.KeyboopSwitch.Activate"),
            engine,
            nullptr};
        GPid pid = 0;
        GError *spawn_err = nullptr;
        if (g_spawn_async(nullptr, argv, nullptr,
                          static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                                   G_SPAWN_DO_NOT_REAP_CHILD |
                                                   G_SPAWN_STDOUT_TO_DEV_NULL |
                                                   G_SPAWN_STDERR_TO_DEV_NULL),
                          nullptr, nullptr, &pid, &spawn_err)) {
          g_child_watch_add(
              pid,
              [](GPid p, gint status, gpointer eng) {
                g_spawn_close_pid(p);
                const bool ok = g_spawn_check_wait_status(status, nullptr);
                if (!ok && g_bus && ibus_bus_is_connected(g_bus)) {
                  ibus_bus_set_global_engine_async(g_bus,
                                                   static_cast<gchar *>(eng),
                                                   -1, nullptr, nullptr,
                                                   nullptr);
                }
                g_free(eng);
              },
              engine);
          return G_SOURCE_REMOVE;
        }
        if (spawn_err)
          g_error_free(spawn_err);
        if (g_bus && ibus_bus_is_connected(g_bus)) {
          ibus_bus_set_global_engine_async(g_bus, engine, -1, nullptr, nullptr,
                                           nullptr);
        }
        g_free(engine);
        return G_SOURCE_REMOVE;
      },
      hold);
}

bool flip_to_cyrillic(const std::string &phrase) {
  return keyboop::has_latin_letter(phrase) && !keyboop::has_cyrillic(phrase);
}

G_DEFINE_TYPE(KeyboopEngine, keyboop_engine, IBUS_TYPE_ENGINE)

enum {
  PROP_0,
  PROP_ENGINE_NAME,
};

static void keyboop_engine_set_property(GObject *object, guint prop_id,
                                        const GValue *value, GParamSpec *pspec) {
  auto *self = reinterpret_cast<KeyboopEngine *>(object);
  switch (prop_id) {
  case PROP_ENGINE_NAME:
    g_free(self->engine_name);
    self->engine_name = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void keyboop_engine_get_property(GObject *object, guint prop_id,
                                        GValue *value, GParamSpec *pspec) {
  auto *self = reinterpret_cast<KeyboopEngine *>(object);
  switch (prop_id) {
  case PROP_ENGINE_NAME:
    g_value_set_string(value, self->engine_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static GObject *keyboop_engine_constructor(GType type, guint n_params,
                                           GObjectConstructParam *params) {
  for (guint i = 0; i < n_params; ++i) {
    if (g_strcmp0(params[i].pspec->name, "active-surrounding-text") == 0) {
      g_value_set_boolean(params[i].value, TRUE);
      break;
    }
  }
  return G_OBJECT_CLASS(keyboop_engine_parent_class)
      ->constructor(type, n_params, params);
}

static void keyboop_engine_init(KeyboopEngine *self) {
  self->core = new keyboop::Engine();
  self->layout = new keyboop::XkbLayoutId();
  self->engine_name = nullptr;
  self->muted = FALSE;
  self->preedit_shown = FALSE;
  self->purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
  self->caps = 0;
  refresh_user_settings(*self->core);
}

static void keyboop_engine_constructed(GObject *object) {
  if (G_OBJECT_CLASS(keyboop_engine_parent_class)->constructed)
    G_OBJECT_CLASS(keyboop_engine_parent_class)->constructed(object);
  auto *self = reinterpret_cast<KeyboopEngine *>(object);
  if (self->engine_name)
    *self->layout = keyboop::layout_from_keyboop_engine(self->engine_name);
  if (self->layout->empty())
    *self->layout = keyboop::XkbLayoutId::parse("us");
  refresh_active_keymap(self->layout);
}

static void keyboop_engine_dispose(GObject *object) {
  auto *self = reinterpret_cast<KeyboopEngine *>(object);
  g_clear_pointer(&self->engine_name, g_free);
  G_OBJECT_CLASS(keyboop_engine_parent_class)->dispose(object);
}

static void keyboop_engine_finalize(GObject *object) {
  auto *self = reinterpret_cast<KeyboopEngine *>(object);
  delete self->core;
  self->core = nullptr;
  delete self->layout;
  self->layout = nullptr;
  G_OBJECT_CLASS(keyboop_engine_parent_class)->finalize(object);
}

static bool is_password(const KeyboopEngine *self) {
  return self->purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
         self->purpose == IBUS_INPUT_PURPOSE_PIN;
}

static bool skip_auto(const KeyboopEngine *self) {
  return is_password(self) || self->purpose == IBUS_INPUT_PURPOSE_TERMINAL;
}

static bool is_manual_hotkey(guint keyval, guint keycode, guint modifiers) {
  (void)keycode;
  keyboop::HotkeySpec spec;
  if (!keyboop::parse_hotkey(g_user_settings.hotkey, &spec) || !spec.ok())
    keyboop::parse_hotkey("Control+Alt+k", &spec);
  const guint mods =
      modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_META_MASK |
                   IBUS_SHIFT_MASK | IBUS_SUPER_MASK | IBUS_HYPER_MASK |
                   IBUS_MOD4_MASK);
  const bool ctrl = (mods & IBUS_CONTROL_MASK) != 0;
  const bool alt =
      (mods & IBUS_MOD1_MASK) != 0 || (mods & IBUS_META_MASK) != 0;
  const bool shift = (mods & IBUS_SHIFT_MASK) != 0;
  const bool super =
      (mods & IBUS_SUPER_MASK) != 0 || (mods & IBUS_MOD4_MASK) != 0 ||
      (mods & IBUS_HYPER_MASK) != 0;
  if (ctrl != spec.ctrl || alt != spec.alt || shift != spec.shift ||
      super != spec.super)
    return false;

  guint want = ibus_keyval_from_name(spec.key.c_str());
  if (!want && spec.key.size() > 1) {
    std::string titled = spec.key;
    titled[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(titled[0])));
    want = ibus_keyval_from_name(titled.c_str());
  }
  if (want && (keyval == want || keyval == ibus_keyval_to_upper(want) ||
               keyval == ibus_keyval_to_lower(want)))
    return true;

  gunichar uc = ibus_keyval_to_unicode(keyval);
  if (!uc)
    return false;
  char buf[8]{};
  gint n = g_unichar_to_utf8(uc, buf);
  if (n <= 0)
    return false;
  std::string ch(buf, static_cast<size_t>(n));
  auto lat = keyboop::to_lower_utf8(keyboop::Keymap::convert(ch, false));
  return lat == spec.key;
}

static bool is_dead_or_compose(guint keyval) {
  if (keyval == IBUS_KEY_Multi_key)
    return true;
  return keyval >= IBUS_KEY_dead_grave && keyval <= IBUS_KEY_dead_greek;
}

static bool is_modifier_key(guint keyval) {
  switch (keyval) {
  case IBUS_KEY_Shift_L:
  case IBUS_KEY_Shift_R:
  case IBUS_KEY_Control_L:
  case IBUS_KEY_Control_R:
  case IBUS_KEY_Alt_L:
  case IBUS_KEY_Alt_R:
  case IBUS_KEY_Meta_L:
  case IBUS_KEY_Meta_R:
  case IBUS_KEY_Super_L:
  case IBUS_KEY_Super_R:
  case IBUS_KEY_Hyper_L:
  case IBUS_KEY_Hyper_R:
  case IBUS_KEY_Caps_Lock:
  case IBUS_KEY_Num_Lock:
  case IBUS_KEY_Scroll_Lock:
  case IBUS_KEY_ISO_Level3_Shift:
  case IBUS_KEY_ISO_Level5_Shift:
  case IBUS_KEY_Mode_switch:
    return true;
  default:
    return false;
  }
}

static bool is_nav_key(guint keyval) {
  return keyval == IBUS_KEY_Escape || keyval == IBUS_KEY_Left ||
         keyval == IBUS_KEY_Right || keyval == IBUS_KEY_Up ||
         keyval == IBUS_KEY_Down || keyval == IBUS_KEY_Home ||
         keyval == IBUS_KEY_End || keyval == IBUS_KEY_Page_Up ||
         keyval == IBUS_KEY_Page_Down || keyval == IBUS_KEY_Delete;
}

static void handle_manual(IBusEngine *engine, KeyboopEngine *self) {
  if (is_password(self))
    return;
  refresh_active_keymap(self->layout);
  hide_preedit(engine, self);

  IBusText *st = nullptr;
  guint cursor = 0, anchor = 0;
  ibus_engine_get_surrounding_text(engine, &st, &cursor, &anchor);
  const gchar *txt = (st && ibus_text_get_text(st) &&
                      ibus_text_get_text(st)[0] != '\0')
                         ? ibus_text_get_text(st)
                         : nullptr;
  if (txt && !g_utf8_validate(txt, -1, nullptr))
    txt = nullptr;

  const std::string primary = read_primary_selection();
  std::string selected;
  guint sel_lo = 0, sel_hi = 0;
  bool have_range = false;
  const bool ibus_sel = txt && cursor != anchor;

  if (ibus_sel) {
    sel_lo = cursor < anchor ? cursor : anchor;
    sel_hi = cursor < anchor ? anchor : cursor;
    selected = utf8_slice_chars(txt, sel_lo, sel_hi);
    if (!selected.empty() && keyboop::utf8_valid(selected))
      have_range = true;
    else
      selected.clear();
  }
  if (!have_range && txt && !primary.empty() && keyboop::utf8_valid(primary)) {
    auto match = keyboop::match_primary_snapshot(txt, cursor, primary);
    if (match.ok) {
      selected = primary;
      sel_lo = static_cast<guint>(match.start_cp);
      sel_hi = static_cast<guint>(match.end_cp);
      have_range = true;
    }
  }
  // Select-all: surrounding (or the IBus slice) is only a prefix of PRIMARY.
  // Backspacing the visible part would leave the rest of the highlight;
  // commit over the app selection instead. Never layout_flip_at the first word.
  if (!primary.empty() && keyboop::utf8_valid(primary)) {
    const std::string visible = selected.empty() && txt ? std::string(txt)
                                                        : selected;
    const bool truncated = !visible.empty() &&
                           keyboop::primary_extends_truncated_selection(
                               visible, cursor, primary);
    if (truncated || (ibus_sel && selected.empty())) {
      selected = primary;
      have_range = false;
    }
  }

  if (!selected.empty()) {
    std::string converted = keymap_flip(selected);
    const bool to_cyr = flip_to_cyrillic(selected);
    if (st)
      g_object_unref(st);
    if (!converted.empty() &&
        replace_highlighted(engine, self, cursor, have_range, sel_lo, sel_hi,
                            selected, converted))
      maybe_switch_layout(to_cyr);
    return;
  }

  // If we have a buffered word in keystroke history (typing without surrounding text, e.g. Chrome):
  if (self->core) {
    if (auto act = self->core->manual_convert()) {
      if (st)
        g_object_unref(st);
      if (apply_expected_replace(engine, self, act->expected, act->replacement)) {
        maybe_switch_layout(act->switch_to_cyrillic);
        self->core->clear_context();
      }
      return;
    }
  }

  if (txt) {
    auto run = keyboop::layout_flip_at(txt, cursor);
    if (!run.text.empty() && keyboop::utf8_valid(run.text)) {
      std::string converted = keymap_flip(run.text);
      if (!converted.empty()) {
        const bool to_cyr = flip_to_cyrillic(run.text);
        replace_char_range(engine, self, cursor,
                           static_cast<guint>(run.start_cp),
                           static_cast<guint>(run.end_cp), run.text,
                           converted);
        maybe_switch_layout(to_cyr);
        if (self->core)
          self->core->clear_context();
      }
    }
  }
  if (st)
    g_object_unref(st);
}

static gboolean commit_boundary(IBusEngine *engine, KeyboopEngine *self,
                                std::string_view ws) {
  const std::string word = self->core->buffer().current_word();
  if (word.empty())
    return FALSE;
  hide_preedit(engine, self);
  if (auto act = self->core->on_boundary(ws)) {
    const std::string &expect =
        act->expected == word ? act->expected : word;
    if (apply_expected_replace(engine, self, expect, act->replacement))
      maybe_switch_layout(act->switch_to_cyrillic);
  }
  return FALSE; // let Space/Enter/Tab through; word is already on screen
}

static gboolean keyboop_process_key_event(IBusEngine *engine, guint keyval,
                                          guint keycode, guint modifiers) {
  if (keyval == IBUS_KEY_VoidSymbol)
    return FALSE;

  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  if (self->muted)
    return FALSE;

  ensure_data_loaded();
  refresh_user_settings(*self->core);
  if (modifiers & IBUS_RELEASE_MASK)
    return FALSE;

  if (is_manual_hotkey(keyval, keycode, modifiers)) {
    handle_manual(engine, self);
    return TRUE;
  }

  if (modifiers & (IBUS_SUPER_MASK | IBUS_MOD4_MASK | IBUS_HYPER_MASK))
    return FALSE;

  if (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_META_MASK)) {
    if (!(modifiers & IBUS_SHIFT_MASK))
      drop_composition(engine, self);
    return FALSE;
  }

  if (is_password(self) || !self->core->settings().enabled) {
    drop_composition(engine, self);
    return FALSE;
  }

  if (keyval == IBUS_KEY_BackSpace) {
    if (!self->core->buffer().current_word().empty())
      self->core->on_backspace();
    return FALSE;
  }

  if (is_nav_key(keyval)) {
    if (!(modifiers & IBUS_SHIFT_MASK))
      drop_composition(engine, self);
    return FALSE;
  }

  auto &cfg = self->core->settings();
  const bool auto_ok = !skip_auto(self);

  if (auto_ok && keyval == IBUS_KEY_space && cfg.on_space)
    return commit_boundary(engine, self, " ");
  if (auto_ok && (keyval == IBUS_KEY_Return || keyval == IBUS_KEY_KP_Enter) &&
      cfg.on_enter)
    return commit_boundary(engine, self, "\n");
  if (auto_ok && keyval == IBUS_KEY_Tab && cfg.on_tab)
    return commit_boundary(engine, self, "\t");

  if (is_dead_or_compose(keyval)) {
    drop_composition(engine, self);
    return FALSE;
  }

  gunichar uc = ibus_keyval_to_unicode(keyval);
  if (uc == 0 || g_unichar_iscntrl(uc)) {
    if (!is_modifier_key(keyval))
      drop_composition(engine, self);
    return FALSE;
  }

  char buf[8]{};
  gint len = g_unichar_to_utf8(uc, buf);
  if (len <= 0)
    return FALSE;
  self->core->on_text(std::string(buf, static_cast<size_t>(len)));
  return FALSE;
}

static void keyboop_focus_in(IBusEngine *engine) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  refresh_active_keymap(self->layout);
  ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
}

static void keyboop_enable(IBusEngine *engine) {
  ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);
  if (IBUS_ENGINE_CLASS(keyboop_engine_parent_class)->enable)
    IBUS_ENGINE_CLASS(keyboop_engine_parent_class)->enable(engine);
}

static void keyboop_focus_out(IBusEngine *engine) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  drop_composition(engine, self);
}

static void keyboop_reset(IBusEngine *engine) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  drop_composition(engine, self);
}

static void keyboop_set_content_type(IBusEngine *engine, guint purpose,
                                     guint /*hints*/) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  if (purpose != self->purpose &&
      (purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
       purpose == IBUS_INPUT_PURPOSE_PIN))
    drop_composition(engine, self);
  self->purpose = purpose;
}

static void keyboop_set_capabilities(IBusEngine *engine, guint caps) {
  reinterpret_cast<KeyboopEngine *>(engine)->caps = caps;
}

static void keyboop_engine_class_init(KeyboopEngineClass *klass) {
  auto *object_class = G_OBJECT_CLASS(klass);
  auto *engine_class = IBUS_ENGINE_CLASS(klass);
  object_class->constructor = keyboop_engine_constructor;
  object_class->constructed = keyboop_engine_constructed;
  object_class->dispose = keyboop_engine_dispose;
  object_class->finalize = keyboop_engine_finalize;
  object_class->set_property = keyboop_engine_set_property;
  object_class->get_property = keyboop_engine_get_property;
  engine_class->process_key_event = keyboop_process_key_event;
  engine_class->enable = keyboop_enable;
  engine_class->focus_in = keyboop_focus_in;
  engine_class->focus_out = keyboop_focus_out;
  engine_class->reset = keyboop_reset;
  engine_class->set_content_type = keyboop_set_content_type;
  engine_class->set_capabilities = keyboop_set_capabilities;

  g_object_class_install_property(
      object_class, PROP_ENGINE_NAME,
      g_param_spec_string("engine-name", "Engine Name", "The engine name",
                          nullptr,
                          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                   G_PARAM_CONSTRUCT_ONLY)));
}

std::string engine_language_code(const keyboop::XkbLayoutId &id) {
  if (id.layout == "ru")
    return "rus";
  if (id.layout == "ua")
    return "ukr";
  if (id.layout == "by")
    return "bel";
  if (id.layout == "us" || id.layout == "gb")
    return "eng";
  if (id.layout == "de")
    return "deu";
  if (id.layout == "fr")
    return "fra";
  auto script = keyboop::detect_layout_script(id);
  if (script == keyboop::LayoutScript::Cyrillic)
    return "rus";
  if (script == keyboop::LayoutScript::Latin)
    return "eng";
  return id.layout.empty() ? "eng" : id.layout;
}

std::string engine_symbol(const keyboop::XkbLayoutId &id) {
  if (id.layout == "us" || id.layout == "gb")
    return "en";
  if (id.layout == "ua")
    return "uk";
  if (id.layout == "by")
    return "by";
  if (!id.layout.empty())
    return id.layout;
  return "en";
}

std::string engine_longname(const keyboop::XkbLayoutId &id) {
  if (id.layout == "us" && id.variant.empty())
    return "English (US)";
  if (id.layout == "gb" && id.variant.empty())
    return "English (UK)";
  if (id.layout == "ru" && id.variant.empty())
    return "Russian";
  if (id.layout == "ua" && id.variant.empty())
    return "Ukrainian";
  if (id.layout == "de" && id.variant.empty())
    return "German";
  if (id.variant.empty())
    return id.layout;
  return id.layout + " (" + id.variant + ")";
}

IBusEngineDesc *make_desc(const keyboop::XkbLayoutId &id) {
  auto name = keyboop::keyboop_engine_name(id);
  std::string longname = engine_longname(id);
  std::string lang = engine_language_code(id);
  std::string symbol = engine_symbol(id);
  if (id.variant.empty()) {
    return ibus_engine_desc_new_varargs(
        "name", name.c_str(), "longname", longname.c_str(), "description",
        "Layout auto-switch (Keyboop)", "language", lang.c_str(), "license",
        "MIT", "author", "Keyboop", "icon", "ibus-keyboard", "layout",
        id.layout.c_str(), "symbol", symbol.c_str(), "rank", 99, nullptr);
  }
  return ibus_engine_desc_new_varargs(
      "name", name.c_str(), "longname", longname.c_str(), "description",
      "Layout auto-switch (Keyboop)", "language", lang.c_str(), "license",
      "MIT", "author", "Keyboop", "icon", "ibus-keyboard", "layout",
      id.layout.c_str(), "layout-variant", id.variant.c_str(), "symbol",
      symbol.c_str(), "rank", 99, nullptr);
}

std::vector<keyboop::XkbLayoutId> engines_to_advertise() {
  auto layouts =
      keyboop::layout_ids_from_sources(keyboop::read_gnome_input_sources());
  if (layouts.empty()) {
    layouts.push_back(keyboop::XkbLayoutId::parse("us"));
    layouts.push_back(keyboop::XkbLayoutId::parse("ru"));
  }
  return layouts;
}

void print_engines_xml() {
  g_print("<engines>\n");
  for (const auto &id : engines_to_advertise()) {
    auto *desc = make_desc(id);
    GString *out = g_string_new(nullptr);
    ibus_engine_desc_output(desc, out, 1);
    g_print("%s", out->str);
    g_string_free(out, TRUE);
    g_object_unref(desc);
  }
  g_print("</engines>\n");
}

void run_ibus() {
  ibus_init();
  g_bus = ibus_bus_new();
  if (!ibus_bus_is_connected(g_bus)) {
    g_warning("not connected to ibus-daemon");
    return;
  }

  ensure_data_loaded();
  refresh_active_keymap(nullptr);

  std::string exec_path =
      std::string(KEYBOOP_LIBEXECDIR) + "/ibus-engine-keyboop --ibus";
  auto *component = ibus_component_new(
      "org.freedesktop.IBus.Keyboop", "Keyboop layout auto-switch", "0.1.0",
      "MIT", "Keyboop", "https://github.com/grmllnn/keyboob", exec_path.c_str(),
      "keyboop");

  auto layouts = engines_to_advertise();
  for (const auto &id : layouts)
    ibus_component_add_engine(component, make_desc(id));

  auto *factory = ibus_factory_new(ibus_bus_get_connection(g_bus));
  g_object_ref_sink(factory);
  for (const auto &id : layouts) {
    auto name = keyboop::keyboop_engine_name(id);
    ibus_factory_add_engine(factory, name.c_str(), keyboop_engine_get_type());
  }

  ibus_bus_register_component(g_bus, component);
  {
    guint32 name_flags = static_cast<guint32>(
        IBUS_BUS_NAME_FLAG_REPLACE_EXISTING | IBUS_BUS_NAME_FLAG_DO_NOT_QUEUE);
    ibus_bus_request_name(g_bus, "org.freedesktop.IBus.Keyboop", name_flags);
  }

  auto *loop = g_main_loop_new(nullptr, FALSE);
  g_main_loop_run(loop);
}

} // namespace

int main(int argc, char **argv) {
  bool xml = false;
  bool ibus_mode = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--xml") == 0)
      xml = true;
    if (std::strcmp(argv[i], "--ibus") == 0)
      ibus_mode = true;
  }
  if (xml) {
    ibus_init();
    print_engines_xml();
    return 0;
  }
  if (ibus_mode || argc == 1) {
    run_ibus();
    return 0;
  }
  g_printerr("usage: ibus-engine-keyboop [--xml|--ibus]\n");
  return 1;
}
