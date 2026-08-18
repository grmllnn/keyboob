#include "anti_resonance.hpp"
#include "ambiguous_pairs.hpp"
#include "active_keymap.hpp"
#include "engine.hpp"
#include "exception_store.hpp"
#include "gnome_sources.hpp"
#include "keymap.hpp"
#include "keystroke_buffer.hpp"
#include "layout_data.hpp"
#include "layout_detector.hpp"
#include "snippet_store.hpp"
#include "undo_learner.hpp"
#include "user_settings.hpp"
#include "utf8.hpp"
#include "xkb_pair.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace keyboop;

static int g_fails = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond "\n";  \
      ++g_fails;                                                               \
    }                                                                          \
  } while (0)

static void test_keymap() {
  CHECK(Keymap::convert("hf,jnftn", true) == "работает");
  CHECK(Keymap::convert(",", true) == "б");
  CHECK(Keymap::convert("работает", false) == "hf,jnftn");
  {
    // us↔ru pair must keep «,»→«б» (xkb shift-level used to overwrite it).
    auto pair = build_xkb_pair(XkbLayoutId::parse("us"),
                               XkbLayoutId::parse("ru"));
    CHECK(pair.ok);
    CHECK(pair.to_cyrillic.at(",") == "б");
    CHECK(pair.to_latin.at("б") == ",");
    CHECK(pair.to_cyrillic.at("^") == ":");
    CHECK(pair.to_cyrillic.at("&") == "?");
    CHECK(pair.to_cyrillic.at(";") == "ж");
    auto &km = ActiveKeymap::shared();
    km.use_pair(std::move(pair));
    CHECK(km.convert("hf,jnftn", true) == "работает");
    CHECK(km.convert(",", true) == "б");
    CHECK(km.convert("^", true) == ":");
    CHECK(km.convert("&", true) == "?");
    CHECK(km.convert(":", true) == "Ж");
    CHECK(km.convert("?", true) == ",");
    CHECK(km.convert(";", true) == "ж");
    CHECK(km.convert("gbcfybye^", true) == "писанину:");
    CHECK(km.convert(":", false) == "^");
    CHECK(km.convert("Ж", false) == ":");
    CHECK(km.convert("?", false) == "&");
    CHECK(km.convert(",", false) == "?");
    km.use_builtin_us_ru();
  }
  CHECK(Keymap::convert("^", true) == ":");
  CHECK(Keymap::convert("&", true) == "?");
  CHECK(Keymap::convert("@", true) == "\"");
  CHECK(Keymap::convert("#", true) == "№");
  CHECK(Keymap::convert("$", true) == ";");
  CHECK(Keymap::convert("ghjdthztv gbcfybye^ cyfxfkf gbitv?", true) ==
        "проверяем писанину: сначала пишем,");
  // yj; → нож when valid target says so
  CHECK(Keymap::smart_convert("yj;", true, [](const std::string &w) {
          return w == "нож";
        }) == "нож");
  CHECK(Keymap::core("привет.") == "привет");
  CHECK(has_cyrillic("привет"));
  CHECK(has_latin_letter("hello"));
  CHECK(!has_cyrillic("hello"));
  CHECK(layout_flip_suffix("лдщзы туц") == "лдщзы туц");
  CHECK(layout_flip_suffix("ghbdtn fylhtq") == "ghbdtn fylhtq");
  CHECK(layout_flip_suffix("привет андрей rfr ltkf") == "rfr ltkf");
  CHECK(layout_flip_suffix("hello привет") == "привет");
  CHECK(layout_flip_suffix(" nhb") == "nhb");
  CHECK(Keymap::convert(layout_flip_suffix("привет андрей rfr ltkf"), true) ==
        "как дела");
  {
    const std::string mid = "раз nhb два";
    // caret after "nhb" (char offset 7: р а з _ n h b |)
    auto run = layout_flip_at(mid, 7);
    CHECK(run.text == "nhb");
    CHECK(Keymap::convert(run.text, true) == "три");
    // caret at end of mono Cyrillic word (spaces do not glue)
    auto all = layout_flip_at("раз два", 7);
    CHECK(all.text == "два");
    CHECK(Keymap::convert(all.text, false) == "ldf");
    auto multi = layout_flip_at("ntcnbv cyjdf", 12);
    CHECK(multi.text == "cyjdf");
    CHECK(Keymap::convert(multi.text, true) == "снова");
    // mid-phrase insertion: "ок тестим cerf снова"
    const std::string phrase2 = "ок тестим cerf снова";
    // caret at start of "cerf" (char offset 10)
    auto run_start = layout_flip_at(phrase2, 10);
    CHECK(run_start.text == "cerf");
    CHECK(run_start.start_cp == 10);
    CHECK(run_start.end_cp == 14);
    CHECK(Keymap::convert(run_start.text, true) == "сука");
    // caret at end of "cerf" (char offset 14)
    auto run_end = layout_flip_at(phrase2, 14);
    CHECK(run_end.text == "cerf");
    CHECK(run_end.start_cp == 10);
    CHECK(run_end.end_cp == 14);
    CHECK(Keymap::convert(run_end.text, true) == "сука");
    // multi-word tail in mixed sentence:
    const std::string phrase3 = "проверяем эту hf,jnftn yj yt lj rjywf";
    auto tail = layout_flip_at(phrase3, keyboop::utf8_length(phrase3));
    CHECK(tail.text == "rjywf");
    CHECK(Keymap::convert(tail.text, true) == "конца");
    const std::string bug =
        "gbitv-gbitv-gbitv b to` xtuj-b,elm gbitv";
    auto last = layout_flip_at(bug, keyboop::utf8_length(bug));
    CHECK(last.text == "gbitv");
    CHECK(Keymap::convert(last.text, true) == "пишем");
    const size_t end = utf8_length(bug);
    auto p_end = match_primary_snapshot(bug, end, "gbitv");
    CHECK(p_end.ok);
    CHECK(p_end.start_cp == end - 5);
    CHECK(p_end.end_cp == end);
    auto p_start = match_primary_snapshot(bug, end - 5, "gbitv");
    CHECK(p_start.ok);
    CHECK(p_start.start_cp == end - 5);
    CHECK(p_start.end_cp == end);
    auto hyphen = layout_flip_at("xtuj-b,elm", 10);
    CHECK(hyphen.text == "xtuj-b,elm");
    CHECK(Keymap::convert(hyphen.text, true) == "чего-ибудь");
    const std::string select_all =
        "t,jibv lkbbbyye. ghjdthre: djn gbbbbitv gjnjv "
        "gbitv-gbitv-gbitv b to` xtuj-b,elm gbitv";
    const std::string select_tail =
        "djn gbbbbitv gjnjv gbitv-gbitv-gbitv b to` xtuj-b,elm gbitv";
    CHECK(layout_flip_at(select_all, 0).text == "t,jibv");
    CHECK(Keymap::convert("t,jibv", true) == "ебошим");
    CHECK(match_primary_snapshot(select_all, 0, select_all).ok);
    CHECK(utf8_length(select_all) > 64);
    CHECK(utf8_length(select_tail) <= 64);
    const std::string trunc = select_all.substr(0, 40);
    CHECK(!match_primary_snapshot(trunc, 0, select_all).ok);
    CHECK(primary_extends_truncated_selection(trunc, 0, select_all));
    CHECK(primary_extends_truncated_selection(trunc, utf8_length(trunc),
                                              select_all));
    CHECK(!primary_extends_truncated_selection(trunc, 5, select_all));
    CHECK(!primary_extends_truncated_selection(select_all, 0, select_all));
    CHECK(!primary_extends_truncated_selection("gbitv", 0, select_tail));
    CHECK(!primary_extends_truncated_selection("hello", 0, "hello world"));
  }
  CHECK(to_lower_utf8("І") == "і");
  CHECK(to_lower_utf8("Ї") == "ї");
  CHECK(to_lower_utf8("Є") == "є");
  CHECK(to_lower_utf8("Ў") == "ў");
}

static void test_buffer() {
  KeystrokeBuffer b;
  b.append("ghb");
  b.append("dtn");
  CHECK(b.current_word() == "ghbdtn");
  b.boundary(" ");
  CHECK(b.current_word().empty());
  CHECK(b.last_word() == "ghbdtn");
  CHECK(b.last_tail() == " ");
  b.backspace(); // eat space
  CHECK(b.last_tail().empty());
  b.backspace(); // re-open word, drop last char
  CHECK(b.current_word() == "ghbdt");
}

static void test_anti_resonance() {
  AntiResonanceGuard g(0.7, 6, 2.5);
  double t = 100.0;
  g.set_clock([&] { return t; });
  CHECK(g.allow("a", "b"));
  CHECK(g.allow("b", "a") == false); // oscillation
  CHECK(g.is_frozen());
  t += 3.0;
  CHECK(!g.is_frozen());
}

static void test_snippets() {
  SnippetStore s;
  s.set_all({{"!test", "ok"}, {"итд", "и так далее"}});
  CHECK(s.expansion_for_typed("!test").value_or("") == "ok");
  CHECK(s.expansion_for_typed("!еуые").value_or("") == "ok"); // same keys
  CHECK(s.expansion_for_typed("итд").value_or("") == "и так далее");
}

static void test_detector() {
  auto &data = LayoutData::shared();
  const char *dir = std::getenv("KEYBOOP_DATA_DIR");
  std::string path = dir ? dir : KEYBOOP_DEFAULT_DATA_DIR;
  CHECK(data.load(path));

  ExceptionStore exc;
  auto d = LayoutDetector::decide("ghbdtn", exc);
  CHECK(d == SwapDecision::convert(true));

  d = LayoutDetector::decide("hello", exc);
  CHECK(d.is_keep());

  d = LayoutDetector::decide("yt", exc, std::string_view("привет"));
  CHECK(d == SwapDecision::convert(true)); // yt → не in RU context

  // forceRuAmb
  d = LayoutDetector::decide("lf", exc);
  CHECK(d == SwapDecision::convert(true)); // → да

  // Trailing punctuation tests (must not flip real English/Russian words)
  CHECK(LayoutDetector::decide("hello,", exc).is_keep());
  CHECK(LayoutDetector::decide("world.", exc).is_keep());
  CHECK(LayoutDetector::decide("test!", exc).is_keep());
  CHECK(LayoutDetector::decide("code;", exc).is_keep());
  CHECK(LayoutDetector::decide("item:", exc).is_keep());
  CHECK(LayoutDetector::decide("value?", exc).is_keep());
  CHECK(LayoutDetector::decide("привет,", exc).is_keep());
  CHECK(LayoutDetector::decide("мир.", exc).is_keep());
  CHECK(LayoutDetector::decide("ghbdtn,", exc) == SwapDecision::convert(true));
  CHECK(LayoutDetector::decide("ghbdtn.", exc) == SwapDecision::convert(true));
  CHECK(LayoutDetector::decide("hf,jnftn", exc) == SwapDecision::convert(true));
  CHECK(LayoutDetector::decide("uhb,", exc) == SwapDecision::convert(true));
  CHECK(LayoutDetector::decide("to`", exc) == SwapDecision::convert(true)); // to` -> ещё
  CHECK(LayoutDetector::decide("hfp", exc) == SwapDecision::convert(true)); // hfp -> раз

  // ignored exception (regression: must not convert a listed keep-word)
  exc.add_ignored("ghbdtn");
  d = LayoutDetector::decide("ghbdtn", exc);
  CHECK(d.is_keep());
}

static void test_engine_boundary_pending() {
  // Boundary key is adapter-owned: expected/replacement are the word only.
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.on_text("ghbdtn");
  auto act = eng.on_boundary(" ");
  CHECK(act.has_value());
  CHECK(act->expected == "ghbdtn");
  CHECK(act->replacement.find("привет") == 0);
  CHECK(!act->replacement.ends_with(" "));
  CHECK(act->switch_to_cyrillic);
}

static void test_engine_live_pending() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  // Build an impossible Latin word that live_decide flips (need ≥4 letters)
  eng.on_text("ghbd");
  eng.on_text("tn");
  auto act = eng.maybe_live_fix();
  if (act) {
    CHECK(act->expected == eng.buffer().current_word() ||
          act->replacement == eng.buffer().current_word());
    CHECK(act->switch_to_cyrillic);
  }
}

static void test_auto_off_skips_boundary() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ActiveKeymap::shared().use_builtin_us_ru();
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.settings().auto_enabled = false;
  eng.on_text("ghbdtn");
  CHECK(!eng.on_boundary(" ").has_value());
  CHECK(!eng.maybe_live_fix().has_value());

  Engine man(exc, snip);
  man.settings().auto_enabled = false;
  man.on_text("ghbdtn");
  auto act = man.manual_convert();
  CHECK(act.has_value());
  if (act)
    CHECK(act->replacement == "привет");
}

static void test_load_user_settings_auto_file() {
  namespace fs = std::filesystem;
  const char *old = std::getenv("HOME");
  const std::string old_home = old ? old : "";
  const auto tmp = fs::temp_directory_path() /
                   ("keyboop-settings-test-" + std::to_string(getpid()));
  fs::create_directories(tmp);
  setenv("HOME", tmp.c_str(), 1);
  UserSettings off;
  off.auto_enabled = false;
  std::string err;
  CHECK(save_user_settings(off, &err));
  auto loaded = load_user_settings();
  CHECK(!loaded.auto_enabled);

  UserSettings hk;
  hk.auto_enabled = false;
  hk.hotkey = "Control+Shift+q";
  CHECK(save_user_settings(hk, &err));
  loaded = load_user_settings();
  CHECK(!loaded.auto_enabled);
  CHECK(loaded.hotkey == "Control+Shift+q");
  HotkeySpec spec;
  CHECK(parse_hotkey("ctrl+alt+K", &spec));
  CHECK(spec.ctrl && spec.alt && !spec.shift && spec.key == "k");
  CHECK(format_hotkey(spec) == "Control+Alt+k");
  CHECK(!parse_hotkey("Nope+k", &spec));

  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.settings().auto_enabled = loaded.auto_enabled;
  eng.on_text("ghbdtn");
  CHECK(!eng.on_boundary(" ").has_value());

  if (!old_home.empty())
    setenv("HOME", old_home.c_str(), 1);
  else
    unsetenv("HOME");
  std::error_code ec;
  fs::remove_all(tmp, ec);
}

static void test_engine_manual() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.on_text("ghbdtn");
  auto act = eng.manual_convert();
  CHECK(act.has_value());
  CHECK(act->expected == "ghbdtn");
  CHECK(act->replacement.find("привет") == 0);
  // UI layer clears after successful replace; simulate that.
  eng.buffer().clear();
  CHECK(!eng.manual_convert().has_value());
}

static void test_undo_learner() {
  ExceptionStore exc;
  UndoLearner u(exc);
  double t = 1000;
  u.set_clock([&] { return t; });
  int suggests = 0;
  u.on_suggest_learn = [&](const std::string &) { ++suggests; };
  u.notice_auto_conversion("ghbdtn", "привет");
  u.notice_manual_reflip("привет", "ghbdtn");
  u.notice_auto_conversion("ghbdtn", "привет");
  t += 0.1;
  u.notice_manual_reflip("привет", "ghbdtn");
  u.notice_auto_conversion("ghbdtn", "привет");
  t += 0.1;
  u.notice_manual_reflip("привет", "ghbdtn");
  CHECK(suggests == 1);
  u.accept_learn("ghbdtn");
  CHECK(exc.learned().count("ghbdtn") == 1);

  // U2 retype path (fresh store — ghbdtn already learned above)
  ExceptionStore exc2;
  UndoLearner u2(exc2);
  u2.set_clock([&] { return t; });
  u2.notice_auto_conversion("ghbdtn", "привет");
  CHECK(u2.observe("приве") == false);
  CHECK(u2.observe("") == false);
  CHECK(u2.should_suppress("ghb"));
  CHECK(u2.observe("ghbdtn"));
  CHECK(u2.is_session_protected("ghbdtn"));
}

static void test_ambiguous_pairs() {
  ExceptionStore exc;
  const auto &pairs = AmbiguousPairs::list();
  CHECK(pairs.size() >= 36);
  const auto &p = pairs.front(); // vs / мы
  CHECK(AmbiguousPairs::choice(p, exc) == AmbiguousPairs::Choice::Auto);
  exc.add_force_swap("мы");
  CHECK(AmbiguousPairs::choice(p, exc) == AmbiguousPairs::Choice::Ru);
  AmbiguousPairs::choose(p, AmbiguousPairs::Choice::Ru, exc);
  CHECK(exc.force_swap().count("мы") == 1);
  CHECK(exc.force_swap().count("vs") == 0);
}

static void test_letter_core() {
  CHECK(LayoutDetector::letter_core("(tckb)") == "tckb");
  CHECK(LayoutDetector::letter_core("gj1") == "gj1");
  CHECK(LayoutDetector::is_layout_letter('['));
  CHECK(!LayoutDetector::is_layout_letter('-'));
}

static void test_keymap_more() {
  CHECK(Keymap::convert("GHBDTN", true) == "ПРИВЕТ");
  CHECK(Keymap::convert("Q", true) == "Й");
  CHECK(Keymap::convert(".", true) == "ю");
  CHECK(Keymap::convert("123", true) == "123");
  CHECK(Keymap::smart_convert("...", true) == "...");
  CHECK(Keymap::smart_convert("yj;", true) == "но;");
}

static void test_xkb_pair_us_ru() {
  auto pair = build_xkb_pair(XkbLayoutId::parse("us"), XkbLayoutId::parse("ru"));
  CHECK(pair.ok);
  CHECK(pair.to_cyrillic.size() >= 50);
  ActiveKeymap::shared().use_pair(pair);
  CHECK(Keymap::convert("ghbdtn", true) == "привет");
  CHECK(Keymap::convert("привет", false) == "ghbdtn");
  CHECK(Keymap::convert("{", true) == "Х");
  CHECK(Keymap::convert("}", true) == "Ъ");
  CHECK(Keymap::convert(":", true) == "Ж");
  CHECK(Keymap::convert("\"", true) == "Э");
  CHECK(Keymap::convert("<", true) == "Б");
  CHECK(Keymap::convert(">", true) == "Ю");
  CHECK(Keymap::convert("?", true) == ",");
  CHECK(to_lower_utf8("І") == "і");
  CHECK(to_lower_utf8("Ї") == "ї");
  CHECK(to_lower_utf8("Є") == "є");
  CHECK(to_lower_utf8("Ў") == "ў");
}

static void test_gnome_sources_parse() {
  auto s = parse_gnome_sources_value(
      "[('xkb', 'us(intl)'), ('xkb', 'ru+phonetic'), ('ibus', 'keyboop:us')]");
  CHECK(s.size() == 3);
  CHECK(s[0].kind == InputSource::Kind::Xkb);
  CHECK(s[0].id == "us(intl)");
  CHECK(s[1].id == "ru+phonetic");
  CHECK(s[2].kind == InputSource::Kind::IBusKeyboop);
  auto layouts = layout_ids_from_sources(s);
  CHECK(layouts.size() == 3);
  CHECK(layouts[0].layout == "us");
  CHECK(layouts[0].variant == "intl");
  CHECK(layouts[1].layout == "ru");
  CHECK(layouts[1].variant == "phonetic");
  CHECK(keyboop_engine_name(XkbLayoutId::parse("ru+phonetic")) ==
        "keyboop:ru+phonetic");
}

static void test_edit_action_contract() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ActiveKeymap::shared().use_builtin_us_ru();

  {
    ExceptionStore exc;
    SnippetStore snip;
    Engine eng(exc, snip);
    eng.on_text("ghbdtn");
    auto act = eng.on_boundary(" ");
    CHECK(act.has_value());
    if (!act)
      return;
    CHECK(act->expected == "ghbdtn");
    CHECK(act->replacement == "привет");
  }
  {
    ExceptionStore exc;
    SnippetStore snip;
    snip.set_all({{"ghb", "hello"}});
    Engine eng(exc, snip);
    eng.on_text("ghb");
    auto act = eng.on_boundary(" ");
    CHECK(act.has_value());
    if (!act)
      return;
    CHECK(act->expected == "ghb");
    CHECK(act->replacement == "hello");
  }
}

static void test_preedit_boundary_and_focus_flush() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.on_text("ghbdtn");
  const std::string preedit = eng.buffer().current_word();
  auto act = eng.on_boundary(" ");
  CHECK(preedit == "ghbdtn");
  CHECK(act.has_value());
  CHECK(act->expected == preedit);
  CHECK(act->replacement == "привет");

  Engine flush_eng(exc, snip);
  flush_eng.on_text("hf,jn");
  const std::string snapshot = flush_eng.buffer().current_word();
  CHECK(snapshot == "hf,jn");
  flush_eng.clear_context();
  CHECK(flush_eng.buffer().current_word().empty());
  CHECK(snapshot == "hf,jn");
}

static void test_stale_surrounding_primary() {
  CHECK(utf8_valid("привет"));
  CHECK(!utf8_valid("\xff\xfe"));
  const std::string around = "hello привет";
  // caret after "привет"
  const size_t cur = utf8_length(around);
  auto ok = match_expected_at_caret(around, cur, "привет");
  CHECK(ok.ok);
  CHECK(ok.start_cp == utf8_length("hello "));
  CHECK(ok.end_cp == cur);

  auto miss = match_expected_at_caret(around, cur, "ghbdtn");
  CHECK(!miss.ok);

  auto primary_ok = match_primary_snapshot(around, cur, "привет");
  CHECK(primary_ok.ok);
  auto stale = match_primary_snapshot(around, cur, "clipboard leftover");
  CHECK(!stale.ok);
  CHECK(!utf8_valid("bad\x80text"));
  CHECK(!match_primary_snapshot("not utf8 \xff", 3, "abc").ok);
}

static void test_buffer_without_preedit_needs_surrounding() {
  // IBus terminal tracks current_word but does not show preedit. Applying
  // ReplaceAction as commit-only would stack (ghbdtnпривет). Commit-only is
  // legal only while preedit is visible; otherwise expected must match
  // surrounding at caret, else no-op.
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ActiveKeymap::shared().use_builtin_us_ru();
  ExceptionStore exc;
  SnippetStore snip;
  Engine eng(exc, snip);
  eng.on_text("ghbdtn");
  auto act = eng.manual_convert();
  CHECK(act.has_value());
  if (!act)
    return;
  CHECK(act->expected == "ghbdtn");
  CHECK(act->replacement == "привет");

  auto around = act->expected;
  auto hit =
      match_expected_at_caret(around, utf8_length(around), act->expected);
  CHECK(hit.ok);
  CHECK(hit.start_cp == 0);
  CHECK(hit.end_cp == utf8_length(around));
  // no surrounding / mismatch → cannot replace; must not commit-only
  CHECK(!match_expected_at_caret("", 0, act->expected).ok);
  CHECK(!match_expected_at_caret("other", utf8_length("other"), act->expected)
             .ok);
}

static void test_pick_pair() {
  std::vector<XkbLayoutId> layouts = {XkbLayoutId::parse("us"),
                                      XkbLayoutId::parse("ru")};
  auto pair = pick_latin_cyrillic_pair(layouts, nullptr);
  CHECK(pair.ok);
  CHECK(pair.latin.id() == "us");
  CHECK(pair.cyrillic.id() == "ru");
}

static void test_fcitx_im_names() {
  auto us = parse_fcitx_im_name("keyboard-us");
  CHECK(us.layout == "us" && us.variant.empty());
  auto ru = parse_fcitx_im_name("keyboard-ru-phonetic");
  CHECK(ru.layout == "ru" && ru.variant == "phonetic");
  CHECK(parse_fcitx_im_name("pinyin").empty());
  std::vector<std::string> names = {"keyboard-us", "keyboard-ru-phonetic"};
  CHECK(fcitx_im_for_layout(names, XkbLayoutId::parse("ru+phonetic")) ==
        "keyboard-ru-phonetic");
  CHECK(fcitx_im_for_layout(names, XkbLayoutId::parse("us")) == "keyboard-us");
  CHECK(fcitx_im_for_layout(names, XkbLayoutId::parse("de")).empty());
}


int main() {
  test_keymap();
  test_keymap_more();
  test_xkb_pair_us_ru();
  test_gnome_sources_parse();
  test_pick_pair();
  test_fcitx_im_names();
  test_edit_action_contract();
  test_preedit_boundary_and_focus_flush();
  test_stale_surrounding_primary();
  test_buffer_without_preedit_needs_surrounding();
  test_buffer();
  test_anti_resonance();
  test_snippets();
  test_detector();
  test_letter_core();
  test_ambiguous_pairs();
  test_engine_manual();
  test_auto_off_skips_boundary();
  test_load_user_settings_auto_file();
  test_engine_boundary_pending();
  test_engine_live_pending();
  test_undo_learner();
  if (g_fails) {
    std::cerr << g_fails << " checks failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
