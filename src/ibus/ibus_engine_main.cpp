/*
 * Keyboop IBus engine — one engine per layout (keyboop:us, keyboop:ru, …).
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
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef KEYBOOP_LIBEXECDIR
#define KEYBOOP_LIBEXECDIR "/usr/libexec"
#endif

namespace {

IBusBus *g_bus = nullptr;
// Cached GNOME sources fingerprint — avoid gsettings+xkb on every key.
std::string g_sources_fp;
time_t g_settings_mtime = -1;

void refresh_user_settings(keyboop::Engine &eng) {
  auto path = keyboop::user_config_path();
  struct stat st {};
  time_t mtime = 0;
  if (!path.empty() && stat(path.c_str(), &st) == 0)
    mtime = st.st_mtime;
  if (mtime == g_settings_mtime && g_settings_mtime != -1)
    return;
  g_settings_mtime = mtime;
  auto us = keyboop::load_user_settings();
  eng.settings().auto_enabled = us.auto_enabled;
}

void ensure_data_loaded() {
  static bool once = false;
  if (once)
    return;
  once = true;
  auto &data = keyboop::LayoutData::shared();
  const char *env = std::getenv("KEYBOOP_DATA_DIR");
  // Prefer installed data over the compile-time source-tree path.
  if (env && data.load(env))
    return;
  if (data.load("/usr/share/keyboop"))
    return;
  if (data.load("/usr/local/share/keyboop"))
    return;
  data.load(KEYBOOP_DEFAULT_DATA_DIR);
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

// delete_count from Engine may include pending boundary chars not yet on screen.
// Snippet actions do NOT include them — only strip when counts match.
void strip_pending(keyboop::ReplaceAction &act, std::string_view ws,
                   bool keep_ws_in_insert) {
  if (ws.empty() || act.insert.size() < ws.size() || !act.insert.ends_with(ws))
    return;
  const int n = static_cast<int>(keyboop::utf8_length(ws));
  const int insert_wo =
      static_cast<int>(keyboop::utf8_length(act.insert) - n);
  if (act.delete_count == insert_wo + n)
    act.delete_count = insert_wo;
  if (!keep_ws_in_insert)
    act.insert.resize(act.insert.size() - ws.size());
}

// One core across keyboop:us / keyboop:ru so Super+Space mid-word keeps buffer.
keyboop::Engine &shared_core() {
  static keyboop::Engine core;
  return core;
}

typedef struct _KeyboopEngine {
  IBusEngine parent;
  keyboop::Engine *core;
  keyboop::XkbLayoutId *layout;
  gchar *engine_name;
  std::string *committed; // exact text we commit_text'd for current word
  gboolean muted;
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

void committed_clear(KeyboopEngine *self) {
  if (self->committed)
    self->committed->clear();
}

void committed_add(KeyboopEngine *self, const std::string &ch) {
  if (self->committed)
    self->committed->append(ch);
}

void committed_pop(KeyboopEngine *self) {
  if (self->committed && !self->committed->empty())
    keyboop::utf8_pop_back(*self->committed);
}

std::string utf8_suffix(const std::string &s, int nchars) {
  if (nchars <= 0)
    return {};
  int total = static_cast<int>(keyboop::utf8_length(s));
  if (nchars >= total)
    return s;
  const gchar *p = s.c_str();
  const gchar *end = p + s.size();
  const gchar *start =
      g_utf8_offset_to_pointer(p, static_cast<glong>(total - nchars));
  if (start < p || start > end)
    return {};
  return std::string(start, end);
}

void utf8_drop_suffix(std::string &s, int nchars) {
  if (nchars <= 0 || s.empty())
    return;
  int total = static_cast<int>(keyboop::utf8_length(s));
  if (nchars >= total) {
    s.clear();
    return;
  }
  const gchar *p = s.c_str();
  const gchar *cut =
      g_utf8_offset_to_pointer(p, static_cast<glong>(total - nchars));
  s.resize(static_cast<size_t>(cut - p));
}

std::string utf8_slice_chars(const gchar *txt, guint from, guint to) {
  if (!txt || to <= from)
    return {};
  const gchar *a = g_utf8_offset_to_pointer(txt, static_cast<glong>(from));
  const gchar *b = g_utf8_offset_to_pointer(txt, static_cast<glong>(to));
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

std::string read_primary_selection() {
  gchar *out = nullptr;
  gint status = 0;
  GError *err = nullptr;
  if (!g_spawn_command_line_sync("wl-paste -n -p", &out, nullptr, &status,
                                 &err) ||
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

void forward_backspace(IBusEngine *engine) {
  ibus_engine_forward_key_event(engine, IBUS_KEY_BackSpace, 22, 0);
  ibus_engine_forward_key_event(engine, IBUS_KEY_BackSpace, 22,
                                IBUS_RELEASE_MASK);
}

// Drop leftover highlight on the just-committed text (GTK often keeps it).
void collapse_selection(IBusEngine *engine) {
  ibus_engine_forward_key_event(engine, IBUS_KEY_Right, 114, 0);
  ibus_engine_forward_key_event(engine, IBUS_KEY_Right, 114,
                                IBUS_RELEASE_MASK);
}

// Replace [start,end) when we have reliable surrounding offsets.
bool replace_char_range(IBusEngine *engine, KeyboopEngine *self, guint cursor,
                        guint start, guint end, const std::string &expect,
                        const std::string &insert) {
  if (end <= start || insert.empty() || expect.empty())
    return false;
  const int n = static_cast<int>(end - start);
  if (static_cast<int>(keyboop::utf8_length(expect)) != n)
    return false;
  const gint offset = static_cast<gint>(start) - static_cast<gint>(cursor);

  self->muted = TRUE;
  ibus_engine_hide_preedit_text(engine);

  bool deleted = false;
  IBusText *st = nullptr;
  guint c2 = 0, a2 = 0;
  ibus_engine_get_surrounding_text(engine, &st, &c2, &a2);
  if (st) {
    const gchar *txt = ibus_text_get_text(st);
    if (txt && utf8_slice_chars(txt, start, end) == expect) {
      ibus_engine_delete_surrounding_text(engine, offset,
                                          static_cast<guint>(n));
      deleted = true;
    }
    g_object_unref(st);
  }
  if (!deleted && end == cursor && cursor >= static_cast<guint>(n)) {
    for (int i = 0; i < n; ++i)
      forward_backspace(engine);
    deleted = true;
  }
  if (!deleted) {
    self->muted = FALSE;
    return false;
  }

  commit_utf8(engine, insert);
  collapse_selection(engine);
  committed_clear(self);
  self->core->clear_context();
  self->muted = FALSE;
  return true;
}

// Selection / PRIMARY: collapse highlight (Right), then erase exact length.
// Never DeleteSurroundingText while a highlight may be active — GTK/Wayland
// then also deletes neighbors («сраную тему» before the selection).
bool replace_selected_text(IBusEngine *engine, KeyboopEngine *self,
                           const std::string &selected,
                           const std::string &insert) {
  if (selected.empty() || insert.empty())
    return false;
  const int n = static_cast<int>(keyboop::utf8_length(selected));
  if (n <= 0)
    return false;

  self->muted = TRUE;
  ibus_engine_hide_preedit_text(engine);
  // Drop highlight without deleting content; caret goes to the end edge.
  collapse_selection(engine);
  for (int i = 0; i < n; ++i)
    forward_backspace(engine);
  commit_utf8(engine, insert);
  collapse_selection(engine);
  committed_clear(self);
  self->core->clear_context();
  self->muted = FALSE;
  return true;
}

// Delete the last `n` committed characters, then commit `insert`.
// Updates `committed` to reflect the on-screen phrase (keeps earlier words).
bool replace_committed_suffix(IBusEngine *engine, KeyboopEngine *self, int n,
                              const std::string &insert) {
  if (!self->committed || n <= 0)
    return false;
  const std::string expect = utf8_suffix(*self->committed, n);
  if (expect.empty() ||
      static_cast<int>(keyboop::utf8_length(expect)) != n)
    return false;

  self->muted = TRUE;
  ibus_engine_hide_preedit_text(engine);

  bool deleted = false;
  IBusText *st = nullptr;
  guint cursor = 0, anchor = 0;
  ibus_engine_get_surrounding_text(engine, &st, &cursor, &anchor);
  if (st && cursor == anchor && cursor >= static_cast<guint>(n)) {
    const gchar *txt = ibus_text_get_text(st);
    const gchar *end =
        g_utf8_offset_to_pointer(txt, static_cast<glong>(cursor));
    const gchar *start =
        g_utf8_offset_to_pointer(txt, static_cast<glong>(cursor) - n);
    std::string suffix(start, end);
    if (suffix == expect) {
      ibus_engine_delete_surrounding_text(engine, -n, static_cast<guint>(n));
      deleted = true;
    }
  }
  if (st)
    g_object_unref(st);

  if (!deleted) {
    for (int i = 0; i < n; ++i)
      forward_backspace(engine);
  }

  commit_utf8(engine, insert);
  collapse_selection(engine);
  utf8_drop_suffix(*self->committed, n);
  self->committed->append(insert);
  self->muted = FALSE;
  return true;
}

void switch_log(const char *msg, const char *extra = nullptr) {
  // ponytail: diagnose layout-switch until this path is proven on GNOME
  FILE *f = fopen("/tmp/keyboop-switch.log", "a");
  if (!f)
    return;
  fprintf(f, "%lld %s", static_cast<long long>(time(nullptr)), msg);
  if (extra)
    fprintf(f, " %s", extra);
  fputc('\n', f);
  fclose(f);
}

void maybe_switch_layout(bool to_cyrillic) {
  // GNOME Wayland: ibus_bus_set_global_engine reports ok but Shell keeps the
  // panel/xkb on the old source. Real switch = InputSource.activate() via the
  // keyboop-switch@keyboop extension (D-Bus).
  auto &km = keyboop::ActiveKeymap::shared();
  const auto &id =
      to_cyrillic ? km.cyrillic_layout() : km.latin_layout();
  if (id.empty()) {
    switch_log("skip empty layout id");
    return;
  }
  std::string name = keyboop::keyboop_engine_name(id);
  if (g_bus && ibus_bus_is_connected(g_bus)) {
    IBusEngineDesc *cur = ibus_bus_get_global_engine(g_bus);
    if (cur) {
      const gchar *cur_name = ibus_engine_desc_get_name(cur);
      const bool same = cur_name && name == cur_name;
      if (cur_name)
        switch_log(same ? "skip same" : "will switch", cur_name);
      g_object_unref(cur);
      if (same)
        return;
    }
  }
  switch_log("schedule", name.c_str());
  gchar *hold = g_strdup(name.c_str());
  g_timeout_add(
      80,
      [](gpointer data) -> gboolean {
        gchar *engine = static_cast<gchar *>(data);
        switch_log("timeout fire", engine);
        // 1) Shell extension — updates panel + IBus together
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
          switch_log("shell activate spawned", engine);
          g_child_watch_add(
              pid,
              [](GPid p, gint status, gpointer eng) {
                g_spawn_close_pid(p);
                const bool ok = g_spawn_check_wait_status(status, nullptr);
                switch_log(ok ? "shell activate exit-ok" : "shell activate exit-fail",
                           static_cast<gchar *>(eng));
                // Fallback if extension missing / failed: IBus-only (panel may
                // stay wrong on Wayland — better than nothing).
                if (!ok && g_bus && ibus_bus_is_connected(g_bus)) {
                  ibus_bus_set_global_engine_async(g_bus,
                                                   static_cast<gchar *>(eng),
                                                   -1, nullptr, nullptr,
                                                   nullptr);
                  switch_log("fallback set_global_engine",
                             static_cast<gchar *>(eng));
                }
                g_free(eng);
              },
              engine);
          return G_SOURCE_REMOVE;
        }
        if (spawn_err) {
          switch_log("shell activate spawn-err", spawn_err->message);
          g_error_free(spawn_err);
        }
        if (g_bus && ibus_bus_is_connected(g_bus)) {
          ibus_bus_set_global_engine_async(g_bus, engine, -1, nullptr, nullptr,
                                           nullptr);
          switch_log("fallback set_global_engine", engine);
        }
        g_free(engine);
        return G_SOURCE_REMOVE;
      },
      hold);
}

bool flip_to_cyrillic(const std::string &phrase) {
  return keyboop::has_latin_letter(phrase) && !keyboop::has_cyrillic(phrase);
}

bool apply_replace(IBusEngine *engine, KeyboopEngine *self,
                   const keyboop::ReplaceAction &act,
                   const std::string &word_suffix) {
  if (act.insert.empty() && act.delete_count <= 0)
    return false;
  if (!self->committed)
    return false;
  // Replace only word_suffix at the end of the committed phrase.
  const int n = static_cast<int>(keyboop::utf8_length(word_suffix));
  if (n <= 0 || utf8_suffix(*self->committed, n) != word_suffix)
    return false;
  if (!replace_committed_suffix(engine, self, n, act.insert))
    return false;
  maybe_switch_layout(act.switch_to_cyrillic);
  return true;
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
  // Ask clients for surrounding text so DeleteSurroundingText can work.
  gboolean set = FALSE;
  for (guint i = 0; i < n_params; ++i) {
    if (g_strcmp0(params[i].pspec->name, "active-surrounding-text") == 0) {
      g_value_set_boolean(params[i].value, TRUE);
      set = TRUE;
      break;
    }
  }
  (void)set;
  return G_OBJECT_CLASS(keyboop_engine_parent_class)
      ->constructor(type, n_params, params);
}

static void keyboop_engine_init(KeyboopEngine *self) {
  self->core = &shared_core();
  self->layout = new keyboop::XkbLayoutId();
  self->engine_name = nullptr;
  self->committed = new std::string();
  self->muted = FALSE;
  self->purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
  self->caps = 0;
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
  self->core = nullptr; // shared
  delete self->layout;
  self->layout = nullptr;
  delete self->committed;
  self->committed = nullptr;
  g_clear_pointer(&self->engine_name, g_free);
  G_OBJECT_CLASS(keyboop_engine_parent_class)->dispose(object);
}

static bool skip_auto(const KeyboopEngine *self) {
  return self->purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
         self->purpose == IBUS_INPUT_PURPOSE_PIN ||
         self->purpose == IBUS_INPUT_PURPOSE_TERMINAL;
}

static bool is_manual_hotkey(guint keyval, guint keycode, guint modifiers) {
  // Ignore Lock/NumLock noise.
  const guint mods =
      modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_META_MASK |
                   IBUS_SHIFT_MASK | IBUS_SUPER_MASK | IBUS_HYPER_MASK);
  const bool ctrl = (mods & IBUS_CONTROL_MASK) != 0;
  const bool alt =
      (mods & IBUS_MOD1_MASK) != 0 || (mods & IBUS_META_MASK) != 0;
  if (!ctrl || !alt)
    return false;
  if (mods & (IBUS_SHIFT_MASK | IBUS_SUPER_MASK | IBUS_HYPER_MASK))
    return false;
  // On RU layout the same physical key yields Cyrillic_el, not 'k'.
  if (keyval == IBUS_KEY_k || keyval == IBUS_KEY_K ||
      keyval == IBUS_KEY_Cyrillic_el || keyval == IBUS_KEY_Cyrillic_EL)
    return true;
  // Evdev KEY_K=37 → XKB keycode 45 (fallback if keyval is weird).
  if (keycode == 45)
    return true;
  return false;
}

static gboolean keyboop_process_key_event(IBusEngine *engine, guint keyval,
                                          guint keycode, guint modifiers) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  ensure_data_loaded();
  refresh_user_settings(*self->core);

  // Swallow keys while we synthesize BackSpaces/commits — returning FALSE let
  // GTK apply the same BackSpace again and wipe the previous word.
  if (self->muted)
    return TRUE;
  if (modifiers & IBUS_RELEASE_MASK)
    return FALSE;

  // Super+Space etc. arrive with MOD4, not only SUPER — never eat those.
  if (modifiers & (IBUS_SUPER_MASK | IBUS_MOD4_MASK | IBUS_HYPER_MASK))
    return FALSE;

  if (is_manual_hotkey(keyval, keycode, modifiers)) {
    if (skip_auto(self))
      return TRUE;
    refresh_active_keymap(self->layout);

    IBusText *st = nullptr;
    guint cursor = 0, anchor = 0;
    ibus_engine_get_surrounding_text(engine, &st, &cursor, &anchor);
    const gchar *txt = (st && ibus_text_get_text(st) &&
                        ibus_text_get_text(st)[0] != '\0')
                           ? ibus_text_get_text(st)
                           : nullptr;

    // --- Selection (IBus range, or PRIMARY that sits at the caret) ---
    std::string selected;
    guint sel_lo = 0, sel_hi = 0;
    bool have_sel_range = false;
    if (txt && cursor != anchor) {
      sel_lo = cursor < anchor ? cursor : anchor;
      sel_hi = cursor < anchor ? anchor : cursor;
      selected = utf8_slice_chars(txt, sel_lo, sel_hi);
      have_sel_range = !selected.empty();
    } else if (txt) {
      // Wayland: selection often not in cursor/anchor — PRIMARY still is.
      std::string primary = read_primary_selection();
      if (!primary.empty()) {
        const int pn = static_cast<int>(keyboop::utf8_length(primary));
        if (pn > 0 && cursor >= static_cast<guint>(pn) &&
            utf8_slice_chars(txt, cursor - static_cast<guint>(pn), cursor) ==
                primary) {
          selected = primary;
          sel_lo = cursor - static_cast<guint>(pn);
          sel_hi = cursor;
          have_sel_range = true;
        } else {
          // Highlight in the middle: PRIMARY equals some run near caret.
          auto run = keyboop::layout_flip_at(txt, cursor);
          if (!run.text.empty() && run.text == primary) {
            selected = primary;
            sel_lo = static_cast<guint>(run.start_cp);
            sel_hi = static_cast<guint>(run.end_cp);
            have_sel_range = true;
          }
          // Else: ignore stale PRIMARY — do not BackSpace blindly.
        }
      }
    }

    if (!selected.empty()) {
      std::string converted = keymap_flip(selected);
      const bool to_cyr = flip_to_cyrillic(selected);
      if (st)
        g_object_unref(st);
      if (!converted.empty() &&
          replace_selected_text(engine, self, selected, converted))
        maybe_switch_layout(to_cyr);
      return TRUE;
    }

    // --- No selection: same-script run at caret ---
    if (txt) {
      auto run = keyboop::layout_flip_at(txt, cursor);
      if (!run.text.empty()) {
        std::string converted = keymap_flip(run.text);
        if (!converted.empty()) {
          const bool to_cyr = flip_to_cyrillic(run.text);
          bool ok = false;
          if (run.end_cp == cursor && self->committed &&
              utf8_suffix(*self->committed,
                          static_cast<int>(keyboop::utf8_length(run.text))) ==
                  run.text) {
            ok = replace_committed_suffix(
                engine, self,
                static_cast<int>(keyboop::utf8_length(run.text)), converted);
            if (ok)
              self->core->clear_context();
          }
          if (!ok)
            ok = replace_char_range(engine, self, cursor,
                                    static_cast<guint>(run.start_cp),
                                    static_cast<guint>(run.end_cp), run.text,
                                    converted);
          if (ok)
            maybe_switch_layout(to_cyr);
          if (st)
            g_object_unref(st);
          // Even on failure: do not fall through to committed tip — that is
          // what scrambled mid-phrase / selection retries.
          return TRUE;
        }
      }
    }
    if (st)
      g_object_unref(st);

    // --- Fallback: committed tip ---
    if (!self->committed || self->committed->empty())
      return TRUE;
    const std::string phrase =
        keyboop::layout_flip_suffix(*self->committed);
    std::string converted = keymap_flip(phrase);
    if (converted.empty())
      return TRUE;
    const bool to_cyr = flip_to_cyrillic(phrase);
    const int n = static_cast<int>(keyboop::utf8_length(phrase));
    if (replace_committed_suffix(engine, self, n, converted)) {
      self->core->clear_context();
      maybe_switch_layout(to_cyr);
    }
    return TRUE;
  }

  if (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_META_MASK))
    return FALSE;

  auto &cfg = self->core->settings();

  auto passthrough_printable = [&]() -> gboolean {
    gunichar uc = ibus_keyval_to_unicode(keyval);
    if (uc == 0 || g_unichar_iscntrl(uc))
      return FALSE;
    char buf[8]{};
    gint len = g_unichar_to_utf8(uc, buf);
    if (len <= 0)
      return FALSE;
    commit_utf8(engine, std::string(buf, static_cast<size_t>(len)));
    return TRUE;
  };

  if (!cfg.enabled || skip_auto(self)) {
    if (keyval == IBUS_KEY_BackSpace || keyval == IBUS_KEY_Return ||
        keyval == IBUS_KEY_KP_Enter || keyval == IBUS_KEY_Tab ||
        keyval == IBUS_KEY_space)
      return FALSE;
    return passthrough_printable();
  }

  if (keyval == IBUS_KEY_BackSpace) {
    self->core->on_backspace();
    committed_pop(self);
    return FALSE;
  }
  if (keyval == IBUS_KEY_Escape || keyval == IBUS_KEY_Left ||
      keyval == IBUS_KEY_Right || keyval == IBUS_KEY_Up ||
      keyval == IBUS_KEY_Down || keyval == IBUS_KEY_Home ||
      keyval == IBUS_KEY_End || keyval == IBUS_KEY_Page_Up ||
      keyval == IBUS_KEY_Page_Down || keyval == IBUS_KEY_Delete) {
    self->core->clear_context();
    committed_clear(self);
    return FALSE;
  }

  if (keyval == IBUS_KEY_space && cfg.on_space) {
    const std::string word = self->core->buffer().current_word();
    if (auto act = self->core->on_boundary(" ")) {
      strip_pending(*act, " ", /*keep_ws_in_insert=*/true);
      if (!apply_replace(engine, self, *act, word)) {
        // Keep phrase tracking; let Space through.
        committed_add(self, " ");
        return FALSE;
      }
      return TRUE;
    }
    // No convert — keep earlier words in committed, append the space.
    committed_add(self, " ");
    return FALSE;
  }
  if ((keyval == IBUS_KEY_Return || keyval == IBUS_KEY_KP_Enter) &&
      cfg.on_enter) {
    const std::string word = self->core->buffer().current_word();
    if (auto act = self->core->on_boundary("\n")) {
      strip_pending(*act, "\n", /*keep_ws_in_insert=*/false);
      if (!apply_replace(engine, self, *act, word)) {
        /* keep committed; Enter goes to app */
      } else {
        committed_clear(self);
        self->core->clear_context();
      }
    } else {
      committed_clear(self);
    }
    return FALSE;
  }
  if (keyval == IBUS_KEY_Tab && cfg.on_tab) {
    const std::string word = self->core->buffer().current_word();
    if (auto act = self->core->on_boundary("\t")) {
      strip_pending(*act, "\t", /*keep_ws_in_insert=*/false);
      (void)apply_replace(engine, self, *act, word);
    }
    committed_clear(self);
    return FALSE;
  }

  gunichar uc = ibus_keyval_to_unicode(keyval);
  if (uc == 0 || g_unichar_iscntrl(uc))
    return FALSE;

  char buf[8]{};
  gint len = g_unichar_to_utf8(uc, buf);
  if (len <= 0)
    return FALSE;
  std::string ch(buf, static_cast<size_t>(len));

  commit_utf8(engine, ch);
  committed_add(self, ch);
  self->core->on_text(ch);
  if (cfg.auto_enabled && cfg.live_fix) {
    if (auto act = self->core->maybe_live_fix()) {
      const std::string &w = self->core->buffer().current_word();
      (void)apply_replace(engine, self, *act, w);
    }
  }
  return TRUE;
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
  if (self->core)
    self->core->clear_context();
  committed_clear(self);
}

static void keyboop_reset(IBusEngine *engine) {
  auto *self = reinterpret_cast<KeyboopEngine *>(engine);
  ibus_engine_hide_preedit_text(engine);
  if (self->core)
    self->core->clear_context();
  committed_clear(self);
}

static void keyboop_set_content_type(IBusEngine *engine, guint purpose,
                                     guint /*hints*/) {
  reinterpret_cast<KeyboopEngine *>(engine)->purpose = purpose;
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
  // GNOME/IBus prefer ISO 639-2 for the menu label ("Английский" / "Русский").
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
    return id.layout; // "ru", "de", …
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
  // Always use varargs so we can set symbol (panel en/ru, not en₁/en₂).
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
