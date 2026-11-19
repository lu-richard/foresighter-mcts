
/**
* @file nt_column_index_interval.hpp
* @brief 
* @author Qingyue Wu
* @date 5/18/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtColumnIndexInterval {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtColumnIndexInterval"; }

  bool operator==([[maybe_unused]] const NtColumnIndexInterval& other) const {
    return true;
  }

  [[nodiscard]] static uint32_t getUnrollingIndex()  { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtColumnIndexInterval> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::NtColumnIndexInterval&
          op) const {
    return 0;
  }
};
}  // namespace std

