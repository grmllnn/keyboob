#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace keyboop {

class LayoutData {
public:
  static LayoutData &shared();

  /// Load from KEYBOOP_DATA_DIR or given path. Returns false on failure.
  bool load(const std::string &data_dir);

  /// Env, then install dir, then compile-time source data dir.
  bool load_from_search_path();

  bool is_loaded() const { return loaded_; }

  double plausibility(std::string_view word, bool cyrillic) const;

  const std::unordered_set<std::string> &words_ru() const { return words_ru_; }
  const std::unordered_set<std::string> &words_en() const { return words_en_; }

  bool has_word_ru(const std::string &w) const {
    if (words_ru_.count(w) != 0)
      return true;
    // Normalize ё -> е and check
    std::string e_form;
    e_form.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
      unsigned char c1 = static_cast<unsigned char>(w[i]);
      if (c1 == 0xd1 && i + 1 < w.size() && static_cast<unsigned char>(w[i + 1]) == 0x91) { // ё
        e_form += "е";
        ++i;
      } else if (c1 == 0xd0 && i + 1 < w.size() && static_cast<unsigned char>(w[i + 1]) == 0x81) { // Ё
        e_form += "Е";
        ++i;
      } else {
        e_form.push_back(w[i]);
      }
    }
    return words_ru_.count(e_form) != 0;
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

std::vector<std::string> layout_data_search_paths();

} // namespace keyboop
