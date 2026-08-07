#pragma once
#include "exception_store.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace keyboop {

class UndoLearner {
public:
  explicit UndoLearner(ExceptionStore &exc = ExceptionStore::shared());

  void notice_auto_conversion(const std::string &original,
                              const std::string &converted);
  void notice_manual_reflip(const std::string &from, const std::string &to);

  /// U2: call after buffer update on each printable char / backspace.
  bool observe(const std::string &current_word);
  bool should_suppress(const std::string &current_word) const;
  void protect(const std::string &word);
  bool is_session_protected(const std::string &typed) const;
  void clear_session();

  void accept_learn(const std::string &word);
  void decline_learn(const std::string &word);

  std::function<void(const std::string &)> on_suggest_learn;
  std::function<void(const std::string &)> on_learned;

  void set_clock(std::function<double()> c) { clock_ = std::move(c); }
  void reset_for_tests();

private:
  enum class Stage { Fresh, Deleting, Retyping };
  struct Candidate {
    std::string original;
    std::string converted;
    double at = 0;
    Stage stage = Stage::Fresh;
  };

  double now() const;
  bool register_strike(const std::string &word);
  Candidate *live_candidate();

  ExceptionStore &exc_;
  std::unordered_set<std::string> declined_;
  std::unordered_set<std::string> session_protected_;
  std::unordered_set<std::string> pending_;
  std::unordered_map<std::string, int> strike_count_;
  std::unordered_map<std::string, double> strike_time_;
  Candidate candidate_{};
  bool has_candidate_ = false;

  static constexpr double kUndoWindow = 4.0;
  static constexpr int kStrikeThreshold = 3;
  static constexpr double kStrikeDecay = 30.0 * 24 * 3600;

  std::function<double()> clock_;
};

} // namespace keyboop
