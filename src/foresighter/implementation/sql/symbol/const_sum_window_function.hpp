
/**
* @file const_sum_window_function.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class ConstSumWindowFunction {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return true; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "ConstSumWindowFunction"; }

  bool operator==([[maybe_unused]] const ConstSumWindowFunction& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::ConstSumWindowFunction> {
  size_t operator()([[maybe_unused]] const foresighter::implementation::sql::
                        ConstSumWindowFunction& op) const {
    return 0;
  }
};
}  // namespace std
