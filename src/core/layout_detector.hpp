#pragma once
#include "exception_store.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace keyboop {

enum class SwapDecisionKind { Keep, Convert };

struct SwapDecision {
  SwapDecisionKind kind = SwapDecisionKind::Keep;
  bool to_cyrillic = false;

  static SwapDecision keep() { return {}; }
  static SwapDecision convert(bool to_cyr) {
    return {SwapDecisionKind::Convert, to_cyr};
  }
  bool is_keep() const { return kind == SwapDecisionKind::Keep; }
  bool operator==(const SwapDecision &o) const {
    return kind == o.kind && to_cyrillic == o.to_cyrillic;
  }
};

enum class ContextHint { None, Cyrillic, Latin };
ContextHint context_hint_of(std::optional<std::string_view> word);

/// Layout plausibility detector (RU/EN).
/// ponytail: word/trigram data is EN–RU; xkb pair may be any Latin↔Cyrillic.
class LayoutDetector {
public:
  static constexpr double margin = 2.0;
  static constexpr double short_en_swap_floor = -10.0;
  static constexpr double live_impossible = -13.0;
  static constexpr double live_margin = 6.0;

  static bool is_layout_letter(uint32_t cp);
  static std::string letter_core(std::string_view raw);

  static SwapDecision live_decide(std::string_view raw);
  static SwapDecision mixed_rescue(std::string_view raw);
  static SwapDecision decide(std::string_view raw, const ExceptionStore &exc,
                             std::optional<std::string_view> prev = std::nullopt,
                             bool after_caret_jump = false);

  static bool is_exception_or_prefix(std::string_view w, bool cyrillic);
};

} // namespace keyboop
