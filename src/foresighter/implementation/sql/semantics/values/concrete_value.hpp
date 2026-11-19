#pragma once
#include <foresighter/implementation/basics/value.hpp>
#include <foresighter/implementation/sql/abstract_value/bottom.hpp>
#include <foresighter/implementation/sql/abstract_value/top.hpp>
#include <foresighter/implementation/sql/abstract_value/unabstracted_term.hpp>
#include <foresighter/implementation/sql/concrete_value/concrete_table.hpp>
#include <foresighter/implementation/sql/concrete_value/error.hpp>

namespace foresighter::implementation::sql::semantics::values {
using namespace foresighter::implementation::basics;

class ConcreteValue
    : public Value<ConcreteTable, Error, Top, UnAbstractedTerm, Bottom> {
 public:
  using TopType = Top;
  using BottomType = Error;

  ConcreteValue() = delete;

  explicit ConcreteValue(VariantType value) noexcept
      : Value(std::move(value)) {}

  template <typename T, typename... Args>
  explicit ConcreteValue(std::in_place_type_t<T> type, Args&&... args) noexcept
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
    foresighter::implementation::sql::semantics::values::ConcreteValue> {
  size_t operator()(
      const foresighter::implementation::sql::semantics::values::ConcreteValue&
          value) const {
    return value.hash();
  }
};
}  // namespace std