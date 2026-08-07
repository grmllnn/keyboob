#include "exception_store.hpp"
#include "utf8.hpp"

namespace keyboop {

ExceptionStore &ExceptionStore::shared() {
  static ExceptionStore s;
  return s;
}

void ExceptionStore::seed_defaults() {
  if (seeded_)
    return;
  ignored_.insert("вк");
  ignored_.insert("тг");
  ignored_.insert("vk");
  seeded_ = true;
}

void ExceptionStore::add_ignored(std::string word) {
  word = to_lower_utf8(word);
  if (word.empty())
    return;
  ignored_.insert(word);
  force_swap_.erase(word);
}

void ExceptionStore::remove_ignored(std::string word) {
  ignored_.erase(to_lower_utf8(word));
}

void ExceptionStore::add_force_swap(std::string word) {
  word = to_lower_utf8(word);
  if (word.empty())
    return;
  force_swap_.insert(word);
  ignored_.erase(word);
}

void ExceptionStore::remove_force_swap(std::string word) {
  force_swap_.erase(to_lower_utf8(word));
}

void ExceptionStore::add_learned(std::string word) {
  word = to_lower_utf8(word);
  if (!word.empty())
    learned_.insert(word);
}

void ExceptionStore::clear_learned(std::string word) {
  learned_.erase(to_lower_utf8(word));
}

} // namespace keyboop
