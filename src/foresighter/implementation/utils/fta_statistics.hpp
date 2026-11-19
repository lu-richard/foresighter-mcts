/**
* @file fta_statistics.hpp
* @brief 
* @author Rui Dong
* @date 4/29/24
*/

#pragma once
#include <foresighter/common/hashmap.hpp>

namespace foresighter::implementation::utils {
template <class F>
std::string getFTAStatistics(const F& fta) {
  auto& stateManager = fta.getStateManager();
  std::string result;
  result += "FTA Statistics:\n";
  result +=
      "FTA Number Of States: " + std::to_string(fta.getStates().size()) + "\n";
  result += "FTA Number Of Transitions: " +
            std::to_string(fta.getTransitions().size()) + "\n";
  result +=
      "FTA Number Of Symbols: " + std::to_string(fta.getSymbols().size()) +
      "\n";
  result += "---States Number By Symbol---\n";
  for (const auto& symbol : fta.getSymbols()) {
    result += "Symbol: " + symbol.toString() + " Number Of States: " +
              std::to_string(fta.getStatesBySymbol(symbol).size()) + "\n";
  }
  emhash8hashmap::HashMap<typename F::SymbolType, size_t> transitions_by_tail;
  emhash8hashmap::HashMap<typename F::SymbolType, size_t> transitions_by_head;
  for (auto state_id : fta.getStates()) {
    for (auto transition_id : fta.getTransitionsByTail(state_id)) {
      transitions_by_tail[stateManager.read(state_id).getSymbol()]++;
    }
    for (auto transition_id : fta.getTransitionsByHead(state_id)) {
      transitions_by_head[stateManager.read(state_id).getSymbol()]++;
    }
  }
  result += "---Transitions Number By Type of Tail---\n";
  for (const auto& [symbol, count] : transitions_by_tail) {
    result += "Symbol: " + symbol.toString() +
              " Number Of Transitions: " + std::to_string(count) + "\n";
  }
  result += "---Transitions Number By Type of Head---\n";
  for (const auto& [symbol, count] : transitions_by_head) {
    result += "Symbol: " + symbol.toString() +
              " Number Of Transitions: " + std::to_string(count) + "\n";
  }

  return result;
}
}  // namespace foresighter::implementation::utils
