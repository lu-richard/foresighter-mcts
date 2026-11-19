#pragma once
/**
 * @file lattice.hpp
 * @brief Lattice operations (meet, join, top, bottom, implication) on abstract values.
 *
 * Implements the partial order and meet/join for the abstract domain used in presynthesis.
 * Handles conjunctions of predicates: meet computes the greatest lower bound, and
 * implies checks the entailment relation (used for slicing and output-state matching).
 */
#include <algorithm>
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/variant.hpp>
#include <variant>
#include "value.hpp"

namespace foresighter::implementation::basics {

template <class ValueType>
class Lattice {
 public:
  using ConjunctionType = ValueType::ConjunctionType;
  using ConjunctionVariantType = ConjunctionType::VariantType;

  static ValueType join(const ValueType& lhs, const ValueType& rhs) {
    throw common::NotImplementedException();
  }

  static ValueType meet(const ValueType& lhs, const ValueType& rhs) {

    auto& lhsValue = lhs.getValue();
    auto& rhsValue = rhs.getValue();
    if (isBottomValue(lhsValue) || isBottomValue(rhsValue))
      return bottom();

    if (isTopValue(lhsValue))
      return rhs;
    if (isTopValue(rhsValue))
      return lhs;

    if (implies(lhs, rhs))
      return lhs;
    if (implies(rhs, lhs))
      return rhs;

    if (lhs == rhs)
      return lhs;

    if (isConjunctionValue(lhsValue) || isConjunctionValue(rhsValue)) {
      return dealWithConjunction(lhs, rhs);
    }

    return createNewConjunction(lhsValue, rhsValue);
  }

  static ValueType bottom() {
    typename ValueType::BottomType bottom;
    return (ValueType)bottom;
  }

  static ValueType top() {
    typename ValueType::TopType top;
    return (ValueType)top;
  }

  static bool isBottom(const ValueType& value) {
    return std::holds_alternative<typename ValueType::BottomType>(
        value.getValue());
  }

  static bool isTop(const ValueType& value) {
    return std::holds_alternative<typename ValueType::TopType>(
        value.getValue());
  }

  static bool implies(const ValueType& lhs, const ValueType& rhs) {
    auto& lhsValue = lhs.getValue();
    auto& rhsValue = rhs.getValue();

    if (isBottomValue(lhsValue) || isTopValue(rhsValue))
      return true;

    bool isConjunctionL = isConjunctionValue(lhsValue);
    bool isConjunctionR = isConjunctionValue(rhsValue);
    if (!isConjunctionL && !isConjunctionR) {
      return check1To1(lhsValue, rhsValue);
    }
    else if (isConjunctionL && !isConjunctionR) {
      return checknTo1(lhsValue, rhsValue);
    }
    else if (!isConjunctionL && isConjunctionR) {
      return check1Ton(lhsValue, rhsValue);
    }
    else {
      return checknTon(lhsValue, rhsValue);
    }
    // should never reach here
    return false;
  }

 protected:
  static bool isTopValue(const auto& value) {
    return std::holds_alternative<typename ValueType::TopType>(value);
  }

  static bool isBottomValue(const auto& value) {
    return std::holds_alternative<typename ValueType::BottomType>(value);
  }

  static bool isConjunctionValue(const auto& value) {
    return std::holds_alternative<ConjunctionType>(value);
  }

  static bool areNonConjunctionTypes(const auto& lhs, const auto& rhs) {
    return !std::holds_alternative<ConjunctionType>(lhs) &&
           !std::holds_alternative<ConjunctionType>(rhs);
  }

  static bool isConjunctionImplyingNonConjunction(const auto& lhs,
                                                  const auto& rhs) {
    return std::holds_alternative<ConjunctionType>(lhs) &&
           !std::holds_alternative<ConjunctionType>(rhs);
  }

  static bool areBothConjunctions(const auto& lhs, const auto& rhs) {
    return std::holds_alternative<ConjunctionType>(lhs) &&
           std::holds_alternative<ConjunctionType>(rhs);
  }

  static bool check1To1(const auto& lhsValue, const auto& rhsValue) {
    return std::visit(
               [&rhsValue](const auto& arg) -> bool {
                 return arg.implies(rhsValue);
               },
               lhsValue) ||
           std::visit(
               [&lhsValue](const auto& arg) -> bool {
                 return arg.impliedBy(lhsValue);
               },
               rhsValue);
  }

  static bool check1Ton(const auto& value, const auto& conjValue) {
    auto& conjunction = std::get<ConjunctionType>(conjValue);
    return std::all_of(conjunction.getConjunction().begin(),
                       conjunction.getConjunction().end(),
                       [&value](const auto& p) { return check1To1(value, p); });
  }

  static bool checknTo1(const auto& conjValue, const auto& value) {
    auto& conjunction = std::get<ConjunctionType>(conjValue);
    return std::any_of(conjunction.getConjunction().begin(),
                       conjunction.getConjunction().end(),
                       [&value](const auto& p) { return check1To1(p, value); });
  }

  static bool checknTon(const auto& lhsValue, const auto& rhsValue) {
    auto& rhsConjunction = std::get<ConjunctionType>(rhsValue);
    return std::all_of(
        rhsConjunction.getConjunction().begin(),
        rhsConjunction.getConjunction().end(),
        [&lhsValue](const auto& p) { return checknTo1(lhsValue, p); });
  }

  static ValueType createNewConjunction(const auto& lhsValue,
                                        const auto& rhsValue) {
    std::vector<ConjunctionVariantType> newPredicates;
    newPredicates.emplace_back(common::variant_cast(lhsValue));
    newPredicates.emplace_back(common::variant_cast(rhsValue));
    return ValueType{std::in_place_type<ConjunctionType>, newPredicates};
  }

  static ValueType dealWithConjunction(const ValueType& lhs,
                                       const ValueType& rhs) {
    auto& lhsValue = lhs.getValue();
    auto& rhsValue = rhs.getValue();

    if (!std::holds_alternative<ConjunctionType>(lhsValue)) {
      return dealWithConjunction(rhs, lhs);
    }

    if (!std::holds_alternative<ConjunctionType>(rhsValue)) {
      return handleConjunctionWithNonConjunction(lhs, rhsValue);
    }

    return mergeConjunctions(lhsValue, rhsValue);
  }

  static ValueType handleConjunctionWithNonConjunction(const ValueType& lhs,
                                                       const auto& rhsValue) {
    auto& lhsConjunction = std::get<ConjunctionType>(lhs.getValue());
    ConjunctionVariantType rhsPredicate = common::variant_cast(rhsValue);

    if (lhsConjunction.getConjunction().contains(rhsPredicate)) {
      return lhs;
    }

    std::vector<ConjunctionVariantType> newPredicates{
        lhsConjunction.getConjunction().begin(),
        lhsConjunction.getConjunction().end()};
    newPredicates.emplace_back(rhsPredicate);
    return ValueType{std::in_place_type<ConjunctionType>, newPredicates};
  }

  static ValueType mergeConjunctions(const auto& lhsValue,
                                     const auto& rhsValue) {
    auto& lhsConjunction = std::get<ConjunctionType>(lhsValue);
    auto& rhsConjunction = std::get<ConjunctionType>(rhsValue);

    std::vector<ConjunctionVariantType> newPredicates;
    newPredicates.reserve(lhsConjunction.getConjunction().size() +
                          rhsConjunction.getConjunction().size());

    newPredicates.insert(newPredicates.end(),
                         lhsConjunction.getConjunction().begin(),
                         lhsConjunction.getConjunction().end());
    newPredicates.insert(newPredicates.end(),
                         rhsConjunction.getConjunction().begin(),
                         rhsConjunction.getConjunction().end());

    return ValueType{std::in_place_type<ConjunctionType>, newPredicates};
  }
};
}  // namespace foresighter::implementation::basics
