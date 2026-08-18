#include "engine.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"
#include "utf8.hpp"

namespace keyboop {

Engine::Engine(ExceptionStore &exc, SnippetStore &snip)
    : exc_(exc), snip_(snip), undo_(exc) {
  exc_.seed_defaults();
}

void Engine::on_text(std::string_view utf8) {
  buffer_.append(utf8);
  undo_.observe(buffer_.current_word());
}

void Engine::on_backspace() {
  buffer_.backspace();
  undo_.observe(buffer_.current_word());
}

void Engine::clear_context() {
  buffer_.clear();
  anti_.reset_history();
  undo_.clear_session();
  after_caret_jump_ = true;
}

void Engine::soft_context_reset() {
  buffer_.soft_context_reset();
  after_caret_jump_ = true;
}

std::optional<ReplaceAction>
Engine::apply_decision(const std::string &word, const std::string &tail,
                       SwapDecision d, bool is_auto, bool completed_only) {
  if (d.is_keep())
    return std::nullopt;
  auto converted =
      Keymap::smart_convert(word, d.to_cyrillic, [&](const std::string &w) {
        return LayoutData::shared().has_word_ru(w);
      });
  if (converted == word)
    return std::nullopt;
  if (is_auto &&
      (undo_.should_suppress(word) || undo_.is_session_protected(word)))
    return std::nullopt;
  if (is_auto && !anti_.allow(word, converted)) {
    buffer_.clear();
    return std::nullopt;
  }
  if (completed_only)
    buffer_.apply_completed_conversion(converted);
  else
    buffer_.apply_conversion(converted);

  last_original_ = word;
  last_converted_ = converted;
  last_tail_ = tail;
  if (is_auto)
    undo_.notice_auto_conversion(word, converted);

  return ReplaceAction{word + tail, converted + tail, d.to_cyrillic};
}

std::optional<ReplaceAction>
Engine::on_boundary(std::string_view whitespace, bool completed_only) {
  (void)completed_only;
  if (!buffer_.current_word().empty()) {
    if (auto exp = snip_.expansion_for_typed(buffer_.current_word())) {
      std::string trigger = buffer_.current_word();
      buffer_.commit_snippet(*exp, std::string(whitespace));
      return ReplaceAction{std::move(trigger), *exp, false};
    }
  }

  std::string word;
  if (!buffer_.current_word().empty())
    word = buffer_.current_word();
  else
    return std::nullopt;

  if (auto mixed = LayoutDetector::mixed_rescue(word);
      !mixed.is_keep() && settings_.auto_enabled && settings_.enabled) {
    buffer_.boundary(whitespace);
    return apply_decision(word, "", mixed, true, true);
  }

  buffer_.boundary(whitespace);

  if (!settings_.enabled || !settings_.auto_enabled)
    return std::nullopt;

  auto done = buffer_.word_for_conversion(true);
  if (!done)
    return std::nullopt;

  auto prev = buffer_.context_word(false);
  std::optional<std::string_view> prev_sv;
  if (prev)
    prev_sv = *prev;

  auto mixed = LayoutDetector::mixed_rescue(done->word);
  SwapDecision d = mixed.is_keep()
                       ? LayoutDetector::decide(done->word, exc_, prev_sv,
                                                after_caret_jump_)
                       : mixed;
  after_caret_jump_ = false;

  return apply_decision(done->word, "", d, true, true);
}

std::optional<ReplaceAction> Engine::manual_convert() {
  if (!settings_.enabled)
    return std::nullopt;
  auto wfc = buffer_.word_for_conversion(false);
  if (!wfc)
    return std::nullopt;

  bool source_cyr = has_cyrillic(wfc->word);
  bool source_lat = has_latin_letter(wfc->word);
  bool to_cyr = true;
  if (source_cyr && !source_lat)
    to_cyr = false;
  else if (source_lat && !source_cyr)
    to_cyr = true;

  undo_.notice_manual_reflip(wfc->word, Keymap::convert(wfc->word, to_cyr));

  SwapDecision d = SwapDecision::convert(to_cyr);
  // Tail is already on screen for a completed word; include it so adapters
  // replace the same span they would delete.
  auto act = apply_decision(wfc->word, wfc->tail, d, false,
                            buffer_.current_word().empty());
  if (act)
    undo_.protect(last_converted_);
  return act;
}

std::optional<ReplaceAction> Engine::maybe_live_fix() {
  if (!settings_.enabled || !settings_.auto_enabled || !settings_.live_fix)
    return std::nullopt;
  if (anti_.is_frozen())
    return std::nullopt;
  const auto &w = buffer_.current_word();
  if (w.empty())
    return std::nullopt;
  if (undo_.should_suppress(w) || undo_.is_session_protected(w))
    return std::nullopt;
  auto d = LayoutDetector::live_decide(w);
  if (d.is_keep())
    return std::nullopt;
  return apply_decision(w, "", d, true, false);
}

std::optional<ReplaceAction> Engine::undo_last() {
  if (last_converted_.empty() || last_original_.empty())
    return std::nullopt;
  std::string expected = last_converted_ + last_tail_;
  std::string insert = last_original_ + last_tail_;
  if (buffer_.current_word().empty())
    buffer_.apply_completed_conversion(last_original_);
  else
    buffer_.apply_conversion(last_original_);
  last_converted_.clear();
  last_original_.clear();
  return ReplaceAction{std::move(expected), std::move(insert), false};
}

} // namespace keyboop
