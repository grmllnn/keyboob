#pragma once
#include "anti_resonance.hpp"
#include "exception_store.hpp"
#include "keystroke_buffer.hpp"
#include "layout_detector.hpp"
#include "snippet_store.hpp"
#include "undo_learner.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keyboop {

struct Settings {
  bool enabled = true;
  bool auto_enabled = true;
  bool on_space = true;
  bool on_enter = true;
  bool on_tab = true;
  // Off by default: live_fix rewrites mid-word and makes "type wrong then
  // fix" impossible to test; space/hotkey still convert.
  bool live_fix = false;
  std::string hotkey_mode = "combo";
  std::vector<std::string> manual_hotkeys = {"Control+Alt+k"};
  int double_tap_timeout_ms = 250;
};

/// Adapter contract: replace on-screen `expected` with `replacement`.
/// Boundary keys (Space/Enter/Tab) are NOT included — adapters own those.
struct ReplaceAction {
  std::string expected;
  std::string replacement;
  bool switch_to_cyrillic = false;
};

class Engine {
public:
  Engine(ExceptionStore &exc = ExceptionStore::shared(),
         SnippetStore &snip = SnippetStore::shared());

  Settings &settings() { return settings_; }
  const Settings &settings() const { return settings_; }
  KeystrokeBuffer &buffer() { return buffer_; }
  AntiResonanceGuard &anti_resonance() { return anti_; }
  UndoLearner &undo_learner() { return undo_; }

  void on_text(std::string_view utf8);
  void on_backspace();
  void clear_context();
  void soft_context_reset();

  std::optional<ReplaceAction> on_boundary(std::string_view whitespace,
                                           bool completed_only = true);
  std::optional<ReplaceAction> manual_convert();
  std::optional<ReplaceAction> maybe_live_fix();
  std::optional<ReplaceAction> undo_last();

  void set_after_caret_jump(bool v) { after_caret_jump_ = v; }

private:
  std::optional<ReplaceAction> apply_decision(const std::string &word,
                                              const std::string &tail,
                                              SwapDecision d, bool is_auto,
                                              bool completed_only);

  ExceptionStore &exc_;
  SnippetStore &snip_;
  Settings settings_;
  KeystrokeBuffer buffer_;
  AntiResonanceGuard anti_;
  UndoLearner undo_;
  bool after_caret_jump_ = false;

  std::string last_original_;
  std::string last_converted_;
  std::string last_tail_;
};

} // namespace keyboop
