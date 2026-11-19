/**
* @file predicate_concrete_row_num_range.hpp
* @brief
* @author Rui Dong
* @date 6/29/25
*/

#pragma once

#include <cassert>
#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>
#include <foresighter/common/hash.hpp>
#include <string>
#include <utility>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateConcreteRowNumRange {
 public:
  PredicateConcreteRowNumRange() = default;

  explicit PredicateConcreteRowNumRange(int lowerBound, int upperBound)
      : _lowerBound(lowerBound), _upperBound(upperBound) {}

  [[nodiscard]] int getLowerBound() const { return _lowerBound; }

  [[nodiscard]] int getUpperBound() const { return _upperBound; }

  [[nodiscard]] std::string toString() const {
    return "PredicateConcreteRowNumRange(" + std::to_string(_lowerBound) +
           ", " + std::to_string(_upperBound) + ")";
  }

  bool operator==(const PredicateConcreteRowNumRange& other) const {
    return _lowerBound == other._lowerBound && _upperBound == other._upperBound;
  }

  PredicateConcreteRowNumRange operator*(
      const PredicateConcreteRowNumRange& other) const {
    return PredicateConcreteRowNumRange{_lowerBound * other._lowerBound,
                                        _upperBound * other._upperBound};
  }

 private:
  int _lowerBound;
  int _upperBound;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateConcreteRowNumRange>(other)) {
      return _lowerBound >= std::get<PredicateConcreteRowNumRange>(other)
                                .getLowerBound() &&
             _upperBound <=
                 std::get<PredicateConcreteRowNumRange>(other).getUpperBound();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_lowerBound, _upperBound);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {

template <>
struct hash<foresighter::implementation::sql::PredicateConcreteRowNumRange> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateConcreteRowNumRange&
          PredicateConcreteRowNumRange) const {
    size_t h = 0;
    foresighter::common::hashCombine(
        h, PredicateConcreteRowNumRange.getLowerBound());
    foresighter::common::hashCombine(
        h, PredicateConcreteRowNumRange.getUpperBound());
    return h;
  }
};
}  // namespace std
