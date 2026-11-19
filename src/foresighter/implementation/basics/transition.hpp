#pragma once
/**
 * @file transition.hpp
 * @brief FTA transition f(q1,...,qn) -> q (element of Delta).
 *
 * Represents a transition rule in the FTA: an operator symbol applied to tail states
 * producing a head state, with an associated cost and column reordering.
 * Hash-consed via EntityManager.
 */

#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>
#include <vector>

#include <foresighter/common/hash.hpp>
#include <foresighter/implementation/basics/types.hpp>

namespace foresighter::implementation::basics {
template <class SymbolType>
class Transition {
 public:
  using IDType = foresighter::implementation::basics::IDType;

  Transition() noexcept = default;

  Transition(IDType head, SymbolType symbol, const std::vector<IDType>& tail,
             WeightType cost, const std::vector<int>& order) noexcept
      : head(head),
        tail(tail),
        symbol(std::move(symbol)),
        cost(cost),
        order(order) {
    hashCode = calculateHashCode();
  }

  [[nodiscard]] IDType getHead() const noexcept { return head; }

  [[nodiscard]] const std::vector<IDType>& getTails() const noexcept {
    return tail;
  }

  [[nodiscard]] const SymbolType& getSymbol() const noexcept { return symbol; }

  [[nodiscard]] WeightType getCost() const noexcept { return cost; }

  [[nodiscard]] const std::vector<int>& getOrder() const noexcept {
    return order;
  }

  bool operator==(const Transition& rhs) const noexcept {
    return head == rhs.head && tail == rhs.tail && symbol == rhs.symbol;
  }

  bool operator!=(const Transition& rhs) const noexcept {
    return !(rhs == *this);
  }

  [[nodiscard]] std::string toString() const {
    std::string str;
    str.reserve(32);  // Pre-allocate reasonable size
    str += '(';
    for (const auto state : tail) {
      str += std::to_string(state);
      str += ',';
    }
    if (!tail.empty()) {
      str.pop_back();
    }
    str += ") --";
    str += symbol.toString();
    if (!order.empty()) {
      str += '(';
      for (const auto i : order) {
        str += std::to_string(i);
        str += ',';
      }
      str.pop_back();
      str += ')';
    }
    str += "--> ";
    str += std::to_string(head);
    return str;
  }

  [[nodiscard]] size_t getHashCode() const noexcept { return hashCode; }

 private:
  IDType head{};
  std::vector<IDType> tail;
  SymbolType symbol;
  WeightType cost{};
  size_t hashCode{};
  // after the transition, reorder the columns to (order[0], order[1], ..., order[n-1])
  std::vector<int> order;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(head, tail, symbol, cost, order);
    hashCode = calculateHashCode();
  }

  size_t calculateHashCode() noexcept {
    size_t h = 0;
    for (const auto state : tail) {
      foresighter::common::hashCombine(h, state);
    }
    foresighter::common::hashCombine(h, symbol);
    foresighter::common::hashCombine(h, head);
    for (const auto state : order) {
      foresighter::common::hashCombine(h, state);
    }
    return h;
  }
};
}  // namespace foresighter::implementation::basics

namespace std {
template <typename T>
struct hash<foresighter::implementation::basics::Transition<T>> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::basics::Transition<T>& transition)
      const noexcept {
    return transition.getHashCode();
  }
};
}  // namespace std
