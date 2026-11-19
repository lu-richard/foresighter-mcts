/**
* @file predicate_subset_of_input.hpp
* @brief
* @author Rui Dong
* @date 4/22/24
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateSubsetOfInput {
 public:
  PredicateSubsetOfInput() = default;

  PredicateSubsetOfInput(int inputTableIdx, int idxInCur)
      : _inputTableIdx(inputTableIdx),
        _idxInCur(idxInCur) {}

  [[nodiscard]] int getInputTableIdx() const { return _inputTableIdx; }

  [[nodiscard]] int getIdxInCur() const { return _idxInCur; }

  [[nodiscard]] std::string toString() const {
    return "PredicateSubsetOfInput(" + std::to_string(_inputTableIdx) + ", "
           + std::to_string(_idxInCur) + ")";
  }

  bool operator==(const PredicateSubsetOfInput& other) const {
    return _inputTableIdx == other._inputTableIdx && _idxInCur == other._idxInCur;
  }

  bool operator<(const PredicateSubsetOfInput& other) const {
    return _inputTableIdx < other._inputTableIdx ||
           _inputTableIdx == other._inputTableIdx && _idxInCur < other._idxInCur;
  }

 private:
  int _inputTableIdx;
  int _idxInCur;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateSubsetOfInput>(other)) {
      return _inputTableIdx ==
                 std::get<PredicateSubsetOfInput>(other).getInputTableIdx() &&
             _idxInCur == std::get<PredicateSubsetOfInput>(other).getIdxInCur();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_inputTableIdx, _idxInCur);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::PredicateSubsetOfInput> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateSubsetOfInput&
          predicateSubsetOfInput) const {
    size_t h = 0;
    foresighter::common::hashCombine(h,
                                     predicateSubsetOfInput.getInputTableIdx(),
                                     predicateSubsetOfInput.getIdxInCur());
    return h;
  }
};
}  // namespace std
