
/**
* @file op_filter_neq.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpFilterNeq {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpFilterNeq"; }

  bool operator==([[maybe_unused]] const OpFilterNeq& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpFilterNeq> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpFilterNeq& op)
      const {
    return 0;
  }
};
}  // namespace std
