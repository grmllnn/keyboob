#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace keyboop {

class LayoutData {
public:
  static LayoutData &shared();

  /// Load from KEYBOOP_DATA_DIR or given path. Returns false on failure.
  bool load(const std::string &data_dir);

  bool is_loaded() const { return loaded_; }

  double plausibility(std::string_view word, bool cyrillic) const;

  const std::unordered_set<std::string> &words_ru() const { return words_ru_; }
  const std::unordered_set<std::string> &words_en() const { return words_en_; }

  bool has_word_ru(const std::string &w) const {
    return words_ru_.count(w) != 0;
  }
  bool has_word_en(const std::string &w) const {
    return words_en_.count(w) != 0;
  }

private:
  LayoutData() = default;
  std::unordered_map<std::string, double> trigrams_ru_;
  std::unordered_map<std::string, double> trigrams_en_;
  std::unordered_set<std::string> words_ru_;
  std::unordered_set<std::string> words_en_;
  bool loaded_ = false;
};

} // namespace keyboop
