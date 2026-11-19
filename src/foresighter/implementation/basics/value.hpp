#pragma once
/**
 * @file value.hpp
 * @brief Base classes for abstract value types: Value and Conjunction.
 *
 * Value<Types...> wraps a variant of predicate types (plus a Conjunction of them).
 * Conjunction<Types...> holds a set of predicates forming a logical AND. Together they
 * represent elements of the abstract domain used in the FTA's state space.
 */
#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <set>
#include <string>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::basics {

template <typename... Types>
class Conjunction {
 public:
  using VariantType = std::variant<Types...>;

  struct PredicateVariantLessThan {
    bool operator()(const VariantType& lhs, const VariantType& rhs) const {
      if (lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index();
      }
      return std::visit(
                 [](const auto& p) {
                   return std::hash<std::decay_t<decltype(p)>>{}(p);
                 },
                 lhs) <
             std::visit(
                 [](const auto& p) {
                   return std::hash<std::decay_t<decltype(p)>>{}(p);
                 },
                 rhs);
    }
  };

  Conjunction() = default;

  explicit Conjunction(const std::vector<VariantType>& conjunction) noexcept {
    _conjunction.insert(conjunction.begin(), conjunction.end());
    _hash = calculateHash();
  }

  [[nodiscard]] const auto& getConjunction() const noexcept {
    return _conjunction;
  }

  [[nodiscard]] size_t hash() const noexcept { return _hash; }

  [[nodiscard]] size_t calculateHash() noexcept {
    //for each predicate, calculate the hash and add it to the hash
    size_t hash = 0;
    for (const auto& p : _conjunction) {
      foresighter::common::hashCombine(hash, p);
    }
    return hash;
  }

  [[nodiscard]] std::string toString() const {
    std::string res;
    res.reserve(64);  // Pre-allocate assuming average string length
    res += "Conjunction(";

    bool first = true;
    for (const auto& p : _conjunction) {
      if (!first) {
        res += ", ";
      }
      first = false;
      res += std::visit([](const auto& p) { return p.toString(); }, p);
    }
    res += ")";
    return res;
  }

  bool operator==(const Conjunction& other) const noexcept {
    return _conjunction == other._conjunction;
  }

 private:
  std::set<VariantType, PredicateVariantLessThan> _conjunction;
  size_t _hash{};
  template <typename U>
  friend class Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <class Archive>
  void serialize(Archive& archive) {
    archive(_conjunction);
  }
};

template <typename... Types>
class Value {
 public:
  using ConjunctionType =
      foresighter::implementation::basics::Conjunction<Types...>;
  using VariantType = std::variant<Types..., ConjunctionType>;

  template <typename T, typename... Args>
  explicit Value(std::in_place_type_t<T> type, Args&&... args) noexcept
      : _value(type, std::forward<Args>(args)...) {}

  explicit Value(VariantType value) noexcept : _value(std::move(value)) {}

  Value() = default;
  Value(const Value&) = default;
  Value(Value&&) noexcept = default;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) noexcept = default;
  ~Value() = default;

  [[nodiscard]] std::string toString() const {
    return std::visit(
        [](const auto& value) -> std::string { return value.toString(); },
        _value);
  }

  [[nodiscard]] size_t hash() const noexcept {
    return std::visit(
        [](const auto& arg) noexcept -> size_t {
          return std::hash<std::decay_t<decltype(arg)>>()(arg);
        },
        _value);
  }

  [[nodiscard]] bool operator==(const Value& other) const noexcept {
    return _value == other._value;
  }

  [[nodiscard]] bool implies(const Value& other) const noexcept {
    return std::visit(
        [&other](const auto& arg) -> bool { return arg.implies(other); },
        _value);
  }

  [[nodiscard]] const auto& getValue() const noexcept { return _value; }

  [[nodiscard]] size_t getType() const noexcept { return _value.index(); }

 protected:
  VariantType _value;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(_value);
  }
};

}  // namespace foresighter::implementation::basics

namespace std {
template <typename... Types>
struct hash<foresighter::implementation::basics::Value<Types...>> {
  size_t operator()(
      const foresighter::implementation::basics::Value<Types...>& value) const {
    return value.hash();
  }
};

template <typename... Types>
struct hash<foresighter::implementation::basics::Conjunction<Types...>> {
  size_t operator()(
      const foresighter::implementation::basics::Conjunction<Types...>&
          conjunction) const {
    return conjunction.hash();
  }
};
}  // namespace std