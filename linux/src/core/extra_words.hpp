#pragma once
#include <string>
#include <unordered_set>

namespace keyboop {

/// Port of Sources/Keyboop/ExtraWords.swift — curated sets.
namespace ExtraWords {
const std::unordered_set<std::string> &ru_abbr();
const std::unordered_set<std::string> &ru_short();
const std::unordered_set<std::string> &force_ru_amb();
const std::unordered_set<std::string> &force_en_amb();
const std::unordered_set<std::string> &default_keep();
const std::unordered_set<std::string> &hyphen_terms();
const std::unordered_set<std::string> &ru_label_classifiers();
const std::unordered_set<std::string> &en_keep_short();
const std::unordered_set<std::string> &label_classifiers();
const std::unordered_set<std::string> &ru();
const std::unordered_set<std::string> &en();
} // namespace ExtraWords

} // namespace keyboop
