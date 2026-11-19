/**
* @file predicate_select_nth_column.hpp
* @brief 
* @author Qingyue Wu
* @date 4/16/25
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <string>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateSelectIntervalColumns {
 public:
  PredicateSelectIntervalColumns() = default;

  explicit PredicateSelectIntervalColumns(int idxL, int idxR, int aggType = -1)
      : _idxL(idxL), _idxR(idxR), _aggType(aggType) {}

  // used for filter condition
  static int NUMERICType() { return 0; }

  static int TEXTType() { return 1; }

  // used for aggregation function
  static int MINMAXType() { return 2; }

  static int COUNTType() { return 3; }

  static int SUMAVGType() { return 4; }

  [[nodiscard]] int getIntervalL() const { return _idxL; }

  [[nodiscard]] int getIntervalR() const { return _idxR; }

  [[nodiscard]] int getAggType() const { return _aggType; }

  [[nodiscard]] std::string toString() const {
    auto result = "PredicateSelectIntervalColumns([" + std::to_string(_idxL) +
                  ", " + std::to_string(_idxR) + "]";
    if (_aggType == MINMAXType())
      result += ", Min/Max";
    if (_aggType == COUNTType())
      result += ", Count/CountDistinct";
    if (_aggType == SUMAVGType())
      result += ", Sum/Avg";
    if (_aggType == NUMERICType())
      result += ", NUMERIC";
    if (_aggType == TEXTType())
      result += ", TEXT";
    result += ")";
    return result;
  }

  bool operator==(const PredicateSelectIntervalColumns& other) const {
    return _idxL == other._idxL && _idxR == other._idxR &&
           _aggType == other._aggType;
  }

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateSelectIntervalColumns>(other)) {
      return _idxL >= std::get<PredicateSelectIntervalColumns>(other)
                          .getIntervalL() &&
             _idxR <= std::get<PredicateSelectIntervalColumns>(other)
                          .getIntervalR() &&
             _aggType ==
                 std::get<PredicateSelectIntervalColumns>(other).getAggType();
    }
    return false;
  }

 private:
  int _idxL, _idxR;
  int _aggType;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_idxL, _idxR, _aggType);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateSelectIntervalColumns> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateSelectIntervalColumns&
          predicateSelectIntervalColumn) const {
    size_t h = 0;
    foresighter::common::hashCombine(
        h, predicateSelectIntervalColumn.getIntervalL());
    foresighter::common::hashCombine(
        h, predicateSelectIntervalColumn.getIntervalR());
    foresighter::common::hashCombine(
        h, predicateSelectIntervalColumn.getAggType());
    return h;
  }
};
}  // namespace std
