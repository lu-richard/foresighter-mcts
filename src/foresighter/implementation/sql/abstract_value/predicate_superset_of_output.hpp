/**
* @file predicate_superset_of_output.hpp
* @brief
* @author Rui Dong
* @date 6/29/25
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <iostream>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateSupersetOfOutput {
 public:
  PredicateSupersetOfOutput() = default;

  [[nodiscard]] std::string toString() const {
    return "PredicateSupersetOfOutput";
  }

  bool operator==(const PredicateSupersetOfOutput& other) const { return true; }

  bool operator<(const PredicateSupersetOfOutput& other) const { return false; }

 private:
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateSupersetOfOutput>(other)) {
      return true;
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive();
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateSupersetOfOutput> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateSupersetOfOutput&
          PredicateSupersetOfOutput) const {
    return 0;
  }
};
}  // namespace std
