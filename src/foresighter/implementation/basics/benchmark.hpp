#pragma once
/**
 * @file benchmark.hpp
 * @brief Base class for synthesis benchmarks, holding IO examples, ground truth, and hints.
 *
 * Template class Benchmark<IOPairType> stores a vector of input-output pairs,
 * a benchmark name, the ground-truth program, and optional hints (filter constants).
 */
#include <string>
#include <vector>

namespace foresighter::implementation::basics {

template <class IOPairType>
class Benchmark {
 public:
  using IOPair = IOPairType;

  Benchmark(std::vector<IOPair> io_pairs, std::string name,
            std::string ground_truth, std::vector<std::string> hints)
      : io_pairs(std::move(io_pairs)),
        name(std::move(name)),
        ground_truth(std::move(ground_truth)),
        hints(std::move(hints)) {}

  // Use noexcept where possible to enable move optimizations
  Benchmark(const Benchmark&) = default;
  Benchmark& operator=(const Benchmark&) = default;
  Benchmark(Benchmark&&) noexcept = default;
  Benchmark& operator=(Benchmark&&) noexcept = default;

  /**
     * @brief Get the input/output pairs of the benchmark
     * @return The input/output pairs of the benchmark
     */
  [[nodiscard]] const std::vector<IOPair>& getIOPairs() const noexcept {
    return io_pairs;
  }

  /**
     * @brief Get the name of the benchmark
     * @return The name of the benchmark
     */
  [[nodiscard]] const std::string& getName() const noexcept { return name; }

  [[nodiscard]] const std::string& getGroundTruth() const noexcept {
    return ground_truth;
  }

  [[nodiscard]] const std::vector<std::string>& getHints() const noexcept {
    return hints;
  }

 private:
  std::vector<IOPair> io_pairs;
  std::string name;
  std::string ground_truth;
  std::vector<std::string> hints;
};
}  // namespace foresighter::implementation::basics
