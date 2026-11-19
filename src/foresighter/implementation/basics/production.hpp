#pragma once
/**
 * @file production.hpp
 * @brief Production rule s -> f(s1,...,sn) in the context-free grammar.
 *
 * Each Production has a left-hand-side non-terminal (lhs), an operator symbol (op),
 * right-hand-side symbols (rhs), and a cost weight. Hash-consed for deduplication.
 */

#include <vector>
#include "foresighter/common/hash.hpp"
#include "foresighter/implementation/basics/types.hpp"

#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>

namespace foresighter::implementation::basics {
template <class SymbolType>
class Production {
 public:
  using WeightType = foresighter::implementation::basics::WeightType;
  Production() = default;

  Production(const SymbolType& lhs, const SymbolType& op,
             const std::vector<SymbolType>& rhs, WeightType cost)
      : lhs(lhs), rhs(rhs), op(op), cost(cost), hashCode(calculateHashCode()) {}

  [[nodiscard]] const SymbolType& getLhs() const noexcept { return lhs; }

  [[nodiscard]] const std::vector<SymbolType>& getRhs() const noexcept {
    return rhs;
  }

  [[nodiscard]] const SymbolType& getOp() const noexcept { return op; }

  [[nodiscard]] bool operator==(const Production& other) const noexcept {
    return lhs == other.lhs && rhs == other.rhs && op == other.op;
  }

  [[nodiscard]] std::string toString() const {
    std::string result;
    result.reserve(64);  // Pre-allocate reasonable size
    result += lhs.toString();
    result += " ->(";
    result += std::to_string(cost);
    result += ") ";
    result += op.toString();
    result += '(';
    for (size_t i = 0; i < rhs.size() - 1; ++i) {
      result += rhs[i].toString();
      result += ',';
    }
    if (!rhs.empty()) {
      result += rhs.back().toString();
    }
    result += ')';
    return result;
  }

  [[nodiscard]] WeightType getCost() const noexcept { return cost; }

  [[nodiscard]] size_t getHashCode() const noexcept { return hashCode; }

 private:
  SymbolType lhs;
  std::vector<SymbolType> rhs;
  SymbolType op;
  WeightType cost = 0;
  size_t hashCode;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(lhs, rhs, op, cost);
    hashCode = calculateHashCode();
  }

  [[nodiscard]] size_t calculateHashCode() const noexcept {
    size_t h = 0;
    foresighter::common::hashCombine(h, lhs);
    foresighter::common::hashCombine(h, op);
    for (const auto& symbol : rhs) {
      foresighter::common::hashCombine(h, symbol);
    }
    return h;
  }
};
}  // namespace foresighter::implementation::basics

namespace std {
template <typename T>
struct hash<foresighter::implementation::basics::Production<T>> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::basics::Production<T>& production)
      const noexcept {
    return production.getHashCode();
  }
};
}  // namespace std