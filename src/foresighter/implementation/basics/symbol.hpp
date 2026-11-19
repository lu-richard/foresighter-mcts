#pragma once
/**
 * @file symbol.hpp
 * @brief Base class for grammar symbol types (non-terminals, operators, constants, variables).
 *
 * Symbol<Types...> wraps a std::variant of concrete symbol types and provides uniform
 * classification (isNonTerminal, isOperator, isConstant, isVariable), hashing, and
 * support for grammar unrolling via idxForUnrolling.
 */
#include <cereal/access.hpp>
#include <string>
#include <variant>
#include "foresighter/common/hash.hpp"

namespace foresighter::implementation::basics {

template <typename... Types>
class Symbol {
 public:
  using VariantType = std::variant<Types...>;

  template <typename T, typename... Args>
  explicit Symbol(std::in_place_type_t<T>, Args&&... args,
                  size_t idxForUnrolling = 0)
      : _symbol(std::in_place_type<T>, std::forward<Args>(args)...),
        idxForUnrolling(idxForUnrolling) {}

  explicit Symbol(VariantType symbol, size_t idxForUnrolling = 0)
      : _symbol(symbol), idxForUnrolling(idxForUnrolling) {}

  explicit Symbol(const Symbol& symbol, size_t idxForUnrolling)
      : _symbol(symbol.getSymbol()), idxForUnrolling(idxForUnrolling) {}

  Symbol() = default;
  Symbol(const Symbol&) = default;
  Symbol(Symbol&&) noexcept = default;
  Symbol& operator=(const Symbol&) = default;
  Symbol& operator=(Symbol&&) noexcept = default;

  [[nodiscard]] std::string toString() const {
    if (idxForUnrolling == 0) {
      return std::visit(
          [](const auto& arg) -> std::string { return arg.toString(); },
          _symbol);
    } else {

      return std::visit(
                 [](const auto& arg) -> std::string { return arg.toString(); },
                 _symbol) +
             "_" + std::to_string(idxForUnrolling);
    }
  }

  [[nodiscard]] bool isNonTerminal() const {
    return std::visit(
        [](const auto& arg) -> bool { return arg.isNonTerminal(); }, _symbol);
  }

  [[nodiscard]] bool isConstant() const {
    return std::visit([](const auto& arg) -> bool { return arg.isConstant(); },
                      _symbol);
  }

  [[nodiscard]] bool isVariable() const {
    return std::visit([](const auto& arg) -> bool { return arg.isVariable(); },
                      _symbol);
  }

  [[nodiscard]] bool isOperator() const {
    return std::visit([](const auto& arg) -> bool { return arg.isOperator(); },
                      _symbol);
  }

  [[nodiscard]] size_t hash() const noexcept {
    return std::visit(
        [](const auto& arg) noexcept -> size_t {
          return std::hash<std::decay_t<decltype(arg)>>()(arg);
        },
        _symbol);
  }

  bool operator==(const Symbol& other) const noexcept {
    return _symbol == other._symbol && idxForUnrolling == other.idxForUnrolling;
  }

  [[nodiscard]] const VariantType& getSymbol() const noexcept {
    return _symbol;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return idxForUnrolling; }

  [[nodiscard]] size_t getType() const noexcept { return _symbol.index(); }

 protected:
  VariantType _symbol;
  size_t idxForUnrolling;
  friend class cereal::access;

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_symbol, idxForUnrolling);
  }
};

}  // namespace foresighter::implementation::basics

namespace std {
template <typename... Types>
struct hash<foresighter::implementation::basics::Symbol<Types...>> {
  size_t operator()(const foresighter::implementation::basics::Symbol<Types...>&
                        symbol) const {
    size_t hash = 0;
    foresighter::common::hashCombine(hash, symbol.hash(), symbol.getType(),
                                     symbol.getUnrollingIndex());
    return hash;
  }
};
}  // namespace std
