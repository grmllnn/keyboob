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
#include "utf8.hpp"
#include "xkb_pair.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

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
    auto &km = ActiveKeymap::shared();
    km.use_pair(std::move(pair));
    CHECK(km.convert("hf,jnftn", true) == "работает");
    CHECK(km.convert(",", true) == "б");
    km.use_builtin_us_ru();
  }
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
    // caret at end of mono Cyrillic phrase → whole phrase (reverse works)
    auto all = layout_flip_at("раз два", 7);
    CHECK(all.text == "раз два");
    CHECK(Keymap::convert(all.text, false) == "hfp ldf");
    // comma is the US key for «б» — must stay inside the latin run
    const std::string phrase = "проверяем эту hf,jnftn yj yt lj rjywf";
    auto tail = layout_flip_at(phrase, keyboop::utf8_length(phrase));
    CHECK(tail.text == "hf,jnftn yj yt lj rjywf");
    CHECK(Keymap::convert(tail.text, true) == "работает но не до конца");
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

  // ignored exception
  exc.add_ignored("ghbdtn");
  d = LayoutDetector::decide("ghbdtn", exc);
  CHECK(d.is_keep());
}

static void test_engine_boundary_pending() {
  // PreInputMethod contract: boundary key is not on screen yet, but Engine
  // includes it in delete_count + insert. Platform glue must subtract.
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
  CHECK(act->insert.find("привет") == 0);
  CHECK(act->insert.ends_with(" "));
  CHECK(act->delete_count ==
        static_cast<int>(utf8_length("ghbdtn") + utf8_length(" ")));
  // After strip (what the addon does for Space):
  int pending = static_cast<int>(utf8_length(" "));
  int on_screen = act->delete_count - pending;
  CHECK(on_screen == static_cast<int>(utf8_length("ghbdtn")));
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
    CHECK(act->delete_count ==
          static_cast<int>(utf8_length(eng.buffer().current_word())));
    CHECK(act->switch_to_cyrillic);
  }
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
  CHECK(act->insert.find("привет") == 0);
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
  // restore builtin for later tests that assume it
  ActiveKeymap::shared().use_builtin_us_ru();
}

static void test_gnome_sources_parse() {
  auto s = parse_gnome_sources_value(
      "[('xkb', 'us'), ('xkb', 'ru'), ('ibus', 'keyboop:us')]");
  CHECK(s.size() == 3);
  CHECK(s[0].kind == InputSource::Kind::Xkb);
  CHECK(s[0].id == "us");
  CHECK(s[1].id == "ru");
  CHECK(s[2].kind == InputSource::Kind::IBusKeyboop);
  auto layouts = layout_ids_from_sources(s);
  CHECK(layouts.size() == 3);
  CHECK(keyboop_engine_name(XkbLayoutId::parse("ru+phonetic")) ==
        "keyboop:ru+phonetic");
}

static void test_strip_pending_semantics() {
  auto &data = LayoutData::shared();
  if (!data.is_loaded()) {
    const char *dir = std::getenv("KEYBOOP_DATA_DIR");
    data.load(dir ? dir : KEYBOOP_DEFAULT_DATA_DIR);
  }
  ActiveKeymap::shared().use_builtin_us_ru();

  // Convert path: delete includes pending space
  {
    ExceptionStore exc;
    SnippetStore snip;
    Engine eng(exc, snip);
    eng.on_text("ghbdtn");
    auto act = eng.on_boundary(" ");
    CHECK(act.has_value());
    if (!act)
      return;
    int before = act->delete_count;
    const int n = 1;
    int insert_wo = static_cast<int>(utf8_length(act->insert) - n);
    CHECK(act->delete_count == insert_wo + n);
    act->delete_count = insert_wo;
    CHECK(act->delete_count == before - 1);
    CHECK(act->delete_count == static_cast<int>(utf8_length("привет")));
  }
  // Snippet path: delete is trigger only (no pending in delete_count)
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
    CHECK(act->insert == "hello ");
    int insert_wo = static_cast<int>(utf8_length(act->insert) - 1);
    // must NOT strip — delete_count != insert_wo + 1
    CHECK(act->delete_count != insert_wo + 1);
    CHECK(act->delete_count == static_cast<int>(utf8_length("ghb")));
  }
}

static void test_pick_pair() {
  std::vector<XkbLayoutId> layouts = {XkbLayoutId::parse("us"),
                                      XkbLayoutId::parse("ru")};
  auto pair = pick_latin_cyrillic_pair(layouts, nullptr);
  CHECK(pair.ok);
  CHECK(pair.latin.id() == "us");
  CHECK(pair.cyrillic.id() == "ru");
}


int main() {
  test_keymap();
  test_keymap_more();
  test_xkb_pair_us_ru();
  test_gnome_sources_parse();
  test_pick_pair();
  test_strip_pending_semantics();
  test_buffer();
  test_anti_resonance();
  test_snippets();
  test_detector();
  test_letter_core();
  test_ambiguous_pairs();
  test_engine_manual();
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
