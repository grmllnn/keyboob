#include "undo_learner.hpp"
#include "utf8.hpp"

#include <chrono>

namespace keyboop {

UndoLearner::UndoLearner(ExceptionStore &exc) : exc_(exc) {}

double UndoLearner::now() const {
  if (clock_)
    return clock_();
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

UndoLearner::Candidate *UndoLearner::live_candidate() {
  if (!has_candidate_)
    return nullptr;
  if (now() - candidate_.at > kUndoWindow) {
    has_candidate_ = false;
    return nullptr;
  }
  return &candidate_;
}

void UndoLearner::notice_auto_conversion(const std::string &original,
                                         const std::string &converted) {
  auto o = to_lower_utf8(original);
  auto c = to_lower_utf8(converted);
  if (o == c || o.empty() || c.empty()) {
    has_candidate_ = false;
    return;
  }
  if (exc_.learned().count(o) || declined_.count(o)) {
    has_candidate_ = false;
    return;
  }
  candidate_ = {o, c, now(), Stage::Fresh};
  has_candidate_ = true;
}

bool UndoLearner::register_strike(const std::string &word) {
  auto w = to_lower_utf8(word);
  double t = now();
  if (auto it = strike_time_.find(w); it != strike_time_.end()) {
    if (t - it->second > kStrikeDecay)
      strike_count_[w] = 0;
  }
  int c = ++strike_count_[w];
  strike_time_[w] = t;
  return c >= kStrikeThreshold;
}

void UndoLearner::notice_manual_reflip(const std::string &from,
                                       const std::string &to) {
  auto *cand = live_candidate();
  if (!cand)
    return;
  auto f = to_lower_utf8(from);
  auto t = to_lower_utf8(to);
  if (f != cand->converted || t != cand->original)
    return;
  has_candidate_ = false;
  session_protected_.insert(cand->original);
  if (declined_.count(cand->original) || pending_.count(cand->original))
    return;
  if (register_strike(cand->original)) {
    pending_.insert(cand->original);
    if (on_suggest_learn)
      on_suggest_learn(cand->original);
  }
}

bool UndoLearner::observe(const std::string &current_word) {
  auto *cand = live_candidate();
  if (!cand)
    return false;
  auto cur = to_lower_utf8(current_word);
  switch (cand->stage) {
  case Stage::Fresh:
  case Stage::Deleting:
    if (cur.empty()) {
      cand->stage = Stage::Retyping;
    } else if (cand->converted.rfind(cur, 0) == 0 &&
               cur.size() < cand->converted.size()) {
      cand->stage = Stage::Deleting;
    } else {
      has_candidate_ = false;
    }
    return false;
  case Stage::Retyping:
    if (cur == cand->original) {
      session_protected_.insert(cand->original);
      has_candidate_ = false;
      if (!declined_.count(cand->original) && !pending_.count(cand->original) &&
          register_strike(cand->original)) {
        pending_.insert(cand->original);
        if (on_suggest_learn)
          on_suggest_learn(cand->original);
      }
      return true;
    }
    if (cand->original.rfind(cur, 0) == 0)
      return false;
    has_candidate_ = false;
    return false;
  }
  return false;
}

bool UndoLearner::should_suppress(const std::string &current_word) const {
  if (!has_candidate_ || candidate_.stage != Stage::Retyping)
    return false;
  if (now() - candidate_.at > kUndoWindow)
    return false;
  auto cur = to_lower_utf8(current_word);
  return !cur.empty() && candidate_.original.rfind(cur, 0) == 0 &&
         cur != candidate_.original;
}

void UndoLearner::protect(const std::string &word) {
  auto w = to_lower_utf8(word);
  if (!w.empty())
    session_protected_.insert(w);
}

bool UndoLearner::is_session_protected(const std::string &typed) const {
  return session_protected_.count(to_lower_utf8(typed)) != 0;
}

void UndoLearner::clear_session() {
  has_candidate_ = false;
  session_protected_.clear();
}

void UndoLearner::accept_learn(const std::string &word) {
  auto w = to_lower_utf8(word);
  pending_.erase(w);
  exc_.add_learned(w);
  strike_count_.erase(w);
  strike_time_.erase(w);
  if (on_learned)
    on_learned(w);
}

void UndoLearner::decline_learn(const std::string &word) {
  auto w = to_lower_utf8(word);
  pending_.erase(w);
  declined_.insert(w);
}

void UndoLearner::reset_for_tests() {
  declined_.clear();
  session_protected_.clear();
  pending_.clear();
  strike_count_.clear();
  strike_time_.clear();
  has_candidate_ = false;
}

} // namespace keyboop
