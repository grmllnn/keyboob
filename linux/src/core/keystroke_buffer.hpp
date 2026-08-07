#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace keyboop {

struct SessionWord {
  std::string word;
  std::string tail;
};

/// Port of Sources/Keyboop/KeystrokeBuffer.swift
class KeystrokeBuffer {
public:
  void append(std::string_view s);
  void backspace();
  void boundary(std::string_view whitespace);
  void clear();
  void soft_context_reset();
  void invalidate_group_history();

  const std::string &current_word() const { return current_word_; }
  const std::string &last_word() const { return last_word_; }
  const std::string &last_tail() const { return last_tail_; }
  const std::vector<SessionWord> &session_words() const {
    return session_words_;
  }

  struct WordForConversion {
    std::string word;
    int delete_count = 0;
    std::string tail;
  };
  std::optional<WordForConversion>
  word_for_conversion(bool completed_only = false) const;

  struct GroupForConversion {
    std::vector<SessionWord> words;
    int delete_count = 0;
  };
  std::optional<GroupForConversion> group_for_conversion() const;

  void apply_conversion(const std::string &converted);
  void apply_completed_conversion(const std::string &converted);
  void commit_snippet(const std::string &expansion,
                      const std::string &whitespace);

  std::optional<std::string> context_word(bool for_current) const;

  /// Injectable clock for tests (seconds since epoch / monotonic).
  using Clock = std::function<double()>;
  void set_clock(Clock c) { clock_ = std::move(c); }

private:
  double now() const;
  std::string current_word_;
  std::string last_word_;
  std::string last_tail_;
  std::vector<SessionWord> session_words_;
  double last_activity_ = 0;
  static constexpr int kGroupMaxChars = 200;
  static constexpr double kGroupMaxIdle = 8.0;
  Clock clock_;
};

} // namespace keyboop
