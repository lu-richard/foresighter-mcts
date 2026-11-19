#pragma once
/**
 * @file grammar.hpp
 * @brief Context-free grammar G = (V, Sigma, R, s0) for program synthesis.
 *
 * Template class Grammar<SymbolType> stores productions, start symbols, and classifies
 * symbols into non-terminals, operators, constants, and variables. Provides topological
 * sort over non-terminals for bottom-up FTA construction.
 */

#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/implementation/algorithms/grammar_topo_sort.hpp>
#include <foresighter/implementation/basics/production.hpp>

#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>
#include "foresighter/implementation/basics/types.hpp"

namespace foresighter::implementation::basics {
template <class SymbolTypeArg>
class Grammar {
 public:
  using ProductionType = Production<SymbolTypeArg>;
  using SymbolType = SymbolTypeArg;

  Grammar() = default;

  const std::vector<SymbolType>& getStartSymbols() const {
    return startSymbols;
  }

  const std::vector<ProductionType>& getProductions(
      const SymbolType& symbol) const {
    auto it = symbolToProductions.find(symbol);
    if (it == symbolToProductions.end()) {
      return emptyProductions;
    }
    return it->second;
  }

  const std::vector<ProductionType>& getProductions() const {
    return productions;
  }

  const std::vector<SymbolType>& getNonTerminals() const {
    return nonTerminalSymbols;
  }

  const std::vector<SymbolType>& getTerminals() const {
    return terminalSymbols;
  }

  const std::vector<SymbolType>& getConstants() const { return constSymbols; }

  const std::vector<SymbolType>& getOperators() const {
    return operatorSymbols;
  }

  const std::vector<SymbolType>& getSymbols() const { return allSymbols; }

  const std::vector<SymbolType>& getVariables() const { return varSymbols; }

  void addProduction(SymbolType lhs, SymbolType op, std::vector<SymbolType> rhs,
                     WeightType cost) {
    ProductionType production(lhs, op, rhs, cost);
    addNewProduction(production);
  }

  void addToStartSymbols(SymbolType symbol) {
    if (symbolToProductions.find(symbol) == symbolToProductions.end()) {
      throw foresighter::common::InvalidArgument(
          "Start symbol must be "
          "a non-terminal symbol");
    }
    startSymbols.push_back(symbol);
  }

  [[nodiscard]] std::string toString() const {
    std::string result;
    for (const auto& production : productions) {
      result += production.toString() + "\n";
    }
    return result;
  }

  [[nodiscard]] std::vector<SymbolType> getTopoSortedOrder() const {
    return algorithms::grammarTopoSort(*this);
  }

  [[nodiscard]] std::string getTopoSortedString() const {
    auto topoSorted = algorithms::grammarTopoSort(*this);
    //reverse the order
    std::reverse(topoSorted.begin(), topoSorted.end());
    std::string result;
    for (const auto& symbol : topoSorted) {
      if (!symbol.isNonTerminal()) {
        continue;
      }
      result += symbol.toString() + " := ";
      for (const auto& production : getProductions(symbol)) {
        result += production.getOp().toString() + "(";
        for (const auto& rhs : production.getRhs()) {
          result += rhs.toString() + ", ";
        }
        result.pop_back();
        result.pop_back();
        result += ")";
        result += " | ";
      }
      result.pop_back();
      result.pop_back();
      result += "\n";
    }
    //print start symbols
    result += "Start Symbols: ";
    for (const auto& symbol : startSymbols) {
      result += symbol.toString() + ", ";
    }
    result.pop_back();
    result.pop_back();
    result += "\n";
    return result;
  }

 private:
  void addNewProduction(const ProductionType& production) {
    //check validity: LHS must be non-terminal
    if (!production.getLhs().isNonTerminal()) {
      throw foresighter::common::InvalidArgument(
          "LHS of a production must be a non-terminal symbol");
    }
    //check if previously added
    if (std::find(productions.begin(), productions.end(), production) !=
        productions.end()) {
      return;
    }
    //if previously not added, add
    productions.push_back(production);
    symbolToProductions[production.getLhs()].push_back(production);
    //gather all symbols
    std::vector<SymbolType> symbols;
    symbols.push_back(production.getLhs());
    for (const auto& symbol : production.getRhs()) {
      symbols.push_back(symbol);
    }
    symbols.push_back(production.getOp());
    //for each symbol, add to corresponding list if not already added
    for (const auto& symbol : symbols) {
      if (std::find(allSymbols.begin(), allSymbols.end(), symbol) !=
          allSymbols.end()) {
        continue;
      }
      allSymbols.push_back(symbol);
      if (symbol.isConstant()) {
        constSymbols.push_back(symbol);
        terminalSymbols.push_back(symbol);
      }
      if (symbol.isOperator()) {
        operatorSymbols.push_back(symbol);
        terminalSymbols.push_back(symbol);
      }
      if (symbol.isVariable()) {
        varSymbols.push_back(symbol);
        terminalSymbols.push_back(symbol);
      }
      if (symbol.isNonTerminal()) {
        nonTerminalSymbols.push_back(symbol);
      }
    }
  }

  emhash8hashmap::HashMap<SymbolType, std::vector<ProductionType>>
      symbolToProductions;
  std::vector<SymbolType> startSymbols;
  std::vector<SymbolType> nonTerminalSymbols;
  std::vector<SymbolType> constSymbols;
  std::vector<SymbolType> operatorSymbols;
  std::vector<SymbolType> varSymbols;
  std::vector<ProductionType> productions;
  std::vector<SymbolType> allSymbols;
  std::vector<SymbolType> terminalSymbols;
  static std::vector<ProductionType> emptyProductions;

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(symbolToProductions, startSymbols, nonTerminalSymbols, constSymbols,
            operatorSymbols, varSymbols, productions, allSymbols,
            terminalSymbols);
  }
};

template <class SymbolTypeArg>
std::vector<Production<SymbolTypeArg>>
    Grammar<SymbolTypeArg>::emptyProductions = {};

}  // namespace foresighter::implementation::basics
