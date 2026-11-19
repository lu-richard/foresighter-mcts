/**
* @file predicate_num_of_column.hpp
* @brief 
* @author Rui Dong
* @date 4/22/24
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateTotalNColumns {
 public:
  PredicateTotalNColumns() = default;

  explicit PredicateTotalNColumns(int numOfColumns) : _num(numOfColumns) {}

  [[nodiscard]] int getNumOfColumns() const { return _num; }

  [[nodiscard]] std::string toString() const {
    return "PredicateTotalNColumns(" + std::to_string(_num) + ")";
  }

  bool operator==(const PredicateTotalNColumns& other) const {
    return _num == other._num;
  }

 private:
  int _num;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateTotalNColumns>(other)) {
      return _num == std::get<PredicateTotalNColumns>(other).getNumOfColumns();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_num);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateTotalNColumns> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateTotalNColumns&
          predicateUsedNColumns) const {
    size_t h = 0;
    foresighter::common::hashCombine(h,
                                     predicateUsedNColumns.getNumOfColumns());
    return h;
  }
};
}  // namespace std
