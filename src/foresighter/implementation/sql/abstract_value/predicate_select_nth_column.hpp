/**
* @file predicate_select_nth_column.hpp
* @brief 
* @author Qingyue Wu
* @date 4/16/25
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateSelectNthColumn {
 public:
  PredicateSelectNthColumn() = default;

  explicit PredicateSelectNthColumn(int nthColumn, int aggType = -1) :
      _idx(nthColumn), _aggType(aggType) {}

  // used for filter condition
  static int NUMERICType() { return 0; }

  static int TEXTType() { return 1; }

  // used for aggregation function
  static int MINMAXType() { return 2; }

  static int COUNTType() { return 3; }

  static int SUMAVGType() { return 4; }

  [[nodiscard]] int getColumnIdx() const { return _idx; }

  [[nodiscard]] int getAggType() const { return _aggType; }

  [[nodiscard]] std::string toString() const {
    auto result = "PredicateSelectNthColumn(" + std::to_string(_idx);
    if(_aggType == MINMAXType()) result += ", Min/Max";
    if(_aggType == COUNTType()) result += ", Count/CountDistinct";
    if(_aggType == SUMAVGType()) result += ", Sum/Avg";
    if(_aggType == NUMERICType()) result += ", NUMERIC";
    if(_aggType == TEXTType()) result += ", TEXT";
    result += ")";
    return result;
  }

  bool operator==(const PredicateSelectNthColumn& other) const {
    return _idx == other._idx && _aggType == other._aggType;
  }

 private:
  int _idx;
  int _aggType;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateSelectNthColumn>(other)) {
      return _idx == std::get<PredicateSelectNthColumn>(other).getColumnIdx() &&
             _aggType == std::get<PredicateSelectNthColumn>(other).getAggType();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_idx, _aggType);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateSelectNthColumn> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateSelectNthColumn&
          predicateSelectNthColumn) const {
    size_t h = 0;
    foresighter::common::hashCombine(h,
                                     predicateSelectNthColumn.getColumnIdx());
    foresighter::common::hashCombine(h,
                                     predicateSelectNthColumn.getAggType());
    return h;
  }
};
}  // namespace std
