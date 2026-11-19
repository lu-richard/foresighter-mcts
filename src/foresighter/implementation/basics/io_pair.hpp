#pragma once
/**
 * @file io_pair.hpp
 * @brief Input-output example e = (e_in, e_out) for synthesis.
 *
 * Template class IOPair<InputType, OutputType> holds one input-output example.
 * Used by Benchmark to specify the synthesis specification.
 */

#include <utility>

namespace foresighter::implementation::basics {
template <typename InputType, typename OutputType>
class IOPair {
 public:
  IOPair() = default;

  IOPair(const InputType& input, const OutputType& output)
      : input(input), output(output) {}

  IOPair(InputType&& input, OutputType&& output) noexcept
      : input(std::move(input)), output(std::move(output)) {}

  [[nodiscard]] const InputType& getInput() const noexcept { return input; }

  [[nodiscard]] const OutputType& getOutput() const noexcept { return output; }

 private:
  InputType input;
  OutputType output;
};
}  // namespace foresighter::implementation::basics
