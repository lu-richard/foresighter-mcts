
/**
* @file op_is_not_null.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpIsNotNull {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpIsNotNull"; }

  bool operator==([[maybe_unused]] const OpIsNotNull& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize(Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpIsNotNull> {
  size_t operator()(
      const foresighter::implementation::sql::OpIsNotNull& op) const {
    return 0;
  }
};
}  // namespace std
