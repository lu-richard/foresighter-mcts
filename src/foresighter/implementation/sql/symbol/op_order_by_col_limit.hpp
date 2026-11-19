
/**
* @file op_order_by_col_limit.hpp
* @brief 
* @author Rui Dong
* @date 2/27/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpOrderByColLimit {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OrderByLimit"; }

  bool operator==([[maybe_unused]] const OpOrderByColLimit& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpOrderByColLimit> {
  size_t operator()([[maybe_unused]] const foresighter::implementation::sql::
                        OpOrderByColLimit& op) const {
    return 0;
  }
};
}  // namespace std
