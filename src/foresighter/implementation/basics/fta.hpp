#pragma once
/**
 * @file fta.hpp
 * @brief Finite Tree Automaton A = (Q, F, Q_f, Delta) -- the core data structure of presynthesis.
 *
 * Template class FTA<SymbolType, ValueType> stores states (Q), final states (Q_f),
 * and transitions (Delta), indexed by symbol, head, and tail for efficient lookup.
 * Built offline by BuildOfflineFTA and sliced online per example.
 * See Definition 3.1 in the paper.
 */
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/hashset.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/basics/state.hpp>
#include <foresighter/implementation/basics/transition.hpp>

#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>

namespace foresighter::implementation::basics {

template <class SymbolType, class ValueType>
class FTA {
 public:
  using IDType = foresighter::implementation::basics::IDType;
  using StateType = State<SymbolType, ValueType>;
  using TransitionType = Transition<SymbolType>;
  using StateManagerType = EntityManager<StateType>;
  using TransitionManagerType = EntityManager<TransitionType>;

  FTA() = default;

  [[nodiscard]] std::vector<IDType> getFinalStates() const {
    std::vector<IDType> finalStates_vec;
    finalStates_vec.reserve(finalStates.size());
    for (const auto finalState : finalStates) {
      finalStates_vec.push_back(finalState);
    }
    return finalStates_vec;
  }

  void setStateManager(StateManagerType&& stateManager) noexcept {
    this->stateManager = std::move(stateManager);
  }

  void setTransitionManager(
      TransitionManagerType&& transitionManager) noexcept {
    this->transitionManager = std::move(transitionManager);
  }

  void markFinalState(const IDType stateID) {
    if (!hasState(stateID)) {
      throw common::InvalidArgument(
          "stateID is not in the FTA. Cannot mark it as final state.");
    }
    finalStates.insert(stateID);
  }

  void unmarkFinalState(const IDType stateID) {
    if (!hasState(stateID)) {
      throw common::InvalidArgument(
          "stateID is not in the FTA. Cannot unmark it as final state.");
    }
    finalStates.erase(stateID);
  }

  bool addState(const IDType stateID) {
    auto [_, inserted] = states.insert(stateID);
    if (!inserted) {
      return false;
    }
    const auto& symbol = stateManager.read(stateID).getSymbol();
    statesBySymbol[symbol].push_back(stateID);
    states_vec.push_back(stateID);
    return true;
  }

  bool addTransition(const IDType transitionID) {
    auto [_, inserted] = transitions.insert(transitionID);
    if (!inserted) {
      return false;
    }
    const auto& transition = transitionManager.read(transitionID);
    const auto head = transition.getHead();
    const auto& tails = transition.getTails();
    transitionHeadMap[head].push_back(transitionID);
    for (const auto tail : tails) {
      transitionTailMap[tail].push_back(transitionID);
    }
    transitions_vec.push_back(transitionID);
    return true;
  }

  std::vector<SymbolType> getSymbols() const {
    std::vector<SymbolType> symbols;
    symbols.reserve(statesBySymbol.size());
    for (const auto& [symbol, _] : statesBySymbol) {
      symbols.push_back(symbol);
    }
    return symbols;
  }

  [[nodiscard]] const std::vector<IDType>& getStatesBySymbol(
      const SymbolType& symbol) const noexcept {
    if (auto it = statesBySymbol.find(symbol); it != statesBySymbol.end()) {
      return it->second;
    }
    return emptyVector;
  }

  [[nodiscard]] const std::vector<IDType>& getTransitionsByHead(
      const IDType head) const noexcept {
    if (auto it = transitionHeadMap.find(head); it != transitionHeadMap.end()) {
      return it->second;
    }
    return emptyVector;
  }

  [[nodiscard]] const std::vector<IDType>& getTransitionsByTail(
      const IDType tail) const noexcept {
    if (auto it = transitionTailMap.find(tail); it != transitionTailMap.end()) {
      return it->second;
    }
    return emptyVector;
  }

  [[nodiscard]] const std::vector<IDType>& getStates() const noexcept {
    return states_vec;
  }

  [[nodiscard]] const std::vector<IDType>& getTransitions() const noexcept {
    return transitions_vec;
  }

  [[nodiscard]] bool hasState(const IDType stateID) const noexcept {
    return states.contains(stateID);
  }

  [[nodiscard]] const EntityManager<State<SymbolType, ValueType>>&
  getStateManager() const noexcept {
    return stateManager;
  }

  [[nodiscard]] const EntityManager<Transition<SymbolType>>&
  getTransitionManager() const noexcept {
    return transitionManager;
  }

  [[nodiscard]] EntityManager<State<SymbolType, ValueType>>&
  getStateManager() noexcept {
    return stateManager;
  }

  [[nodiscard]] EntityManager<Transition<SymbolType>>&
  getTransitionManager() noexcept {
    return transitionManager;
  }

  [[nodiscard]] bool isFinalState(const IDType state) const noexcept {
    return finalStates.contains(state);
  }

  void clear() {
    emptyVector.clear();
    states.clear();
    finalStates.clear();
    transitions.clear();
    states_vec.clear();
    transitions_vec.clear();
    statesBySymbol.clear();
    transitionHeadMap.clear();
    transitionTailMap.clear();
    stateManager.clear();
    transitionManager.clear();
  }

 private:
  static std::vector<IDType> emptyVector;
  //a lookup set for all states in the FTA
  emhash8hashset::HashSet<IDType> states;
  emhash8hashset::HashSet<IDType> finalStates;
  //a lookup set for all transitions in the FTA
  emhash8hashset::HashSet<IDType> transitions;
  std::vector<IDType> states_vec;
  std::vector<IDType> transitions_vec;
  //a lookup map for all states in the FTA, indexed by their symbols
  emhash8hashmap::HashMap<SymbolType, std::vector<IDType>> statesBySymbol;
  //a lookup map for all transitions in the FTA, indexed by their heads
  emhash8hashmap::HashMap<IDType, std::vector<IDType>> transitionHeadMap;
  //a lookup map for all transitions in the FTA, indexed by their tails
  emhash8hashmap::HashMap<IDType, std::vector<IDType>> transitionTailMap;

  EntityManager<State<SymbolType, ValueType>> stateManager;
  EntityManager<Transition<SymbolType>> transitionManager;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(states, transitions, states_vec, transitions_vec, statesBySymbol,
       transitionHeadMap, transitionTailMap, stateManager, transitionManager,
       finalStates);
  }
};

template <class SymbolType, class ValueType>
std::vector<typename FTA<SymbolType, ValueType>::IDType>
    FTA<SymbolType, ValueType>::emptyVector;

}  // namespace foresighter::implementation::basics