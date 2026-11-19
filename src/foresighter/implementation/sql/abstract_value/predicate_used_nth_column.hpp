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
class PredicateUsedNthColumn {
 public:
  PredicateUsedNthColumn() = default;

  explicit PredicateUsedNthColumn(int nthColumn) : _idx(nthColumn) {}

  [[nodiscard]] int getColumnIdx() const { return _idx; }

  [[nodiscard]] std::string toString() const {
    return "PredicateUsedNthColumn(" + std::to_string(_idx) + ")";
  }

  bool operator==(const PredicateUsedNthColumn& other) const {
    return _idx == other._idx;
  }

 private:
  int _idx;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateUsedNthColumn>(other)) {
      return _idx == std::get<PredicateUsedNthColumn>(other).getColumnIdx();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_idx);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateUsedNthColumn> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateUsedNthColumn&
          predicateUsedNthColumn) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, predicateUsedNthColumn.getColumnIdx());
    return h;
  }
};
}  // namespace std
