
#pragma once
/**
 * @file offline_fta_construct.hpp
 * @brief BuildOfflineFTA: constructs FTA A from grammar G and abstract transformers (Fig. 5).
 *
 * Iterates over grammar symbols in topological order. For terminals, generates initial
 * states from abstract values. For non-terminals, applies abstract transformers to all
 * combinations of child states to produce transitions and new states, up to a cost limit.
 * Handles SQL-specific lazy evaluation for column-index and predicate sub-grammars.
 */

#include <spdlog/spdlog.h>
#include <algorithm>
#include <foresighter/common/cartesian_product.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/sql/constants.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "foresighter/common/variant.hpp"
#include "foresighter/implementation/basics/fta.hpp"
#include "foresighter/implementation/basics/production.hpp"
#include "foresighter/implementation/basics/types.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_select_interval_columns.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/grammar_generator.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"
#include "foresighter/implementation/sql/symbol.hpp"
#include "foresighter/implementation/sql/symbol/nt_aggregation.hpp"
#include "foresighter/implementation/sql/symbol/nt_aggregation_list.hpp"
#include "foresighter/implementation/sql/symbol/nt_column_index.hpp"
#include "foresighter/implementation/sql/symbol/nt_column_index_interval.hpp"
#include "foresighter/implementation/sql/symbol/nt_order_by_list.hpp"
#include "foresighter/implementation/sql/symbol/op_identity.hpp"
#include "foresighter/implementation/sql/symbol/op_inner_join.hpp"
#include "foresighter/implementation/sql/symbol/op_left_join.hpp"
#include "foresighter/implementation/sql/symbol/op_projection.hpp"

namespace {

using IDType = foresighter::implementation::basics::IDType;
using namespace foresighter::implementation::sql;

struct NewFTAAddResult {
  IDType state;
  IDType transition;

  NewFTAAddResult(IDType state, IDType transition)
      : state(state), transition(transition) {}
};

template <class SemanticsType, class P, class FtaType>
std::vector<NewFTAAddResult> buildTransitionForFTA(
    const P& production, [[maybe_unused]] FtaType& fta,
    const std::vector<IDType>& argStates, SemanticsType& semantics,
    const std::shared_ptr<spdlog::logger>& logger, int costLimit, int numOfPredicates) {
  //first, get value from states
  std::vector<typename SemanticsType::ValueType> argValues;
  for (auto& state : argStates) {
    argValues.push_back(fta.getStateManager().read(state).getValue());
  }
  //then get the new value
  auto results = semantics.transform(production.getOp(), argValues, numOfPredicates);
  std::vector<NewFTAAddResult> answer;
  for (auto result : results) {
    auto& newValue = result.first;
    auto& order = result.second;
    //if newValue is bottom, we should return nullopt
    if (SemanticsType::LatticeType::isBottom(newValue)) {
      continue;
    }
    int newCost = 0;
    for (auto& state : argStates) {
      newCost += fta.getStateManager().read(state).getCost();
    }
    newCost += production.getCost();

    //if newCost is greater than maxExactDistance, we should not add this transition.
    if (newCost > costLimit) {
      continue;
    }

    auto newState =
        fta.getStateManager().create(production.getLhs(), newValue, newCost);
    auto newTransition = fta.getTransitionManager().create(
        newState, production.getOp(), argStates, production.getCost(), order);

    answer.emplace_back(newState, newTransition);
  }
  return answer;
}

template <class SemanticsType, class P, class FtaType>
std::set<IDType> dealWithProduction(
    const P& production, [[maybe_unused]] FtaType& fta,
    const std::vector<std::vector<IDType>>& argStates, SemanticsType& semantics,
    const std::shared_ptr<spdlog::logger>& logger, int costLimit, int numOfPredicates) {

  int newlyAddedTransitions = 0;
  auto argCombinations =
      foresighter::common::CartesianProductIterator(argStates);
  std::set<IDType> result;
  while (argCombinations.hasNext()) {
    auto& argStates = argCombinations.next();
    auto newAddResults = buildTransitionForFTA<SemanticsType>(
        production, fta, argStates, semantics, logger, costLimit, numOfPredicates);
    for (const auto& newAddResult : newAddResults) {
      fta.addTransition(newAddResult.transition);
      fta.addState(newAddResult.state);
      result.insert(newAddResult.state);
    }
  }
  return result;
}

template <class FtaType, class SymbolType, class GrammarType,
          class SemanticsType>
std::vector<IDType> buildValidArgsByIntervals(
    const GrammarType& grammar, SemanticsType& semantics, FtaType& fta,
    SymbolType symbol, const std::vector<std::pair<int, int>>& intervals,
    const std::shared_ptr<spdlog::logger>& logger, int costLimit, int numOfPredicates) {

  if (std::holds_alternative<NtAggregationList>(symbol.getSymbol()) ||
      std::holds_alternative<NtProjectionList>(symbol.getSymbol()) ||
      std::holds_alternative<NtGroupByList>(symbol.getSymbol()) ||
      std::holds_alternative<NtOrderedGroupByList>(symbol.getSymbol()) ||
      std::holds_alternative<NtSelectPredicate>(symbol.getSymbol()) ||
      std::holds_alternative<NtAggregation>(symbol.getSymbol()) ||
      std::holds_alternative<NtOrderByList>(symbol.getSymbol())) {
    // list symbols, proceed the intervals to all of its children
    auto productions = grammar.getProductions(symbol);
    std::set<IDType> result;
    for (const auto& production : productions) {
      std::vector<std::vector<IDType>> argStates;
      for (const auto& arg : production.getRhs()) {
        argStates.push_back(buildValidArgsByIntervals(
            grammar, semantics, fta, arg, intervals, logger, costLimit, numOfPredicates));
      }
      auto newStates = dealWithProduction(production, fta, argStates, semantics,
                                          logger, costLimit, numOfPredicates);
      result.insert(newStates.begin(), newStates.end());
    }
    return {result.begin(), result.end()};
  } else if (std::holds_alternative<NtSelectPredicateClause>(
                 symbol.getSymbol())) {
    //NtSelectPredicateClause := OpFilterAnd(NtSelectPredicateClause, NtSelectPredicateClause)
    //NtSelectPredicateClause := OpFilterEq(NtColumnIndexInterval, NtFilterConstantIndex)
    auto& productions = grammar.getProductions(symbol);
    std::set<IDType> result;
    for (const auto& production : productions) {
      std::vector<std::vector<IDType>> argStates;
      if (std::holds_alternative<NtSelectPredicateClause>(
              production.getRhs()[0].getSymbol())) {
        //NtSelectPredicateClause := OpFilterAnd(NtSelectPredicateClause, NtSelectPredicateClause)
        argStates.push_back(buildValidArgsByIntervals(
            grammar, semantics, fta, production.getRhs()[0], intervals, logger,
            costLimit, numOfPredicates));
        argStates.push_back(buildValidArgsByIntervals(
            grammar, semantics, fta, production.getRhs()[1], intervals, logger,
            costLimit, numOfPredicates));
      } else {
        //NtSelectPredicateClause := OpFilterEq(NtColumnIndexInterval, NtFilterConstantIndex)
        argStates.push_back(buildValidArgsByIntervals(
            grammar, semantics, fta, production.getRhs()[0], intervals, logger,
            costLimit, numOfPredicates));
        argStates.push_back(fta.getStatesBySymbol(production.getRhs()[1]));
      }
      auto newStates = dealWithProduction(production, fta, argStates, semantics,
                                          logger, costLimit, numOfPredicates);
      result.insert(newStates.begin(), newStates.end());
    }
    return {result.begin(), result.end()};
  } else if (std::holds_alternative<NtColumnIndexInterval>(
                 symbol.getSymbol())) {
    // interval symbol, return valid states
    auto& states = fta.getStatesBySymbol(symbol);
    std::vector<IDType> result;
    for (const auto& state : states) {
      auto value = fta.getStateManager().read(state).getValue();
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(
          value.getValue()));
      auto predicate =
          std::get<PredicateSelectIntervalColumns>(value.getValue());
      for (const auto& interval : intervals) {
        if (interval.first == predicate.getIntervalL() &&
            interval.second == predicate.getIntervalR()) {
          result.push_back(state);
        }
      }
    }
    return result;
  }
  assert(0);
  return {};
}

template <class FtaType, class SymbolType, class GrammarType,
          class SemanticsType>
std::vector<IDType> buildValidArgsByIntervals(
    const GrammarType& grammar, SemanticsType& semantics, FtaType& fta,
    SymbolType symbol, const std::vector<std::pair<int, int>>& intervals1,
    const std::vector<std::pair<int, int>>& intervals2,
    const std::shared_ptr<spdlog::logger>& logger, int costLimit, int numOfPredicates) {

  if (std::holds_alternative<NtJoinPredicate>(symbol.getSymbol())) {
    // NtJoinPredicate := OpIdentity(NtJoinPredicateClause)
    auto productions = grammar.getProductions(symbol);
    std::set<IDType> result;
    for (const auto& production : productions) {
      std::vector<std::vector<IDType>> argStates;
      for (const auto& arg : production.getRhs()) {
        argStates.push_back(
            buildValidArgsByIntervals(grammar, semantics, fta, arg, intervals1,
                                      intervals2, logger, costLimit, numOfPredicates));
      }
      auto newStates = dealWithProduction(production, fta, argStates, semantics,
                                          logger, costLimit, numOfPredicates);
      result.insert(newStates.begin(), newStates.end());
    }
    return {result.begin(), result.end()};
  } else if (std::holds_alternative<NtJoinPredicateClause>(
                 symbol.getSymbol())) {
    // NtJoinPredicateClause := OpJoinEq(NtColumnIndex, NtColumnIndex)
    auto productions = grammar.getProductions(symbol);
    std::set<IDType> result;
    for (const auto& production : productions) {
      std::vector<std::vector<IDType>> argStates;
      argStates.push_back(buildValidArgsByIntervals(
          grammar, semantics, fta, production.getRhs()[0], intervals1, logger,
          costLimit, numOfPredicates));
      argStates.push_back(buildValidArgsByIntervals(
          grammar, semantics, fta, production.getRhs()[1], intervals2, logger,
          costLimit, numOfPredicates));
      auto newStates = dealWithProduction(production, fta, argStates, semantics,
                                          logger, costLimit, numOfPredicates);
      result.insert(newStates.begin(), newStates.end());
    }
    return {result.begin(), result.end()};
  }
  assert(0);
  return {};
}

}  //anonymous namespace

namespace foresighter::implementation::algorithms {
using IDType = foresighter::implementation::basics::IDType;

template <typename SemanticsType, typename GrammarType>
auto constructFTA(const GrammarType& grammar,
                  const std::optional<sql::SQLBenchmark>& benchmark,
                  const std::shared_ptr<spdlog::logger>& logger, int costLimit,
                  int numOfPredicates) {
  auto timeoutStart = std::chrono::high_resolution_clock::now();

  using ValueType = SemanticsType::ValueType;
  using SymbolType = SemanticsType::OperatorType;
  using FTAType =
      foresighter::implementation::basics::FTA<SymbolType, ValueType>;
  using StateLookupContainer =
      emhash8hashmap::HashMap<SymbolType, emhash8hashset::HashSet<IDType>>;

  FTAType fta;
  SemanticsType semantics;

  std::vector<SymbolType> symbolOrder = grammar.getTopoSortedOrder();
  for (const auto& symbol : symbolOrder) {
    if (!symbol.isNonTerminal()) {  // terminal symbol, generate initial states
      SPDLOG_LOGGER_TRACE(logger, "Processing terminal symbol: {}",
                          symbol.toString());
      auto initialValues =
          semantics.getInitialValues(symbol, benchmark, numOfPredicates);
      SPDLOG_LOGGER_TRACE(logger, "Got values with size {} in the context",
                          initialValues.size());
      for (auto& value : initialValues) {
        auto state = fta.getStateManager().create(symbol, value, 0);
        SPDLOG_LOGGER_TRACE(logger, "State created with id {}", state);
        SPDLOG_LOGGER_TRACE(logger, "State has (symbol, value) = ({}, {})",
                            symbol.toString(), value.toString());
        fta.addState(state);
      }
    } else {  // non-terminal symbol
      SPDLOG_LOGGER_TRACE(logger, "Processing non-terminal symbol: {}",
                          symbol.toString());
      auto productions = grammar.getProductions(symbol);
      for (auto production : productions) {
        auto op = production.getOp().getSymbol();
        if (std::holds_alternative<sql::OpProjection>(op) ||
            std::holds_alternative<sql::OpProjectionDistinct>(op) ||
            std::holds_alternative<sql::OpGroupByOrderByCountLimit>(op) ||
            std::holds_alternative<sql::OpSelect>(op) ||
            std::holds_alternative<sql::OpOrderByColLimit>(op) ||
            std::holds_alternative<sql::OpGroupWithoutKey>(op)) {
          // T --op--> (T, Arg)
          auto tableStates = fta.getStatesBySymbol(production.getRhs()[0]);
          auto argSymbol = production.getRhs()[1];
          for (auto tableState : tableStates) {
            auto tableValue = fta.getStateManager().read(tableState).getValue();
            auto validIntervals =
                semantics.getValidIntervalsFromTable(tableValue);
            auto argStates =
                buildValidArgsByIntervals(grammar, semantics, fta, argSymbol,
                                          validIntervals, logger, costLimit, numOfPredicates);
            dealWithProduction(production, fta, {{tableState}, argStates},
                               semantics, logger, costLimit, numOfPredicates);
          }
        } else if (std::holds_alternative<sql::OpGroupWithKey>(op)) {
          // T --op--> (T, Arg, Arg)
          auto tableStates = fta.getStatesBySymbol(production.getRhs()[0]);
          auto argSymbol1 = production.getRhs()[1];
          auto argSymbol2 = production.getRhs()[2];
          for (auto tableState : tableStates) {
            auto tableValue = fta.getStateManager().read(tableState).getValue();
            auto validIntervals =
                semantics.getValidIntervalsFromTable(tableValue);
            auto argStates1 =
                buildValidArgsByIntervals(grammar, semantics, fta, argSymbol1,
                                          validIntervals, logger, costLimit, numOfPredicates);
            auto argStates2 =
                buildValidArgsByIntervals(grammar, semantics, fta, argSymbol2,
                                          validIntervals, logger, costLimit, numOfPredicates);
            dealWithProduction(production, fta,
                               {{tableState}, argStates1, argStates2},
                               semantics, logger, costLimit, numOfPredicates);
          }
        } else if (std::holds_alternative<sql::OpInnerJoin>(op) ||
                   std::holds_alternative<sql::OpLeftJoin>(op)) {
          // T --op--> (T1, T2, Arg)
          auto tableStates1 = fta.getStatesBySymbol(production.getRhs()[0]);
          auto tableStates2 = fta.getStatesBySymbol(production.getRhs()[1]);
          // SPDLOG_LOGGER_TRACE(logger,
          //                     "Cartesian product size on first 2 arguments: {}",
          //                     tableStates1.size() * tableStates2.size());
          auto argSymbol = production.getRhs()[2];
          for (auto tableState1 : tableStates1) {
            auto tableValue1 =
                fta.getStateManager().read(tableState1).getValue();
            auto validIntervals1 =
                semantics.getValidIntervalsFromTable(tableValue1);
            int numOfCols1 = validIntervals1.back().second + 1;
            for (auto tableState2 : tableStates2) {
              auto tableValue2 =
                  fta.getStateManager().read(tableState2).getValue();
              auto validIntervals2 =
                  semantics.getValidIntervalsFromTable(tableValue2);
              int numOfCols2 = validIntervals2.back().second + 1;
              if (numOfCols1 + numOfCols2 >
                  constants::Constants::sqlSetting
                      .maxNumberOfColumnsIntermediate) {
                continue;
              }
              auto argStates = buildValidArgsByIntervals(
                  grammar, semantics, fta, argSymbol, validIntervals1,
                  validIntervals2, logger, costLimit, numOfPredicates);
              dealWithProduction(production, fta,
                                 {{tableState1}, {tableState2}, argStates},
                                 semantics, logger, costLimit, numOfPredicates);
            }
          }
        } else {
          std::vector<std::vector<IDType>> argStates;
          for (auto& arg : production.getRhs()) {
            argStates.push_back(fta.getStatesBySymbol(arg));
          }
          dealWithProduction(production, fta, argStates, semantics, logger,
                             costLimit, numOfPredicates);
        }
      }
    }
  }

  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-states|{}",
                      fta.getStates().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-transitions|{}",
                      fta.getTransitions().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-symbols|{}",
                      fta.getSymbols().size());
  return fta;
}

}  // namespace foresighter::implementation::algorithms
