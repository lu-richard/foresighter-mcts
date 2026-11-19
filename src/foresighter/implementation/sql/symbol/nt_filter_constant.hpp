
/**
* @file nt_column_index.hpp
* @brief 
* @author Rui Dong
* @date 9/15/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtFilterConstant {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtFilterConstantIndex"; }

  bool operator==([[maybe_unused]] const NtFilterConstant& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtFilterConstant> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::NtFilterConstant&
          op) const {
    return 0;
  }
};
}  // namespace std

