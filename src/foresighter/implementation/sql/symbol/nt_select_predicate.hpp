
/**
* @file nt_select_predicate.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>
namespace foresighter::implementation::sql {
class NtSelectPredicate {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtSelectPredicate"; }

  bool operator==([[maybe_unused]] const NtSelectPredicate& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtSelectPredicate> {
  size_t operator()([[maybe_unused]] const foresighter::implementation::sql::
                        NtSelectPredicate& op) const {
    return 0;
  }
};
}  // namespace std

