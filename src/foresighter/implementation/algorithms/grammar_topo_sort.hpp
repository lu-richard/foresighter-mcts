/**
* @file grammar_topo_sort.hpp
* @brief 
* @author Rui Dong
* @date 4/30/24
*/

#pragma once
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashset.hpp>
#include <vector>

namespace foresighter::implementation::algorithms {

/**
 * @brief Get all symbols in the grammar in topological order
 * @param grammar
 * @return a list of symbols in topological order
 * @pre the Grammar must be a DAG
 */
template <class GrammarType>
std::vector<typename GrammarType::SymbolType> grammarTopoSort(
    const GrammarType& grammar) {
  using SymbolType = typename GrammarType::SymbolType;
  std::vector<SymbolType> result;
  result.reserve(grammar.getNonTerminals().size());

  emhash8hashset::HashSet<SymbolType> visited;

  emhash8hashset::HashSet<SymbolType> inStack;

  auto dfs = [&](auto&& self, const SymbolType& u) -> void {
    if (visited.contains(u)) {
      return;
    }
    visited.insert(u);
    inStack.insert(u);

    const auto& productions = grammar.getProductions(u);
    for (const auto& p : productions) {
      const auto& rhs = p.getRhs();
      for (const auto& v : rhs) {
        if (inStack.contains(v)) {
          throw foresighter::common::InvalidArgument(
              "Grammar is not a DAG, there is a cycle in the grammar: "
              "production:" +
              p.toString() + ", symbol recursive: " + v.toString());
        }
        self(self, v);
      }
    }
    inStack.erase(u);
    result.push_back(u);
  };

  const auto& nonTerminals = grammar.getNonTerminals();
  for (const auto& u : nonTerminals) {
    dfs(dfs, u);
  }
  return result;
}
}  // namespace foresighter::implementation::algorithms