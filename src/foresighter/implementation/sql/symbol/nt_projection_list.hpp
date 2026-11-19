
/**
* @file nt_aggregation_list.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtProjectionList {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtProjectionList"; }

  bool operator==([[maybe_unused]] const NtProjectionList& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtProjectionList> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::NtProjectionList&
          op) const {
    return 0;
  }
};
}  // namespace std
