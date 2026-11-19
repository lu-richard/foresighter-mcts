/**
 * @file exceptions.hpp
 * @brief
 * @author Rui Dong
 * @date 4/16/24
 */

#pragma once

#include <source_location>
#include <stdexcept>
#include <string>

namespace foresighter::common {
class NotImplementedException : public std::logic_error {
 public:
  using std::logic_error::logic_error;

  explicit NotImplementedException(
      const std::source_location location = std::source_location::current())
      : logic_error{std::string(location.function_name()) +
                    " is not implemented!"} {}
};

class InvalidArgument : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;

  explicit InvalidArgument(
      const std::string& message,
      const std::source_location location = std::source_location::current())
      : invalid_argument{std::string(location.function_name()) + message} {}

  explicit InvalidArgument(
      [[maybe_unused]] const char* message,
      const std::source_location location = std::source_location::current())
      : invalid_argument{"file: " + std::string(location.file_name()) + " " +
                         "function: " + std::string(location.function_name()) +
                         "line: " + std::to_string(location.line()) + " " +
                         "col: " + std::to_string(location.column()) + " " +
                         message} {}
};

}  // namespace foresighter::common
