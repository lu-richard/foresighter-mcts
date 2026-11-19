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
class PredicateNumOfColumns {
 public:
  PredicateNumOfColumns() = default;

  explicit PredicateNumOfColumns(int numOfColumn) : _num(numOfColumn) {}

  [[nodiscard]] int getNumOfColumn() const { return _num; }

  [[nodiscard]] std::string toString() const {
    return "PredicateNumOfColumns(" + std::to_string(_num) + ")";
  }

  bool operator==(const PredicateNumOfColumns& other) const {
    return _num == other._num;
  }

 private:
  int _num;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateNumOfColumns>(other)) {
      return _num == std::get<PredicateNumOfColumns>(other).getNumOfColumn();
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
struct hash<foresighter::implementation::sql::PredicateNumOfColumns> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateNumOfColumns&
          predicateNumOfColumn) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, predicateNumOfColumn.getNumOfColumn());
    return h;
  }
};
}  // namespace std
