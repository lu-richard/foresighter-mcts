#pragma once

#include <foresighter/implementation/basics/value.hpp>
#include <foresighter/implementation/sql/abstract_value/bottom.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_select_nth_column.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_num_of_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_ordered_tuple.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_total_n_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/top.hpp>
#include "foresighter/implementation/sql/abstract_value/predicate_col_type.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_subset_of_input.hpp"
#include "foresighter/implementation/sql/abstract_value/unabstracted_term.hpp"

namespace foresighter::implementation::sql::semantics::values {
using namespace foresighter::implementation::basics;

class OfflinePlusSOIValue
    : public Value<Bottom, PredicateNumOfColumns, UnAbstractedTerm, Top,
                   PredicateSubsetOfInput> {
 public:
  using TopType = Top;
  using BottomType = Bottom;

  OfflinePlusSOIValue() = default;

  explicit OfflinePlusSOIValue(VariantType value) noexcept
      : Value(std::move(value)) {}

  template <typename T, typename... Args>
  explicit OfflinePlusSOIValue(std::in_place_type_t<T> type,
                               Args&&... args) noexcept
      : Value(type, std::forward<Args>(args)...) {}

 private:
  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(_value);
  }
};
}  // namespace foresighter::implementation::sql::semantics::values

namespace std {
template <>
struct hash<
    foresighter::implementation::sql::semantics::values::OfflinePlusSOIValue> {
  size_t operator()(const foresighter::implementation::sql::semantics::values::
                        OfflinePlusSOIValue& value) const {
    return value.hash();
  }
};
}  // namespace std