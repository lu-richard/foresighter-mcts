/**
* @file bottom.hpp
* @brief
* @author Rui Dong
* @date 4/22/24
*/

#pragma once
#include <cereal/access.hpp>
#include <string>
#include <variant>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class Bottom {
 public:
  static std::string toString() { return "BOTTOM"; }

  bool operator==([[maybe_unused]] const Bottom& other) const { return true; }

 private:
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    return true;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize([[maybe_unused]] Archive& archive) {}

};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::Bottom> {
  size_t operator()(
      [[maybe_unused]] const foresighter::implementation::sql::Bottom& bottom)
      const {
    return 0;
  }
};
}  // namespace std
