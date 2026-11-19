
/**
* @file nt_order_by_list.hpp
* @brief 
* @author Rui Dong
* @date 3/1/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtOrderByList {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtOrderByList"; }

  bool operator==([[maybe_unused]] const NtOrderByList& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtOrderByList> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::NtOrderByList&
          op) const {
    return 0;
  }
};
}  // namespace std
