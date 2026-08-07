#include "ambiguous_pairs.hpp"
#include "utf8.hpp"

namespace keyboop {
namespace AmbiguousPairs {

const std::vector<Pair> &list() {
  static const std::vector<Pair> pairs = {
      {"vs", "мы"},     {"here", "руку"}, {"herb", "руки"}, {"her", "рук"},
      {"dbl", "вид"},   {"tt", "ее"},     {"cj", "со"},     {"rj", "ко"},
      {"ne", "ту"},     {"dj", "во"},     {"tim", "ешь"},   {"leif", "душа"},
      {"lei", "душ"},   {"verb", "муки"}, {"inert", "штуке"}, {"celt", "суде"},
      {"dyer", "внук"}, {"buh", "игр"},   {"lye", "дну"},   {"neh", "тур"},
      {"vlf", "мда"},   {"abu", "фиг"},   {"cv", "см"},     {"rv", "км"},
      {"ru", "кг"},     {"rd", "кв"},     {"vu", "мг"},     {"uh", "гр"},
      {"in", "шт"},     {"nsc", "тыс"},   {"lng", "дтп"},   {"ids", "швы"},
      {"tv", "ем"},     {"key", "лун"},   {"keys", "луны"}, {"ev", "ум"},
  };
  return pairs;
}

Choice choice(const Pair &p, const ExceptionStore &exc) {
  if (exc.force_swap().count(p.ru))
    return Choice::Ru;
  if (exc.force_swap().count(p.en))
    return Choice::En;
  return Choice::Auto;
}

void choose(const Pair &p, Choice c, ExceptionStore &exc) {
  exc.remove_force_swap(p.en);
  exc.remove_force_swap(p.ru);
  switch (c) {
  case Choice::Ru:
    exc.add_force_swap(p.ru);
    break;
  case Choice::En:
    exc.add_force_swap(p.en);
    break;
  case Choice::Auto:
    break;
  }
}

} // namespace AmbiguousPairs
} // namespace keyboop
