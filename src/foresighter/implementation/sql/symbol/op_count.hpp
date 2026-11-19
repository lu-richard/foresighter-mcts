
/**
* @file op_count.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpCount {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpCount"; }

  bool operator==([[maybe_unused]] const OpCount& other) const { return true; }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpCount> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpCount& op)
      const {
    return 0;
  }
};
}  // namespace std
