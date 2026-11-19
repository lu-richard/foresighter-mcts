#pragma once
/**
 * @file state.hpp
 * @brief FTA state q = (symbol, value, cost).
 *
 * Each state in the FTA is a triple of a grammar symbol, an abstract value,
 * and a cost (AST size). States are hash-consed via EntityManager for deduplication.
 */

#include <foresighter/common/hash.hpp>
#include <foresighter/implementation/basics/types.hpp>
#include <vector>

#include <cereal/access.hpp>

namespace foresighter::implementation::basics {
template <class SymbolType, class ValueType>
class State {
 public:
  using IDType = foresighter::implementation::basics::IDType;

  State() = default;

  State(const SymbolType& symbol, const ValueType& value, int cost) noexcept
      : symbol(symbol), value(value), cost(cost) {
    hashCode = calculateHashCode();
  }

  [[nodiscard]] const SymbolType& getSymbol() const noexcept { return symbol; }

  [[nodiscard]] const ValueType& getValue() const noexcept { return value; }

  [[nodiscard]] int getCost() const noexcept { return cost; }

  bool operator==(const State& other) const noexcept {
    return symbol == other.symbol && value == other.value && cost == other.cost;
  }

  [[nodiscard]] std::string toString() const {
    return "State(" + symbol.toString() + ", " + value.toString() + ", " +
           std::to_string(cost) + ")";
  }

  [[nodiscard]] size_t getHashCode() const noexcept { return hashCode; }

 private:
  SymbolType symbol;
  ValueType value;
  size_t hashCode;
  int cost;
  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(symbol, value, cost);
    hashCode = calculateHashCode();
  }

  [[nodiscard]] size_t calculateHashCode() const noexcept {
    size_t h = 0;
    foresighter::common::hashCombine(h, symbol, value, cost);
    return h;
  }
};
}  // namespace foresighter::implementation::basics

namespace std {
template <typename S, typename V>
struct hash<foresighter::implementation::basics::State<S, V>> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::basics::State<S, V>& state)
      const noexcept {
    return state.getHashCode();
  }
};
}  // namespace std