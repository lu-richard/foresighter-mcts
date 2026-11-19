
/**
* @file const_column_index.hpp
* @brief 
* @author Rui Dong
* @date 4/21/24
*/

#pragma once
#include <cereal/access.hpp>

namespace foresighter::implementation::sql {
class ConstColumnIndex {
 public:
  ConstColumnIndex() = default;

  explicit ConstColumnIndex(uint32_t index) : index(index) {}

  static bool isNonTerminal() { return false; }

  static bool isConstant() { return true; }

  static bool isVariable() { return false; }

  static bool isOperator() { return false; }

  [[nodiscard]] std::string toString() const {
    return "ConstColumnIndex(" + std::to_string(index) + ")";
  }

  bool operator==([[maybe_unused]] const ConstColumnIndex& other) const {
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
struct hash<foresighter::implementation::sql::ConstColumnIndex> {
  size_t operator()(
      const foresighter::implementation::sql::ConstColumnIndex& op) const {
    return std::hash<uint32_t>()(0);
  }
};
}  // namespace std

