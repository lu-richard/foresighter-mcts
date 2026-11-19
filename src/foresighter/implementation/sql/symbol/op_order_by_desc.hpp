
/**
* @file op_order_by_desc.hpp
* @brief 
* @author Rui Dong
* @date 3/1/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpOrderByDesc {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpOrderByDesc"; }

  bool operator==([[maybe_unused]] const OpOrderByDesc& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpOrderByDesc> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpOrderByDesc&
          op) const {
    return 0;
  }
};
}  // namespace std
