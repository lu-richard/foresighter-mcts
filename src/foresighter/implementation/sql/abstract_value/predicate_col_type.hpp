/**
* @file predicate_col_type.hpp
* @brief 
* @author Rui Dong
* @date 12/4/24
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <string>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateColType {
 public:
  PredicateColType() = default;

  static int NUMERICType() { return 0; }

  static int TEXTType() { return 1; }

  explicit PredicateColType(int colIdx, int colType)
      : _colIdx(colIdx), _colType(colType) {}

  [[nodiscard]] int getColIdx() const { return _colIdx; }

  [[nodiscard]] int getColType() const { return _colType; }

  [[nodiscard]] std::string toString() const {
    auto result = "PredicateColType(" + std::to_string(_colIdx);
    if(_colType == NUMERICType()) result += ", NUMERIC";
    if(_colType == TEXTType()) result += ", TEXT";
    result += ")";
    return result;
  }

  bool operator==(const PredicateColType& other) const {
    return _colIdx == other._colIdx && _colType == other._colType;
  }

 private:
  int _colIdx;
  int _colType;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateColType>(other)) {
      return _colIdx == std::get<PredicateColType>(other).getColIdx() &&
             _colType == std::get<PredicateColType>(other).getColType();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_colIdx, _colType);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateColType> {
  size_t operator()(const foresighter::implementation::sql::PredicateColType&
                        predicateColType) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, predicateColType.getColIdx());
    foresighter::common::hashCombine(h, predicateColType.getColType());
    return h;
  }
};
}  // namespace std
