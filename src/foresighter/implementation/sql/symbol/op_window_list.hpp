
/**
* @file op_list.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string> 

namespace foresighter::implementation::sql {
class OpWindowList {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpWindowList"; }

  bool operator==([[maybe_unused]] const OpWindowList& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpWindowList> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpWindowList& op)
      const {
    return 0;
  }
};
}  // namespace std

