/**
* @file predicate_constant_type.hpp
* @brief 
* @author Qingyue Wu
* @date 4/25/25
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <string>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateConstantType {
 public:
  PredicateConstantType() = default;

  static int NUMERICType() { return 0; }

  static int TEXTType() { return 1; }

  explicit PredicateConstantType(int type): _type(type) {}

  [[nodiscard]] int getType() const { return _type; }

  [[nodiscard]] std::string toString() const {
    if(_type == NUMERICType()) {
      return "PredicateConstantType(NUMERIC)";
    }
    if(_type == TEXTType()) {
      return "PredicateConstantType(TEXT)";
    }
    return "PredicateConstantType()";
  }

  bool operator==(const PredicateConstantType& other) const {
    return _type == other._type;
  }

 private:
  int _type;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateConstantType>(other)) {
      return _type == std::get<PredicateConstantType>(other).getType();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_type);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateConstantType> {
  size_t operator()(const foresighter::implementation::sql::PredicateConstantType&
                        predicateConstantType) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, predicateConstantType.getType());
    return h;
  }
};
}  // namespace std
