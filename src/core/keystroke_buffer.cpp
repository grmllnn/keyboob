#include "keystroke_buffer.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <chrono>

namespace keyboop {

double KeystrokeBuffer::now() const {
  if (clock_)
    return clock_();
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void KeystrokeBuffer::append(std::string_view s) {
  current_word_.append(s);
  last_activity_ = now();
}

void KeystrokeBuffer::backspace() {
  last_activity_ = now();
  if (!current_word_.empty()) {
    utf8_pop_back(current_word_);
    if (current_word_.empty()) {
      last_word_.clear();
      last_tail_.clear();
      session_words_.clear();
    }
  } else if (!last_tail_.empty()) {
    utf8_pop_back(last_tail_);
    if (!session_words_.empty())
      session_words_.back().tail = last_tail_;
  } else if (!last_word_.empty()) {
    current_word_ = last_word_;
    if (!session_words_.empty())
      session_words_.pop_back();
    last_word_ = session_words_.empty() ? "" : session_words_.back().word;
    last_tail_ = session_words_.empty() ? "" : session_words_.back().tail;
    utf8_pop_back(current_word_);
    if (current_word_.empty()) {
      last_word_.clear();
      last_tail_.clear();
      session_words_.clear();
    }
  } else {
    clear();
  }
}

void KeystrokeBuffer::boundary(std::string_view whitespace) {
  last_activity_ = now();
  if (!current_word_.empty()) {
    session_words_.push_back({current_word_, std::string(whitespace)});
    last_word_ = current_word_;
    last_tail_ = whitespace;
    current_word_.clear();
  } else if (!last_word_.empty()) {
    last_tail_.append(whitespace);
    if (!session_words_.empty())
      session_words_.back().tail += whitespace;
  }
}

void KeystrokeBuffer::clear() {
  current_word_.clear();
  last_word_.clear();
  last_tail_.clear();
  session_words_.clear();
}

void KeystrokeBuffer::soft_context_reset() {
  last_word_.clear();
  last_tail_.clear();
  session_words_.clear();
}

void KeystrokeBuffer::invalidate_group_history() { session_words_.clear(); }

std::optional<KeystrokeBuffer::WordForConversion>
KeystrokeBuffer::word_for_conversion(bool completed_only) const {
  if (completed_only) {
    if (last_word_.empty())
      return std::nullopt;
    return WordForConversion{last_word_, last_tail_ + current_word_};
  }
  if (!current_word_.empty())
    return WordForConversion{current_word_, ""};
  if (!last_word_.empty())
    return WordForConversion{last_word_, last_tail_};
  return std::nullopt;
}

std::optional<KeystrokeBuffer::GroupForConversion>
KeystrokeBuffer::group_for_conversion() const {
  if (now() - last_activity_ > kGroupMaxIdle)
    return std::nullopt;
  auto words = session_words_;
  if (!current_word_.empty())
    words.push_back({current_word_, ""});
  if (words.size() < 2)
    return std::nullopt;
  int total = 0;
  for (auto &wt : words) {
    if (wt.tail.find('\n') != std::string::npos ||
        wt.tail.find('\t') != std::string::npos)
      return std::nullopt;
    total += static_cast<int>(utf8_length(wt.word) + utf8_length(wt.tail));
  }
  if (total <= 0 || total > kGroupMaxChars)
    return std::nullopt;
  return GroupForConversion{std::move(words)};
}

void KeystrokeBuffer::apply_conversion(const std::string &converted) {
  if (!current_word_.empty()) {
    current_word_ = converted;
  } else if (!last_word_.empty()) {
    last_word_ = converted;
    if (!session_words_.empty())
      session_words_.back().word = converted;
  }
}

void KeystrokeBuffer::apply_completed_conversion(const std::string &converted) {
  if (last_word_.empty())
    return;
  last_word_ = converted;
  if (!session_words_.empty())
    session_words_.back().word = converted;
}

void KeystrokeBuffer::commit_snippet(const std::string &expansion,
                                     const std::string &whitespace) {
  current_word_ = expansion;
  boundary(whitespace);
}

std::optional<std::string>
KeystrokeBuffer::context_word(bool for_current) const {
  if (for_current) {
    if (session_words_.empty())
      return std::nullopt;
    return session_words_.back().word;
  }
  if (session_words_.size() < 2)
    return std::nullopt;
  return session_words_[session_words_.size() - 2].word;
}

} // namespace keyboop
