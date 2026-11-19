#pragma once

#include <cassert>
#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <foresighter/implementation/basics/types.hpp>
#include <optional>
#include <vector>

namespace foresighter::implementation::basics {
class VariableControlledSourceSet {
 public:
  using IDType = foresighter::implementation::basics::IDType;

  explicit VariableControlledSourceSet(bool valid) noexcept : valid(valid) {}

  explicit VariableControlledSourceSet(size_t numberOfVariables) {
    sourceSet.resize(numberOfVariables);
  }

  [[nodiscard]] bool isValid() const noexcept { return valid; }

  [[nodiscard]] const std::vector<std::optional<IDType>>& getSourceSet()
      const noexcept {
    return sourceSet;
  }

  void setSourceSet(IDType variableIndex,
                    std::optional<IDType> source) noexcept {
    sourceSet[variableIndex] = source;
  }

  [[nodiscard]] bool match(
      const VariableControlledSourceSet& other) const noexcept {
    if (sourceSet.size() < other.sourceSet.size()) {
      return false;
    }

    for (size_t i = 0; i < other.sourceSet.size(); ++i) {
      if (!sourceSet[i].has_value()) {
        continue;
      }
      if (sourceSet[i].has_value() && !other.sourceSet[i].has_value()) {
        return false;
      }
      if (sourceSet[i].value() != other.sourceSet[i].value()) {
        return false;
      }
    }
    return true;
  }

  bool operator==(const VariableControlledSourceSet& other) const noexcept {
    return sourceSet == other.sourceSet;
  }

  bool operator!=(const VariableControlledSourceSet& other) const noexcept {
    return sourceSet != other.sourceSet;
  }

  // Note: in alphabetical order, not in subset order!
  [[nodiscard]] bool operator<=(
      const VariableControlledSourceSet& other) const noexcept {
    if (!valid)
      return true;
    if (!other.valid)
      return false;
    if (size() < other.size())
      return true;
    if (size() > other.size())
      return false;
    for (size_t i = 0; i < size(); i++) {
      if (!sourceSet[i].has_value())
        return true;
      if (!other.getSourceSet()[i].has_value())
        return false;
      if (sourceSet[i].value() < other.getSourceSet()[i].value())
        return true;
      if (sourceSet[i].value() > other.getSourceSet()[i].value())
        return false;
    }
    return true;
  }

  [[nodiscard]] bool operator<(
      const VariableControlledSourceSet& other) const noexcept {
    return *this <= other && *this != other;
  }

  [[nodiscard]] std::string toString() const {
    std::string result;
    result.reserve(sourceSet.size() * 4 +
                   2);  // Estimate size to avoid reallocations
    result = "(";
    for (const auto& source : sourceSet) {
      if (source.has_value()) {
        result += std::to_string(source.value()) + ",";
      } else {
        result += "_,";
      }
    }
    result += ")";
    return result;
  }

  void invalidate() noexcept { valid = false; }

  [[nodiscard]] size_t size() const noexcept { return sourceSet.size(); }

  [[nodiscard]] VariableControlledSourceSet intersect(
      const VariableControlledSourceSet& other) const {
    if (sourceSet.size() != other.sourceSet.size()) {
      return VariableControlledSourceSet(false);
    }

    VariableControlledSourceSet result(sourceSet.size());
    for (size_t i = 0; i < sourceSet.size(); ++i) {
      if (!sourceSet[i].has_value()) {
        result.setSourceSet(i, other.sourceSet[i]);
        continue;
      }
      if (!other.sourceSet[i].has_value()) {
        result.setSourceSet(i, sourceSet[i]);
        continue;
      }
      if (sourceSet[i].value() == other.sourceSet[i].value()) {
        result.setSourceSet(i, sourceSet[i]);
        continue;
      }
      return VariableControlledSourceSet(false);
    }
    return result;
  }

  // get all supersets in alphabetical order. Require sourseStates to be ordered.
  // for example, if sourceStates is {{1,2}, {4, 5, 6}, {3, 7}}, and suppose
  // sourceSet is [1, _, 3], then the supersets are [1, 2, 3], [1, 5, 3], [1, 6, 3]
  [[nodiscard]] std::vector<VariableControlledSourceSet> getSupersets(
      const std::vector<std::vector<IDType>>& sourceStates) const {
    assert(sourceStates.size() == size());

    std::vector<int> currentIndices(size(), -1);
    std::vector<VariableControlledSourceSet> supersets;
    VariableControlledSourceSet currentSet(size());

    // Initialize indices for fixed values in sourceSet
    for (size_t i = 0; i < size(); ++i) {
      if (sourceSet[i].has_value()) {
        auto it = std::find(sourceStates[i].begin(), sourceStates[i].end(),
                            sourceSet[i].value());
        assert(it != sourceStates[i].end());
        currentIndices[i] = std::distance(sourceStates[i].begin(), it);
      }
    }

    while (true) {
      // Construct the current superset
      for (size_t i = 0; i < size(); ++i) {
        currentSet.setSourceSet(
            i, currentIndices[i] != -1
                   ? std::make_optional(sourceStates[i][currentIndices[i]])
                   : std::nullopt);
      }
      supersets.push_back(currentSet);

      // Increment indices to generate the next superset
      bool incremented = false;
      for (int i = static_cast<int>(size()) - 1; i >= 0; --i) {
        if (!sourceSet[i].has_value()) {
          if (++currentIndices[i] < static_cast<int>(sourceStates[i].size())) {
            incremented = true;
            break;
          } else {
            currentIndices[i] = -1;
          }
        }
      }
      if (!incremented)
        break;
    }

    return supersets;
  }

  [[nodiscard]] size_t hash() const noexcept {
    size_t h = 0;
    for (const auto& source : sourceSet) {
      common::hashCombine(
          h, source.has_value() ? source.value() : static_cast<IDType>(-1));
    }
    return h;
  }

  VariableControlledSourceSet() noexcept = default;

 private:
  std::vector<std::optional<IDType>> sourceSet;
  bool valid = true;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(sourceSet, valid);
  }
};

}  // namespace foresighter::implementation::basics

//std hash function for VariableControlledSourceSet
template <>
struct std::hash<
    foresighter::implementation::basics::VariableControlledSourceSet> {
  [[nodiscard]] std::size_t operator()(
      const foresighter::implementation::basics::VariableControlledSourceSet&
          sourceSet) const noexcept {
    return sourceSet.hash();
  }
};