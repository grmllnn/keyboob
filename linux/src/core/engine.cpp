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
Engine::apply_decision(const std::string &word, int delete_count,
                       const std::string &tail, SwapDecision d, bool is_auto,
                       bool completed_only) {
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
  std::string insert = converted + tail;
  if (completed_only)
    buffer_.apply_completed_conversion(converted);
  else
    buffer_.apply_conversion(converted);

  last_original_ = word;
  last_converted_ = converted;
  last_delete_count_ = delete_count;
  last_tail_ = tail;
  last_to_cyr_ = d.to_cyrillic;
  if (is_auto)
    undo_.notice_auto_conversion(word, converted);

  return ReplaceAction{delete_count, insert, d.to_cyrillic, true};
}

std::optional<ReplaceAction>
Engine::on_boundary(std::string_view whitespace, bool completed_only) {
  // Snippet before boundary convert
  if (!buffer_.current_word().empty()) {
    if (auto exp = snip_.expansion_for_typed(buffer_.current_word())) {
      int del = static_cast<int>(utf8_length(buffer_.current_word()));
      buffer_.commit_snippet(*exp, std::string(whitespace));
      return ReplaceAction{del, *exp + std::string(whitespace), false, false};
    }
  }

  auto wfc = buffer_.word_for_conversion(/*completed_only=*/false);
  // First: take current word before boundary moves it
  std::string word;
  int delete_count = 0;
  std::string tail;
  if (!buffer_.current_word().empty()) {
    word = buffer_.current_word();
    delete_count = static_cast<int>(utf8_length(word));
    tail = std::string(whitespace);
  }

  // Mixed rescue on current word
  if (!word.empty()) {
    auto mixed = LayoutDetector::mixed_rescue(word);
    if (!mixed.is_keep() && settings_.auto_enabled && settings_.enabled) {
      buffer_.boundary(whitespace);
      auto prev = buffer_.context_word(/*for_current=*/false);
      return apply_decision(word, delete_count + static_cast<int>(utf8_length(whitespace)),
                            std::string(whitespace), mixed, true, true);
    }
  }

  buffer_.boundary(whitespace);

  if (!settings_.enabled || !settings_.auto_enabled)
    return std::nullopt;

  // After boundary, completed word is last_word
  auto done = buffer_.word_for_conversion(true);
  if (!done)
    return std::nullopt;

  auto prev = buffer_.context_word(false);
  std::optional<std::string_view> prev_sv;
  if (prev)
    prev_sv = *prev;

  // Prefer mixed rescue on completed word
  auto mixed = LayoutDetector::mixed_rescue(done->word);
  SwapDecision d = mixed.is_keep()
                       ? LayoutDetector::decide(done->word, exc_, prev_sv,
                                                after_caret_jump_)
                       : mixed;
  after_caret_jump_ = false;

  // delete_count includes trailing whitespace that was just typed — we re-insert it
  int del = static_cast<int>(utf8_length(done->word) + utf8_length(done->tail));
  return apply_decision(done->word, del, done->tail, d, true, true);
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

  // U1 undo detection: flipping last auto result back
  undo_.notice_manual_reflip(wfc->word,
                             Keymap::convert(wfc->word, to_cyr));

  if (undo_.is_session_protected(wfc->word)) {
    // still allow manual — user asked explicitly
  }

  SwapDecision d = SwapDecision::convert(to_cyr);
  auto act = apply_decision(wfc->word, wfc->delete_count, wfc->tail, d, false,
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
  int del = static_cast<int>(utf8_length(w));
  return apply_decision(w, del, "", d, true, false);
}

std::optional<ReplaceAction> Engine::undo_last() {
  if (last_converted_.empty() || last_original_.empty())
    return std::nullopt;
  // Restore original + tail
  int del = static_cast<int>(utf8_length(last_converted_) +
                             utf8_length(last_tail_));
  std::string insert = last_original_ + last_tail_;
  if (buffer_.current_word().empty())
    buffer_.apply_completed_conversion(last_original_);
  else
    buffer_.apply_conversion(last_original_);
  last_converted_.clear();
  last_original_.clear();
  return ReplaceAction{del, insert, false, false};
}

} // namespace keyboop
