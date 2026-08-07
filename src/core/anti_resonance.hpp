#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keyboop {

/// Circuit breaker against A↔B auto-conversion oscillation.
class AntiResonanceGuard {
public:
  AntiResonanceGuard(double window = 0.7, int max_flips = 6,
                     double freeze_for = 2.5);

  bool allow(const std::string &word, const std::string &produced);
  bool is_frozen() const;
  void reset_history();
  void reset_for_tests();

  using Clock = std::function<double()>;
  void set_clock(Clock c) { clock_ = std::move(c); }

private:
  double now() const;
  double window_;
  int max_flips_;
  double freeze_for_;
  struct Entry {
    std::string produced;
    double at;
  };
  std::vector<Entry> recent_;
  double frozen_until_ = 0;
  bool did_log_freeze_ = false;
  Clock clock_;
};

} // namespace keyboop
