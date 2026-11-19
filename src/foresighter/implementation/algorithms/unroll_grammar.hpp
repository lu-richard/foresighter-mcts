/**
* @file unroll_grammar.hpp
* @brief 
* @author Rui Dong
* @date 4/30/24
*/

#pragma once
#include <spdlog/spdlog.h>
#include <foresighter/common/cartesian_product.hpp>
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/hashset.hpp>
#include <foresighter/implementation/algorithms/grammar_topo_sort.hpp>
#include <foresighter/implementation/basics/grammar.hpp>
#include <variant>
#include <vector>
#include "foresighter/implementation/sql/symbol/op_filter_and.hpp"
#include "foresighter/implementation/sql/symbol/op_filter_or.hpp"
#include "foresighter/implementation/sql/symbol/op_group_by_order_by_count_limit.hpp"
#include "foresighter/implementation/sql/symbol/op_inner_join.hpp"
#include "foresighter/implementation/sql/symbol/op_left_join.hpp"
#include "foresighter/implementation/sql/symbol/op_projection.hpp"
#include "foresighter/implementation/sql/symbol/op_select.hpp"

namespace foresighter::implementation::algorithms {

template <class Symbol>
class SymbolWithUnrollInfo {
 public:
  SymbolWithUnrollInfo() = default;

  explicit SymbolWithUnrollInfo(Symbol symbol) : s(symbol) {}

  [[nodiscard]] bool isNonTerminal() const { return s.isNonTerminal(); }

  [[nodiscard]] bool isConstant() const { return s.isConstant(); }

  [[nodiscard]] bool isVariable() const { return s.isVariable(); }

  [[nodiscard]] bool isOperator() const { return s.isOperator(); }

  [[nodiscard]] std::string toString() const {
    std::string result;
    result += s.toString();
    result += "{";
    result += std::to_string(cost);
    result += "}";
    return result;
  };

  void setCost(size_t cost) { this->cost = cost; }

  [[nodiscard]] size_t getCost() const { return cost; }

  void setFilterUnrollTimes(size_t filterUnrollTimes) {
    this->filterUnrollTimes = filterUnrollTimes;
  }

  void setJoinUnrollTimes(size_t joinUnrollTimes) {
    this->joinUnrollTimes = joinUnrollTimes;
  }

  [[nodiscard]] size_t getFilterUnrollTimes() const {
    return filterUnrollTimes;
  }

  void setFilterAndUnrollTimes(size_t filterAndUnrollTimes) {
    this->filterAndUnrollTimes = filterAndUnrollTimes;
  }

  void setFilterOrUnrollTimes(size_t filterOrUnrollTimes) {
    this->filterOrUnrollTimes = filterOrUnrollTimes;
  }

  [[nodiscard]] size_t getFilterAndUnrollTimes() const {
    return filterAndUnrollTimes;
  }

  [[nodiscard]] size_t getFilterOrUnrollTimes() const {
    return filterOrUnrollTimes;
  }

  [[nodiscard]] size_t getJoinUnrollTimes() const { return joinUnrollTimes; }

  [[nodiscard]] Symbol getSymbol() const { return s; }

  bool operator==(const SymbolWithUnrollInfo& other) const {
    return s == other.s and cost == other.cost and
           filterUnrollTimes == other.filterUnrollTimes and
           joinUnrollTimes == other.joinUnrollTimes and
           filterAndUnrollTimes == other.filterAndUnrollTimes and
           filterOrUnrollTimes == other.filterOrUnrollTimes;
  }

  [[nodiscard]] size_t hash() const {
    //a hash that doesn't matter for hash map key order
    size_t h = 0;
    foresighter::common::hashCombine(h, s, cost, filterUnrollTimes,
                                     joinUnrollTimes, filterAndUnrollTimes,
                                     filterOrUnrollTimes);
    return h;
  }

  [[nodiscard]] uint32_t getUnrollingIndex() const { return 0; }

 private:
  //the symbol
  Symbol s;
  size_t cost = 0;
  size_t filterUnrollTimes = 0;
  size_t joinUnrollTimes = 0;
  size_t filterAndUnrollTimes = 0;
  size_t filterOrUnrollTimes = 0;
};
}  // namespace foresighter::implementation::algorithms

//hash function for SymbolWithExpansionInfo
namespace std {
template <typename Symbol>
struct hash<
    foresighter::implementation::algorithms::SymbolWithUnrollInfo<Symbol>> {
  size_t operator()(
      const foresighter::implementation::algorithms::SymbolWithUnrollInfo<
          Symbol>& op) const {
    return op.hash();
  }
};
}  // namespace std

namespace foresighter::implementation::algorithms {
class UnrollLimitSetting {
 public:
  UnrollLimitSetting() = delete;

  UnrollLimitSetting(size_t filterUnrollTimes, size_t joinUnrollTimes,
                     size_t costLimit)
      : filterUnrollTimes(filterUnrollTimes),
        joinUnrollTimes(joinUnrollTimes),
        costLimit(costLimit) {}

  [[nodiscard]] bool isLimitExceeded(auto& symbolWithUnrollInfo) const {
    if (symbolWithUnrollInfo.getFilterUnrollTimes() > filterUnrollTimes) {
      return true;
    }
    if (symbolWithUnrollInfo.getJoinUnrollTimes() > joinUnrollTimes) {
      return true;
    }
    if (symbolWithUnrollInfo.getCost() > costLimit) {
      return true;
    }
    if (symbolWithUnrollInfo.getFilterOrUnrollTimes() +
            symbolWithUnrollInfo.getFilterAndUnrollTimes() >
        2) {
      return true;
    }
    return false;
  }

 private:
  size_t filterUnrollTimes = 0;
  size_t joinUnrollTimes = 0;
  size_t costLimit = 0;
};

/**
 * @brief Convert a grammar to a DAG, by unrolling the symbols
 * @param grammar
 * @param setting
 * @return a new grammar that is a DAG
 */
template <class G, class S = G::SymbolType>
G convertToDAG(const G& grammar, const UnrollLimitSetting& unrollLimitSetting) {
  //the procedure of building a DAG is performed in a bottom-up manner
  //we will assign each new symbol a ExpansionInfo,
  //which descibes for current info, if expand it, the max times
  //each non-terminals can be expanded.
  //we will start from all const symbols, and build the grammar in a
  //bottom-up manner, until saturation.
  using GrammarWithUnrollInfo = basics::Grammar<SymbolWithUnrollInfo<S>>;
  using SymbolBankType = emhash8hashmap::HashMap<
      S, emhash8hashset::HashSet<typename GrammarWithUnrollInfo::SymbolType>>;

  auto getInitialBank = [](const G& grammar) {
    //the initial bank consists of all terminals
    //for each terminal, if it is interested, we record it
    //otherwise, we set all expansion times to 0
    SymbolBankType bank;
    for (const auto& terminalSymbol : grammar.getTerminals()) {
      SymbolWithUnrollInfo<S> symbolToBeInserted(terminalSymbol);
      bank[terminalSymbol].insert(symbolToBeInserted);
    }
    return bank;
  };

  auto isProductionActivated = [](const auto& prod,
                                  const SymbolBankType& bankBeforeLastRound,
                                  const SymbolBankType& bankLastRound) {
    bool allArgsInBank = true;
    bool haveAtLeastOneArgFromLastRound = false;
    for (auto& arg : prod.getRhs()) {
      if (bankLastRound.find(arg) == bankLastRound.end() &&
          bankBeforeLastRound.find(arg) == bankBeforeLastRound.end()) {
        allArgsInBank = false;
        break;
      }
      if (bankLastRound.find(arg) != bankLastRound.end()) {
        haveAtLeastOneArgFromLastRound = true;
      }
    }
    return allArgsInBank && haveAtLeastOneArgFromLastRound;
  };

  auto generateAllArgCombinations = [](const auto& prod,
                                       const SymbolBankType&
                                           bankBeforeLastRound,
                                       const SymbolBankType& bankLastRound) {
    std::vector<std::vector<SymbolWithUnrollInfo<S>>> result;

    std::vector<std::vector<SymbolWithUnrollInfo<S>>> argsFromBeforeLastRound;
    std::vector<std::vector<SymbolWithUnrollInfo<S>>> argsFromLastRound;

    for (auto& arg : prod.getRhs()) {
      if (bankLastRound.find(arg) != bankLastRound.end()) {
        argsFromLastRound.push_back(
            {bankLastRound.at(arg).begin(), bankLastRound.at(arg).end()});
      } else {
        argsFromLastRound.push_back({});
      }
      if (bankBeforeLastRound.find(arg) != bankBeforeLastRound.end()) {
        argsFromBeforeLastRound.push_back({bankBeforeLastRound.at(arg).begin(),
                                           bankBeforeLastRound.at(arg).end()});
      } else {
        argsFromBeforeLastRound.push_back({});
      }
    }
    //now we have all args, we can combine them
    for (int i = 1; i <= (1 << prod.getRhs().size()) - 1; ++i) {
      //use the binary representation of i to decide which args to use.
      //1 means use the arg from last round, 0 means use the arg from before last round
      int bitMask = i;
      //we first see if any of the argument is empty. if so, we skip this
      // combination.
      bool skip = false;
      for (int j = 0; j < prod.getRhs().size(); ++j) {
        if (bitMask & (1 << j)) {
          if (argsFromLastRound[j].empty()) {
            skip = true;
            break;
          }
        } else {
          if (argsFromBeforeLastRound[j].empty()) {
            skip = true;
            break;
          }
        }
      }
      if (skip) {
        continue;
      }
      //not skipped, means we can generate new arg combination
      std::vector<std::vector<SymbolWithUnrollInfo<S>>> toCartesian;
      for (int j = 0; j < prod.getRhs().size(); ++j) {
        if (bitMask & (1 << j)) {
          toCartesian.push_back(argsFromLastRound[j]);
        } else {
          toCartesian.push_back(argsFromBeforeLastRound[j]);
        }
      }
      //now we have all args, we can do a cartesian product
      auto cartesianResult = common::cartesianProduct(toCartesian);
      result.insert(result.end(), cartesianResult.begin(),
                    cartesianResult.end());
    }
    return result;
  };

  auto bottomUpBuildDAGGrammarWithExpansionInfo = [isProductionActivated,
                                                   generateAllArgCombinations,
                                                   getInitialBank](
                                                      const G& grammar,
                                                      const UnrollLimitSetting&
                                                          unrollLimitSetting) {
    GrammarWithUnrollInfo resultGrammar;
    SymbolBankType bankBeforeLastRound;
    SymbolBankType bankLastRound = getInitialBank(grammar);
    while (!bankLastRound.empty()) {
      SymbolBankType bankThisRoundGenerated;
      for (auto& prod : grammar.getProductions()) {
        //for each production, we try to see if this production is
        //activated for expansion.
        //it is activated if
        //1) all symbols on the rhs are in the bank
        //2) at least one symbol is generated in the last round
        if (!isProductionActivated(prod, bankBeforeLastRound, bankLastRound)) {
          continue;
        }
        //if activated, then we find all argument combinations
        std::vector<std::vector<SymbolWithUnrollInfo<S>>> argCombinations =
            generateAllArgCombinations(prod, bankBeforeLastRound,
                                       bankLastRound);
        //now we have all arg combinations, we can generate new symbols
        for (auto& argComb : argCombinations) {
          SymbolWithUnrollInfo<S> newSymbolInfo(prod.getLhs());
          //update cost
          size_t newCost = 0;
          for (auto& arg : argComb) {
            newCost += arg.getCost();
          }
          newCost += prod.getCost();

          newSymbolInfo.setCost(newCost);

          //no filter is allowed outside of join
          if (std::holds_alternative<sql::OpSelect>(prod.getOp().getSymbol())) {
            bool hasJoin = false;
            for (auto& arg : argComb) {
              if (arg.getJoinUnrollTimes() > 0) {
                hasJoin = true;
                break;
              }
            }
            if (hasJoin) {
              continue;
            }
          }

          //update filter times
          size_t newFilterTimes = 0;
          for (auto& arg : argComb) {
            newFilterTimes += arg.getFilterUnrollTimes();
          }
          if (std::holds_alternative<sql::OpSelect>(prod.getOp().getSymbol())) {
            newFilterTimes += 1;
          }
          newSymbolInfo.setFilterUnrollTimes(newFilterTimes);

          //update filterAnd times
          size_t newFilterAndTimes = 0;
          for (auto& arg : argComb) {
            newFilterAndTimes += arg.getFilterAndUnrollTimes();
          }
          if (std::holds_alternative<sql::OpFilterAnd>(
                  prod.getOp().getSymbol())) {
            newFilterAndTimes += 1;
          }
          newSymbolInfo.setFilterAndUnrollTimes(newFilterAndTimes);

          //update filterOr times
          size_t newFilterOrTimes = 0;
          for (auto& arg : argComb) {
            newFilterOrTimes += arg.getFilterOrUnrollTimes();
          }
          if (std::holds_alternative<sql::OpFilterOr>(
                  prod.getOp().getSymbol())) {
            newFilterOrTimes += 1;
          }
          newSymbolInfo.setFilterOrUnrollTimes(newFilterOrTimes);

          //update join times
          size_t newJoinTimes = 0;
          for (auto& arg : argComb) {
            newJoinTimes += arg.getJoinUnrollTimes();
          }
          if (std::holds_alternative<sql::OpInnerJoin>(
                  prod.getOp().getSymbol()) ||
              std::holds_alternative<sql::OpLeftJoin>(
                  prod.getOp().getSymbol())) {
            newJoinTimes += 1;
          }
          newSymbolInfo.setJoinUnrollTimes(newJoinTimes);

          //update join times
          if (unrollLimitSetting.isLimitExceeded(newSymbolInfo)) {
            continue;
          }

          //now, we can add the new symbol to the bank
          //and add the new production to the grammar
          std::vector<SymbolWithUnrollInfo<S>> newRhs{argComb.begin(),
                                                      argComb.end()};

          resultGrammar.addProduction(newSymbolInfo,
                                      SymbolWithUnrollInfo<S>(prod.getOp()),
                                      newRhs, prod.getCost());
          //update bank for newSymbolInfo.
          bool existInBeforeLastRound = false;
          bool existInLastRound = false;
          if (bankBeforeLastRound.find(newSymbolInfo.getSymbol()) !=
              bankBeforeLastRound.end()) {
            if (bankBeforeLastRound[newSymbolInfo.getSymbol()].find(
                    newSymbolInfo) !=
                bankBeforeLastRound[newSymbolInfo.getSymbol()].end()) {
              existInBeforeLastRound = true;
            }
          }
          if (bankLastRound.find(newSymbolInfo.getSymbol()) !=
              bankLastRound.end()) {
            if (bankLastRound[newSymbolInfo.getSymbol()].find(newSymbolInfo) !=
                bankLastRound[newSymbolInfo.getSymbol()].end()) {
              existInLastRound = true;
            }
          }
          //if both not existed, then we add it to this round generated
          bankThisRoundGenerated[newSymbolInfo.getSymbol()].insert(
              newSymbolInfo);
        }
      };
      //combine last round and before last round to before last round
      for (auto& [k, v] : bankLastRound) {
        bankBeforeLastRound[k].insert(v.begin(), v.end());
      }
      //update last round
      bankLastRound = bankThisRoundGenerated;
    }
    return resultGrammar;
  };

  auto generateCleanedGrammar = [](const GrammarWithUnrollInfo& grammar,
                                   const S& startSymbol) {
    basics::Grammar<S> result;
    //cleaned grammar is a DAG such that the symbols
    //are marked with indices, which denotes their topological order
    //in the DAG
    //first, we need to get the topological order
    auto topoOrder = algorithms::grammarTopoSort(grammar);
    emhash8hashmap::HashMap<S, std::vector<SymbolWithUnrollInfo<S>>>
        symbolToCostInfo;
    for (auto& symbol : grammar.getSymbols()) {
      symbolToCostInfo[symbol.getSymbol()].push_back(symbol);
    }
    //sort each vector following the topological order
    for (auto& [k, v] : symbolToCostInfo) {
      std::sort(v.begin(), v.end(), [&topoOrder](const auto& a, const auto& b) {
        return std::find(topoOrder.begin(), topoOrder.end(), a) <
               std::find(topoOrder.begin(), topoOrder.end(), b);
      });
    }
    //then, we build an index translation map
    emhash8hashmap::HashMap<SymbolWithUnrollInfo<S>, uint32_t> translationMap;
    for (auto& s : grammar.getSymbols()) {
      auto index =
          std::distance(symbolToCostInfo[s.getSymbol()].begin(),
                        std::find(symbolToCostInfo[s.getSymbol()].begin(),
                                  symbolToCostInfo[s.getSymbol()].end(), s));
      translationMap[s] = index;
    }
    //then, we build the grammar
    for (auto& p : grammar.getProductions()) {
      std::vector<S> newRhs;
      for (auto& r : p.getRhs()) {
        newRhs.push_back(S(r.getSymbol(), translationMap[r]));
      }
      S newLhs = S(p.getLhs().getSymbol(), translationMap[p.getLhs()]);
      S newOp = S(p.getOp().getSymbol());
      result.addProduction(newLhs, newOp, newRhs, p.getCost());
    }

    //last, we set start symbol. The start symbol are symbols with the
    //NtQuery symbol
    for (auto& s : result.getNonTerminals()) {
      if (s.getSymbol() == startSymbol.getSymbol()) {
        result.addToStartSymbols(s);
      }
    }
    return result;
  };

  //this grammar is a temp grammar that each symbol is associated with
  //an expansion info
  GrammarWithUnrollInfo tmpGrammar =
      bottomUpBuildDAGGrammarWithExpansionInfo(grammar, unrollLimitSetting);
  return generateCleanedGrammar(tmpGrammar, grammar.getStartSymbols()[0]);
}
}  // namespace foresighter::implementation::algorithms
