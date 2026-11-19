
/**
* @file var_filter_const.hpp
* @brief 
* @author Rui Dong
* @date 9/15/2024
*/

#pragma once
#include <cereal/access.hpp>
#include <string>

namespace foresighter::implementation::sql {
class VarFilterConst {
 public:
  VarFilterConst() = default;

  explicit VarFilterConst(uint32_t index) : index(index) {}

  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return true; }

  static bool isOperator() { return false; }

  [[nodiscard]] std::string toString() const {
    return "VarFilterConst(" + std::to_string(index) + ")";
  }

  bool operator==([[maybe_unused]] const VarFilterConst& other) const {
    return index == other.index;
  }

  [[nodiscard]] uint32_t getIndex() const { return index; }

  [[nodiscard]] static uint32_t getUnrollingIndex()  { return 0; }

 private:
  uint32_t index{0};

  friend class cereal::access;

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(index);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::VarFilterConst> {
  size_t operator()(
      const foresighter::implementation::sql::VarFilterConst& op) const {
    return std::hash<uint32_t>()(op.getIndex());
  }
};
}  // namespace std

