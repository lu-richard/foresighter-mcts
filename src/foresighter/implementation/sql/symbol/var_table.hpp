
/**
* @file const_table_index.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <cereal/access.hpp>
#include <string>

namespace foresighter::implementation::sql {
class VarTable {
 public:
  VarTable() = default;

  explicit VarTable(size_t index) : index(index) {}

  static bool isNonTerminal() { return false; }

  static bool isConstant() { return false; }

  static bool isVariable() { return true; }

  static bool isOperator() { return false; }

  [[nodiscard]] std::string toString() const {
    return "VarTableIndex(" + std::to_string(index) + ")";
  }

  [[nodiscard]] uint32_t getIndex() const { return index; }

  bool operator==(const VarTable& other) const { return index == other.index; }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

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
struct hash<foresighter::implementation::sql::VarTable> {
  size_t operator()(
      const foresighter::implementation::sql::VarTable& op) const {
    return std::hash<uint32_t>()(op.getIndex());
  }
};
}  // namespace std

