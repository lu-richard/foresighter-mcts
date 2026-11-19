#pragma once
/**
 * @file term.hpp
 * @brief Concrete syntax tree (program term) extracted from the FTA.
 *
 * Term<SymbolType> is a tree where each node holds an operator/constant/variable symbol
 * and its children. Represents the synthesized program. Supports weight, depth, and
 * AST size computation for cost-bounded enumeration.
 */

#include <foresighter/common/hash.hpp>
#include <foresighter/implementation/basics/types.hpp>
#include <variant>
#include <vector>

#include <cereal/access.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

namespace foresighter::implementation::basics {
template <class _SymbolType>
class Term {
 public:
  Term() = default;
  using WeightType = foresighter::implementation::basics::WeightType;
  using SymbolType = _SymbolType;

  explicit Term(SymbolType contained, WeightType curWeight = 1)
      : contained(std::move(contained)), cost(curWeight), children({}) {
    hashCode = calculateHashCode();
  }

  Term(const SymbolType& contained, std::vector<Term<SymbolType>> children,
       WeightType curWeight = 1)
      : contained(contained), children(std::move(children)), cost(curWeight) {
    hashCode = calculateHashCode();
  }

  [[nodiscard]] const SymbolType& getSymbol() const noexcept {
    return contained;
  }

  [[nodiscard]] const std::vector<Term<SymbolType>>& getChildren()
      const noexcept {
    return children;
  }

  [[nodiscard]] WeightType getTotalWeight() const noexcept {
    if (children.empty()) {
      return 0;
    }
    WeightType total = cost;
    for (const auto& child : children) {
      total += child.getTotalWeight();
    }
    return total;
  }

  [[nodiscard]] WeightType getCurrentLevelWeight() const noexcept {
    return cost;
  }

  [[nodiscard]] uint32_t getDepth() const noexcept {
    uint32_t maxDepth = 0;
    for (const auto& child : children) {
      maxDepth = std::max(maxDepth, child.getDepth());
    }
    return maxDepth + 1;
  }

  [[nodiscard]] uint32_t getASTSize() const noexcept {
    uint32_t size = 1;
    for (const auto& child : children) {
      size += child.getASTSize();
    }
    return size;
  }

  [[nodiscard]] uint32_t getASTSizeWithoutIdentityAndIntermediateProjection()
      const noexcept {
    auto contained_string = contained.toString();
    if (contained_string == "OpIdentity" || contained_string == "Projection") {
      return children[0].getASTSizeWithoutIdentityAndIntermediateProjection();
    }
    uint32_t size = 1;
    for (const auto& child : children) {
      size += child.getASTSizeWithoutIdentityAndIntermediateProjection();
    }
    return size;
  }

  [[nodiscard]] bool operator==(const Term<SymbolType>& other) const noexcept {
    return contained == other.contained && children == other.children;
  }

  [[nodiscard]] std::string toString() const {
    std::string result;
    result.reserve(32);  // Pre-allocate reasonable size
    result += contained.toString();

    if (!children.empty()) {
      result += '(';
      for (size_t i = 0; i < children.size() - 1; ++i) {
        result += children[i].toString();
        result += ',';
      }
      result += children.back().toString();
      result += ')';
    }
    return result;
  }

  [[nodiscard]] std::string toDSLSketchString() const {
    std::string result;
    result.reserve(32);  // Pre-allocate reasonable size
    result += contained.toString();
    if (result == "Projection") {
      return children[0].toDSLSketchString();
    }
    if (result == "Select" || result == "OpIdentity" || result == "Distinct" ||
        result == "GroupWithKey" || result == "GroupWithoutKey" ||
        result == "InnerJoin" || result == "LeftJoin" ||
        result == "OrderByLimit" || result == "GroupByOrderByCountLimit") {
      if (!children.empty()) {
        result += '(';
        for (size_t i = 0; i < children.size() - 1; ++i) {
          result += children[i].toDSLSketchString();
          result += ',';
        }
        result += children.back().toDSLSketchString();
        result += ')';
      }
      return result;
    }
    return "...";
  }

  [[nodiscard]] size_t getHashCode() const noexcept { return hashCode; }

 private:
  SymbolType contained;
  WeightType cost{};
  std::vector<Term<SymbolType>> children;
  size_t hashCode;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(contained, cost, children);
    hashCode = calculateHashCode();
  }

  [[nodiscard]] size_t calculateHashCode() const noexcept {
    size_t h = 0;
    foresighter::common::hashCombine(h, contained, cost);
    for (const auto& child : children) {
      foresighter::common::hashCombine(h, child);
    }
    return h;
  }
};
}  // namespace foresighter::implementation::basics

namespace std {
template <typename T>
struct hash<foresighter::implementation::basics::Term<T>> {
  size_t operator()(
      const foresighter::implementation::basics::Term<T>& term) const noexcept {
    return term.getHashCode();
  }
};
}  // namespace std