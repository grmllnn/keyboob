#pragma once
#include "exception_store.hpp"
#include <string>
#include <utility>
#include <vector>

namespace keyboop {

/// Port of Sources/Keyboop/AmbiguousPairs.swift
namespace AmbiguousPairs {
struct Pair {
  std::string en;
  std::string ru;
};

enum class Choice { En, Auto, Ru };

const std::vector<Pair> &list();
Choice choice(const Pair &p, const ExceptionStore &exc);
void choose(const Pair &p, Choice c, ExceptionStore &exc);
} // namespace AmbiguousPairs

} // namespace keyboop
