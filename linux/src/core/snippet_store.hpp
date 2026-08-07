#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keyboop {

class SnippetStore {
public:
  static SnippetStore &shared();

  static std::string canonical(std::string_view s);

  std::optional<std::string> expansion_for_typed(std::string_view word) const;
  const std::vector<std::pair<std::string, std::string>> &pairs() const {
    return ordered_;
  }
  void set_all(std::vector<std::pair<std::string, std::string>> pairs);

private:
  void rebuild_index();
  std::vector<std::pair<std::string, std::string>> ordered_;
  std::unordered_map<std::string, std::string> by_canonical_;
};

} // namespace keyboop
