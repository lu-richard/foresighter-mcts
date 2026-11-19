/**
* @file predicate_num_of_column.hpp
* @brief 
* @author Rui Dong
* @date 4/22/24
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <utility>
#include <variant>
#include "foresighter/implementation/sql/abstract_value/predicate_select_interval_columns.hpp"
#include "predicate_num_of_columns.hpp"
#include "predicate_select_nth_column.hpp"
#include "predicate_total_n_columns.hpp"

namespace foresighter::implementation::sql {
class PredicateOrderedTuple {
 public:
  using VariantType =
      std::variant<PredicateNumOfColumns, PredicateSelectNthColumn,
                   PredicateTotalNColumns, PredicateSelectIntervalColumns>;
  PredicateOrderedTuple() = default;

  explicit PredicateOrderedTuple(std::vector<VariantType> values)
      : _values(std::move(values)) {}

  [[nodiscard]] int getNumOfValues() const { return (int)_values.size(); }

  [[nodiscard]] std::vector<VariantType> getValues() const { return _values; }

  [[nodiscard]] std::string toString() const {
    std::string result = "PredicateOrderedTuple(";
    for (auto value : _values) {
      result += std::visit([](const auto& value) { return value.toString(); },
                           value) +
                ",";
    }
    return result + ")";
  }

  bool operator==(const PredicateOrderedTuple& other) const {
    return _values == other._values;
  }

 private:
  std::vector<VariantType> _values;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;
  friend class foresighter::implementation::sql::PredicateSelectIntervalColumns;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateOrderedTuple>(other)) {
      //for all values in other, there exists a value in this that implies it
      for (const auto& value : std::get<PredicateOrderedTuple>(other)._values) {
        bool hasImplied = false;
        for (const auto& value2 : _values) {
          if (std::holds_alternative<PredicateSelectIntervalColumns>(value2)) {
            if (std::get<PredicateSelectIntervalColumns>(value2).implies(
                    value)) {
              hasImplied = true;
              break;
            }
          }
        }
        if (!hasImplied) {
          return false;
        }
      }
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
    archive(_values);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateOrderedTuple> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateOrderedTuple&
          predicateOrderedTuple) const {
    size_t h = 0;
    for (const auto& p : predicateOrderedTuple.getValues()) {
      foresighter::common::hashCombine(h, p);
    }
    return h;
  }
};
}  // namespace std