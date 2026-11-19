/**
* @file predicate_row_num_range.hpp
* @brief 
* @author Rui Dong
* @date 4/9/25
*/

#pragma once

#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <cereal/types/vector.hpp>
#include <string>
#include <utility>
#include <cassert>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class PredicateRowNumRange {
 public:
  class RowNumWithParameter {
   public:
    RowNumWithParameter() {
      _iszero = true;
    }
    RowNumWithParameter(bool _iszero, std::vector<int> _power)
        : _iszero(_iszero), _power(std::move(_power)) {}

    [[nodiscard]] bool iszero() const {return _iszero;}
    [[nodiscard]] std::vector<int> getPower() const {return _power;}

    [[nodiscard]] std::string toString() const {
      if (_iszero) {
        return "RowNum(0)";
      }
      std::string result = "RowNumWithParameter(";
      for (int i = 0; i < _power.size(); ++i) {
        result += std::to_string(_power[i]);
        if (i < _power.size() - 1) result += ", ";
      }
      result += ")";
      return result;
    }

    [[nodiscard]] int toConcrete(const std::vector<int>& numOfRowsInInput) {
      if(_iszero) return 0;
      int res = 1;
      assert(numOfRowsInInput.size() <= _power.size());
      for(int i = 0; i < numOfRowsInInput.size(); ++i) {
        for(int c = 0; c < _power[i]; ++c) {
          res = res * numOfRowsInInput[i];
        }
      }
      return res;
    }

    bool operator == (const RowNumWithParameter& other) const {
      if (_iszero && other._iszero) return true;
      if (_iszero != other._iszero) return false;
      if (_power.size() != other._power.size()) return false;
      for (int i = 0; i < _power.size(); ++i) {
        if(_power[i] != other._power[i]) return false;
      }
      return true;
    }
    
    bool operator >= (const RowNumWithParameter& other) const {
      if (_iszero && other._iszero) return true;
      if (_iszero != other._iszero) return _iszero >= other._iszero;
      assert(_power.size() == other._power.size());
      for(int i = 0; i < _power.size(); ++i) {
        if (_power[i] < other._power[i]) return false;
      }
      return true;
    }

    bool operator <= (const RowNumWithParameter& other) const {
      return other >= *this;
    }

    RowNumWithParameter operator * (const RowNumWithParameter& other) const {
      if(_iszero || other._iszero) return {true, {}};
      assert(_power.size() == other._power.size());
      std::vector<int> result;
      result.resize(_power.size());
      for(int i = 0; i < result.size(); ++i) {
        result[i] = _power[i] + other._power[i];
      }
      return RowNumWithParameter{false, result};
    }
  
   private:
    bool _iszero;
    std::vector<int> _power;
    friend class cereal::access;

    template <typename Archive>
    void serialize(Archive& archive) {
      archive(_iszero, _power);
    }
  };

  PredicateRowNumRange() = default;

  explicit PredicateRowNumRange(RowNumWithParameter lowerBound,
      RowNumWithParameter upperBound)
      : _lowerBound(std::move(lowerBound)), _upperBound(std::move(upperBound)) {}

  [[nodiscard]] RowNumWithParameter getLowerBound() const { return _lowerBound; }

  [[nodiscard]] RowNumWithParameter getUpperBound() const { return _upperBound; }

  [[nodiscard]] std::string toString() const {
    return "PredicateRowNumRange(" + _lowerBound.toString() + ", " +
           _upperBound.toString() + ")";
  }

  bool operator==(const PredicateRowNumRange& other) const {
    return _lowerBound == other._lowerBound && _upperBound == other._upperBound;
  }

  PredicateRowNumRange operator * (const PredicateRowNumRange& other) const {
    return PredicateRowNumRange{_lowerBound * other._lowerBound,
        _upperBound * other._upperBound};
  }

 private:
  RowNumWithParameter _lowerBound;
  RowNumWithParameter _upperBound;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<PredicateRowNumRange>(other)) {
      return _lowerBound >=
                 std::get<PredicateRowNumRange>(other).getLowerBound() &&
             _upperBound <=
                 std::get<PredicateRowNumRange>(other).getUpperBound();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_lowerBound, _upperBound);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {

template <>
struct hash<foresighter::implementation::sql::PredicateRowNumRange::RowNumWithParameter> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateRowNumRange::RowNumWithParameter&
          rowNumWithParameter) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, rowNumWithParameter.iszero());
    for (auto p: rowNumWithParameter.getPower()) {
      foresighter::common::hashCombine(h, p);
    }
    return h;
  }
};
template <>
struct hash<foresighter::implementation::sql::PredicateRowNumRange> {
  size_t operator()(
      const foresighter::implementation::sql::PredicateRowNumRange&
          predicateRowNumRange) const {
    size_t h = 0;
    foresighter::common::hashCombine(h, predicateRowNumRange.getLowerBound());
    foresighter::common::hashCombine(h, predicateRowNumRange.getUpperBound());
    return h;
  }
};
}  // namespace std
