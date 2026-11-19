/**
 * @file slice.hpp
 * @brief Slice A_e: a sub-FTA extracted for a given input-output example.
 *
 * Wraps an FTA together with a mapping from local state IDs back to global
 * (original FTA) state IDs. Produced by the online Slice phase of presynthesis
 * and consumed by the Search phase to find a concrete program.
 */
#pragma once
#include <cereal/access.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/implementation/basics/types.hpp>
#include <vector>

namespace foresighter::implementation::basics {
template <class FTAType>
class Slice {
 public:
  Slice() : fta(), statesToGlobalStates() {}

  Slice(FTAType&& fta) : fta(std::move(fta)) {
    for (auto state : fta.getStates()) {
      statesToGlobalStates[state] = state;
    }
  }

  Slice(FTAType&& fta,
        emhash8hashmap::HashMap<IDType, IDType>&& statesToGlobalStates)
      : fta(std::move(fta)),
        statesToGlobalStates(std::move(statesToGlobalStates)) {}

  [[nodiscard]] const FTAType& getFTA() const { return fta; }

  [[nodiscard]] IDType getGlobalState(IDType stateID) const {
    return statesToGlobalStates.find(stateID)->second;
  }

 private:
  FTAType fta;
  emhash8hashmap::HashMap<IDType, IDType> statesToGlobalStates;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(fta, statesToGlobalStates);
  }
};
}  // namespace foresighter::implementation::basics
