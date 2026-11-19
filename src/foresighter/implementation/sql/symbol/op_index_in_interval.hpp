
/**
* @file op_index_in_interval.hpp
* @brief 
* @author Qingyue Wu
* @date 5/18/25
*/

#pragma once
#include <string>

namespace foresighter::implementation::sql {
class OpIndexInInterval {
 public:
  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return false; }

  static bool isOperator() { return true; }

  static std::string toString() { return "OpIndexInInterval"; }

  bool operator==([[maybe_unused]] const OpIndexInInterval& other) const {
    return true;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

  //cereal serializatio for empty class
  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::OpIndexInInterval> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::OpIndexInInterval& op)
      const {
    return 0;
  }
};
}  // namespace std

