#include "layout_data.hpp"
#include "extra_words.hpp"
#include "utf8.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
#include <nlohmann/json.hpp>

namespace keyboop {

LayoutData &LayoutData::shared() {
  static LayoutData d;
  return d;
}

bool LayoutData::load(const std::string &data_dir) {
  auto load_dict = [&](const char *name,
                       std::unordered_map<std::string, double> &out) -> bool {
    std::ifstream in(data_dir + "/" + name + ".json");
    if (!in)
      return false;
    nlohmann::json j;
    in >> j;
    out.clear();
    for (auto it = j.begin(); it != j.end(); ++it)
      out[it.key()] = it.value().get<double>();
    return true;
  };
  auto load_set = [&](const char *name,
                      std::unordered_set<std::string> &out) -> bool {
    std::ifstream in(data_dir + "/" + name + ".json");
    if (!in)
      return false;
    nlohmann::json j;
    in >> j;
    out.clear();
    for (auto &v : j)
      out.insert(v.get<std::string>());
    return true;
  };

  if (!load_dict("trigrams_ru", trigrams_ru_) ||
      !load_dict("trigrams_en", trigrams_en_) ||
      !load_set("words_ru", words_ru_) || !load_set("words_en", words_en_)) {
    std::cerr << "Keyboop: failed to load LayoutData from " << data_dir << "\n";
    loaded_ = false;
    return false;
  }

  for (auto &w : ExtraWords::ru())
    words_ru_.insert(w);
  for (auto &w : ExtraWords::ru_abbr())
    words_ru_.insert(w);
  for (auto &w : ExtraWords::ru_short())
    words_ru_.insert(w);
  for (auto &w : ExtraWords::en())
    words_en_.insert(w);

  loaded_ = !trigrams_ru_.empty() && !words_en_.empty();
  std::cerr << "Keyboop: LayoutData loaded=" << loaded_
            << " ru-tri=" << trigrams_ru_.size()
            << " en-tri=" << trigrams_en_.size()
            << " ru-w=" << words_ru_.size() << " en-w=" << words_en_.size()
            << "\n";
  return loaded_;
}

double LayoutData::plausibility(std::string_view word, bool cyrillic) const {
  const auto &table = cyrillic ? trigrams_ru_ : trigrams_en_;
  std::string padded;
  padded.reserve(word.size() + 2);
  padded.push_back(' ');
  padded += to_lower_utf8(word);
  padded.push_back(' ');

  std::vector<std::string_view> chars;
  chars.reserve(padded.size());
  Utf8Iter it(padded);
  while (it.ok()) {
    const unsigned char *start = it.p;
    it.next();
    chars.emplace_back(reinterpret_cast<const char *>(start),
                       static_cast<size_t>(it.p - start));
  }
  if (chars.size() < 3)
    return -std::numeric_limits<double>::infinity();

  double sum = 0;
  std::string tri;
  tri.reserve(16);
  for (size_t i = 0; i + 2 < chars.size(); ++i) {
    tri.assign(chars[i]);
    tri.append(chars[i + 1]);
    tri.append(chars[i + 2]);
    auto f = table.find(tri);
    sum += (f != table.end()) ? f->second : -20.0;
  }
  return sum / static_cast<double>(chars.size() - 2);
}

} // namespace keyboop
