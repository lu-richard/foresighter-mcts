
/**
* @file nt_aggregation_list.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class NtOrderedGroupByList {
 public:
  static bool isNonTerminal() { return true; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  static std::string toString() { return "NtOrderedGroupByList"; }

  bool operator==([[maybe_unused]] const NtOrderedGroupByList& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::NtOrderedGroupByList> {
  size_t operator()([[maybe_unused]] const foresighter::implementation::sql::
                        NtOrderedGroupByList& op) const {
    return 0;
  }
};
}  // namespace std
