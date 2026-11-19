
/**
* @file op_filter_like.hpp
* @brief 
* @author Rui Dong
* @date 5/18/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpFilterLike {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpFilterLike"; }

  bool operator==([[maybe_unused]] const OpFilterLike& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpFilterLike> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpFilterLike& op)
      const {
    return 0;
  }
};
}  // namespace std
