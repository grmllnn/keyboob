#include "anti_resonance.hpp"
#include "ambiguous_pairs.hpp"
#include "engine.hpp"
#include "exception_store.hpp"
#include "keymap.hpp"
#include "keystroke_buffer.hpp"
#include "layout_data.hpp"
#include "layout_detector.hpp"
#include "snippet_store.hpp"
#include "undo_learner.hpp"
#include "utf8.hpp"

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
  CHECK(Keymap::convert("ghbdtn", true) == "привет");
  CHECK(Keymap::convert("привет", false) == "ghbdtn");
  CHECK(Keymap::smart_convert("ghbdtn.", true) == "привет.");
  // yj; → нож when valid target says so
  CHECK(Keymap::smart_convert("yj;", true, [](const std::string &w) {
          return w == "нож";
        }) == "нож");
  CHECK(Keymap::core("привет.") == "привет");
  CHECK(has_cyrillic("привет"));
  CHECK(has_latin_letter("hello"));
  CHECK(!has_cyrillic("hello"));
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

int main() {
  test_keymap();
  test_keymap_more();
  test_buffer();
  test_anti_resonance();
  test_snippets();
  test_detector();
  test_letter_core();
  test_ambiguous_pairs();
  test_engine_manual();
  test_undo_learner();
  if (g_fails) {
    std::cerr << g_fails << " checks failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
