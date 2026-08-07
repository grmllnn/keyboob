#include "anti_resonance.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace keyboop {

AntiResonanceGuard::AntiResonanceGuard(double window, int max_flips,
                                       double freeze_for)
    : window_(window), max_flips_(max_flips), freeze_for_(freeze_for) {}

double AntiResonanceGuard::now() const {
  if (clock_)
    return clock_();
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

bool AntiResonanceGuard::allow(const std::string &word,
                               const std::string &produced) {
  double t = now();
  if (t < frozen_until_)
    return false;

  recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                               [&](const Entry &e) {
                                 return t - e.at > window_;
                               }),
                recent_.end());

  bool oscillation = false;
  for (auto &e : recent_) {
    if (e.produced == word) {
      oscillation = true;
      break;
    }
  }
  recent_.push_back({produced, t});

  if (oscillation ||
      static_cast<int>(recent_.size()) > max_flips_) {
    frozen_until_ = t + freeze_for_;
    recent_.clear();
    if (!did_log_freeze_) {
      did_log_freeze_ = true;
      std::cerr << "anti-resonance: freeze "
                << (oscillation ? "oscillation" : "storm") << "\n";
    }
    return false;
  }
  return true;
}

bool AntiResonanceGuard::is_frozen() const { return now() < frozen_until_; }

void AntiResonanceGuard::reset_history() { recent_.clear(); }

void AntiResonanceGuard::reset_for_tests() {
  recent_.clear();
  frozen_until_ = 0;
  did_log_freeze_ = false;
}

} // namespace keyboop
