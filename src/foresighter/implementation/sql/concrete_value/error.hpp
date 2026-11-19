#pragma once
#include <cereal/access.hpp>
#include <cereal/types/string.hpp>
#include <string>
#include <utility>
#include <variant>

namespace foresighter::implementation::sql {
class Error {
 public:
  explicit Error(std::string error_message) noexcept
      : error_message(std::move(error_message)) {}

  [[nodiscard]] const std::string& toString() const noexcept {
    return error_message;
  }

  bool operator==(const Error& other) const noexcept {
    return error_message == other.error_message;
  }

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    return std::holds_alternative<Error>(other);
  }

 private:
  std::string error_message;
  friend class cereal::access;

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(error_message);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::Error> {
  size_t operator()(
      const foresighter::implementation::sql::Error& error) const noexcept {
    return hash<std::string>()(error.toString());
  }
};
}  // namespace std
