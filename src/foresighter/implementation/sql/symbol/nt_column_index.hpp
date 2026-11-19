
/**
* @file nt_column_index.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtColumnIndex {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtColumnIndex"; }

  bool operator==([[maybe_unused]] const NtColumnIndex& other) const {
    return true;
  }

  [[nodiscard]] static uint32_t getUnrollingIndex()  { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtColumnIndex> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::NtColumnIndex&
          op) const {
    return 0;
  }
};
}  // namespace std

