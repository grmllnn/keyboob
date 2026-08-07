#pragma once
#include <string>
#include <unordered_set>

namespace keyboop {

/// In-memory exception lists (ignored / force / learned). Optional JSON persist later.
class ExceptionStore {
public:
  static ExceptionStore &shared();

  const std::unordered_set<std::string> &ignored() const { return ignored_; }
  const std::unordered_set<std::string> &force_swap() const { return force_swap_; }
  const std::unordered_set<std::string> &learned() const { return learned_; }

  void add_ignored(std::string word);
  void remove_ignored(std::string word);
  void add_force_swap(std::string word);
  void remove_force_swap(std::string word);
  void add_learned(std::string word);
  void clear_learned(std::string word);

  /// Seed default exceptions (вк, тг, vk) once.
  void seed_defaults();

  void set_ignored(std::unordered_set<std::string> s) { ignored_ = std::move(s); }
  void set_force_swap(std::unordered_set<std::string> s) {
    force_swap_ = std::move(s);
  }
  void set_learned(std::unordered_set<std::string> s) { learned_ = std::move(s); }

private:
  std::unordered_set<std::string> ignored_;
  std::unordered_set<std::string> force_swap_;
  std::unordered_set<std::string> learned_;
  bool seeded_ = false;
};

} // namespace keyboop
