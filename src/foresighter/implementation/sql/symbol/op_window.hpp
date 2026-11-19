
/**
* @file op_window.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpWindow {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpWindow"; }

  bool operator==([[maybe_unused]] const OpWindow& other) const { return true; }

  [[nodiscard]] size_t getUnrolledTimes() const { return 0; }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpWindow> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpWindow& op)
      const {
    return 0;
  }
};
}  // namespace std
