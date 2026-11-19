/**
 * @file slicing_server.hpp
 * @brief Caches pre-computed slices (sub-FTAs) per benchmark to disk.
 *
 * SlicingServer serializes/deserializes Slice<FTAType> objects to binary files,
 * keyed by benchmark ID and cost level. Builds slices for all cost levels of a
 * benchmark using the SlicingOracle, enabling fast online loading during synthesis.
 */
#pragma once
#include <spdlog/spdlog.h>
#include <cereal/archives/binary.hpp>
#include <filesystem>
#include <foresighter/implementation/basics/types.hpp>
#include <fstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/common/exceptions.hpp"
#include "foresighter/implementation/basics/slice.hpp"
#include "foresighter/implementation/basics/slicing_oracle.hpp"
#include "foresighter/implementation/sql/abstract_value/bottom.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_col_type.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_num_of_columns.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_subset_of_input.hpp"
#include "foresighter/implementation/sql/abstract_value/unabstracted_term.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"
#include "foresighter/implementation/sql/symbol/op_proj_list.hpp"
#include "foresighter/implementation/sql/types.hpp"

#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"

namespace {
using namespace foresighter::implementation::basics;

template <class SemanticsType, class AbstractorType, class AbstractFTAType,
          class BenchmarkType, class GrammarType>
foresighter::implementation::basics::VariableControlledSourceSet
getVariableStates(const AbstractFTAType& fta, const BenchmarkType& benchmark,
                  const GrammarType& grammar) {
  using SymbolType = GrammarType::SymbolType;
  using LatticeType = AbstractorType::Lattice;
  AbstractorType abstractor(benchmark);
  const auto& stateManager = fta.getStateManager();
  auto& varSymbols = grammar.getVariables();

  auto varTableSymbols = [&]() {
    std::vector<SymbolType> result;
    for (auto& symbol : varSymbols) {
      if (symbol.isVariable() &&
          std::holds_alternative<foresighter::implementation::sql::VarTable>(
              symbol.getSymbol())) {
        result.push_back(symbol);
      }
    }
    return result;
  }();

  foresighter::implementation::basics::VariableControlledSourceSet
      variableStates(varTableSymbols.size());
  for (int i = 0; i < std::min(benchmark.getIOPairs()[0].getInput().size(),
                               varTableSymbols.size());
       ++i) {
    auto& symbol = varTableSymbols[i];
    auto& states = fta.getStatesBySymbol(symbol);
    typename SemanticsType::ValueType inputVal(
        benchmark.getIOPairs()[0].getInput()[i]);
    auto abstractVal = abstractor.abstract(inputVal);
    for (auto& state : states) {
      if (LatticeType::implies(abstractVal,
                               abstractor.removeRowNumRange(
                                   stateManager.read(state).getValue()))) {
        variableStates.setSourceSet(i, state);
        //input space is disjoint, so we can break here
        break;
      }
    }
  }
  return variableStates;
}

//see if value1 implies value2 under canonical order and projection
//each column has type and SOI(i)
//we categorize columns according to (type, SOI(i)) keys
//A implies B if cat(A) is superset of cat(B)
//this is hardcoded for 3 offline predicates for now
using namespace foresighter::implementation::sql::semantics::values;
using namespace foresighter::implementation::sql;

bool impliesUnderCanonicalOrderAndProjectionBool(
    const OfflineConstructionValue& value1,
    const OfflineConstructionValue& value2) {

  struct ColumnType {
    int type;
    std::set<int> SOIInputTables;

    bool operator==(const ColumnType& other) const {
      return type == other.type and SOIInputTables == other.SOIInputTables;
    }

    [[nodiscard]] bool implies(const ColumnType& other) const {
      return (type == other.type or other.type == -1) and
             std::includes(SOIInputTables.begin(), SOIInputTables.end(),
                           other.SOIInputTables.begin(),
                           other.SOIInputTables.end());
    }
  };

  auto getColumnType = [&](const OfflineConstructionValue& value)
      -> emhash8hashmap::HashMap<int, ColumnType> {
    emhash8hashmap::HashMap<int, ColumnType> result;
    //build result map
    int numOfCols = -1;
    if (std::holds_alternative<OfflineConstructionValue::ConjunctionType>(
            value.getValue())) {
      for (auto& predicate :
           std::get<OfflineConstructionValue::ConjunctionType>(value.getValue())
               .getConjunction()) {
        if (std::holds_alternative<PredicateSubsetOfInput>(predicate)) {
          auto predicateSubsetOfInput =
              std::get<PredicateSubsetOfInput>(predicate);
          int colIndexInCur = predicateSubsetOfInput.getIdxInCur();
          int colIndexInInput = predicateSubsetOfInput.getInputTableIdx();
          auto it = result.find(colIndexInCur);
          if (it == result.end()) {
            result[colIndexInCur] = ColumnType{-1, {colIndexInInput}};
          } else {
            it->second.SOIInputTables.insert(colIndexInInput);
          }
        } else if (std::holds_alternative<PredicateColType>(predicate)) {
          auto predicateColType = std::get<PredicateColType>(predicate);
          int colType = predicateColType.getColType();
          int colIndex = predicateColType.getColIdx();
          auto it = result.find(colIndex);
          if (it == result.end()) {
            result[colIndex] = ColumnType{colType, {}};
          } else {
            it->second.type = colType;
          }
        } else if (std::holds_alternative<PredicateNumOfColumns>(predicate)) {
          numOfCols =
              std::get<PredicateNumOfColumns>(predicate).getNumOfColumn();
          continue;
        } else if (std::holds_alternative<Top>(predicate)) {
          continue;
        } else {
          throw foresighter::common::InvalidArgument(
              "Invalid offline construction value 165: " + value.toString());
        }
      }
    } else if (std::holds_alternative<PredicateSubsetOfInput>(
                   value.getValue())) {
      auto predicate = std::get<PredicateSubsetOfInput>(value.getValue());
      int colIndexInCur = predicate.getIdxInCur();
      int colIndexInInput = predicate.getInputTableIdx();
      auto it = result.find(colIndexInCur);
      if (it == result.end()) {
        result[colIndexInCur] = ColumnType{-1, {colIndexInInput}};
      } else {
        it->second.SOIInputTables.insert(colIndexInInput);
      }
    } else if (std::holds_alternative<PredicateColType>(value.getValue())) {
      auto predicate = std::get<PredicateColType>(value.getValue());
      int colType = predicate.getColType();
      int colIndex = predicate.getColIdx();
      auto it = result.find(colIndex);
      if (it == result.end()) {
        result[colIndex] = ColumnType{colType, {}};
      } else {
        it->second.type = colType;
      }
    } else if (std::holds_alternative<Top>(value.getValue())) {
      return result;
    } else if (std::holds_alternative<PredicateNumOfColumns>(
                   value.getValue())) {
      return result;
    } else {
      throw foresighter::common::InvalidArgument(
          "Invalid offline construction value 198: " + value.toString());
    }
    if (numOfCols != -1) {
      for (auto idx = 0; idx < numOfCols; ++idx) {
        auto it = result.find(idx);
        if (it == result.end()) {
          result[idx] = ColumnType{-1, {}};
        }
      }
    }
    return result;
  };

  auto resultMapForValue1 = getColumnType(value1);
  auto resultMapForValue2 = getColumnType(value2);

  //see if for all predicates in value 1, there is a predicate in value 2 that implies it
  for (auto& [colIndex, columnType] : resultMapForValue1) {
    bool hasImpliedPredicate = false;
    if (resultMapForValue2.empty()) {
      continue;
    }
    for (auto& [colIndex2, columnType2] : resultMapForValue2) {
      if (columnType.implies(columnType2)) {
        hasImpliedPredicate = true;
        break;
      }
    }
    if (!hasImpliedPredicate) {
      return false;
    }
  }
  return true;
}

template <class SemanticsType, class AbstractorType, class AbstractFTAType,
          class BenchmarkType, class GrammarType>
std::vector<IDType> getOutputStates(
    AbstractFTAType& fta, const BenchmarkType& benchmark,
    const GrammarType& grammar, const std::shared_ptr<spdlog::logger>& logger) {
  using LatticeType = AbstractorType::Lattice;
  AbstractorType abstractor(benchmark);
  const auto& stateManager = fta.getStateManager();
  std::vector<IDType> outputStates;
  typename SemanticsType::ValueType outputVal(
      benchmark.getIOPairs()[0].getOutput());
  std::vector<int> inputRowNums(benchmark.getIOPairs()[0].getInput().size());
  for (int i = 0; i < inputRowNums.size(); ++i) {
    inputRowNums[i] = benchmark.getIOPairs()[0].getInput()[i].getNumRows();
  }
  int outputRowNum = benchmark.getIOPairs()[0].getOutput().getNumRows();
  auto [abstractVal, order] =
      semantics::transformers::OfflineConstructionTransformer::getOrderedValue(
          abstractor.abstract(outputVal));
  //log the output's strongest abstract value
  SPDLOG_LOGGER_TRACE(logger, "|output-strongest-abstract-value|{}",
                      abstractVal.toString());
  auto& startSymbols = grammar.getStartSymbols();
  for (auto& symbol : startSymbols) {
    auto& states = fta.getStatesBySymbol(symbol);
    //for those states such that abstractVal -> state, add to outputStates
    for (auto& state : states) {
      auto stateVal = stateManager.read(state).getValue();
      //if abstractVal or stateVal is bottom, then skip
      if (std::holds_alternative<foresighter::implementation::sql::Bottom>(
              abstractVal.getValue()) ||
          std::holds_alternative<foresighter::implementation::sql::Bottom>(
              stateVal.getValue())) {
        continue;
      }
      //here, we seperate the logic for row number and others, mainly because offline we represented row number in a different way,
      //which is not consistent with the abstractor implementation.
      if (auto mappings = impliesUnderCanonicalOrderAndProjectionBool(
              abstractVal, abstractor.removeRowNumRange(
                               stateManager.read(state).getValue()));
          mappings &&
          abstractor.impliesRowNumRange(inputRowNums, outputRowNum,
                                        stateManager.read(state).getValue())) {
        outputStates.push_back(state);
      }
    }
  }
  return outputStates;
}

using Term = foresighter::implementation::sql::Term;
using namespace foresighter::implementation::sql;

Term getProjectionListTerm(const std::vector<int>& order) {
  std::vector<Term> colIndices;
  colIndices.reserve(order.size());
  for (int i = 0; i < order.size(); ++i) {
    colIndices.push_back(Term{Term::SymbolType(ConstColumnIndex(order[i])), 0});
  }
  return Term{Term::SymbolType(OpProjList()), colIndices, 0};
}

}  // namespace

namespace foresighter::implementation::basics {

template <class FTAType>
class SlicingServer {
 public:
  SlicingServer() = delete;

  explicit SlicingServer(const std::string& folderPath)
      : folderPath(folderPath) {
    if (!std::filesystem::exists(folderPath)) {
      std::filesystem::create_directories(folderPath);
    }
  }

  void saveSlicing(
      const std::string& benchmarkID, int cost,
      const std::pair<std::vector<IDType>, std::vector<IDType>>& slice,
      const FTAType& fta, const std::shared_ptr<spdlog::logger>& logger,
      const emhash8hashset::HashSet<IDType>& outputStatesSet) {
    SPDLOG_LOGGER_TRACE(logger, "|saving-slice|{}|{}", benchmarkID, cost);
    auto start = std::chrono::high_resolution_clock::now();
    FTAType ftaToStoreSlice;

    emhash8hashmap::HashMap<IDType, IDType> oldStatesToNewStatesMapping;
    emhash8hashmap::HashMap<IDType, IDType> newStatesToOldStatesMapping;

    for (auto state : slice.first) {
      auto& stateEntity = fta.getStateManager().read(state);
      auto stateID = ftaToStoreSlice.getStateManager().create(
          stateEntity.getSymbol(), stateEntity.getValue(),
          stateEntity.getCost());
      oldStatesToNewStatesMapping[state] = stateID;
      newStatesToOldStatesMapping[stateID] = state;
      ftaToStoreSlice.addState(stateID);
      if (outputStatesSet.contains(state)) {
        ftaToStoreSlice.markFinalState(stateID);
      }
    }

    for (auto transition : slice.second) {
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      std::vector<IDType> newTails;
      IDType newHead;
      for (auto tail : transitionEntity.getTails()) {
        newTails.push_back(oldStatesToNewStatesMapping[tail]);
      }
      newHead = oldStatesToNewStatesMapping[transitionEntity.getHead()];
      auto transitionID = ftaToStoreSlice.getTransitionManager().create(
          newHead, transitionEntity.getSymbol(), newTails,
          transitionEntity.getCost(), transitionEntity.getOrder());
      ftaToStoreSlice.addTransition(transitionID);
    }
    Slice<FTAType> sliceToSave(std::move(ftaToStoreSlice),
                               std::move(newStatesToOldStatesMapping));
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    SPDLOG_LOGGER_TRACE(logger, "|saving-slice-fta-built|{}|{}|{}", benchmarkID,
                        cost, duration.count());

    start = std::chrono::high_resolution_clock::now();
    auto filePath =
        folderPath + "/" + benchmarkID + "|" + std::to_string(cost) + ".bin";
    std::ofstream ofs(filePath, std::ios::binary);
    {
      cereal::BinaryOutputArchive archive(ofs);
      archive(sliceToSave);
    }
    end = std::chrono::high_resolution_clock::now();
    auto fileSizeInMB = std::filesystem::file_size(filePath) / 1024 / 1024;
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    SPDLOG_LOGGER_TRACE(logger, "|saving-slice-fta-saved|{}|{}|{}|{}|{}|{}",
                        benchmarkID, cost, duration.count(),
                        ftaToStoreSlice.getStates().size(),
                        ftaToStoreSlice.getTransitions().size(), fileSizeInMB);
    ofs.close();
  }

  Slice<FTAType> loadSlicing(const std::string& benchmarkID, int cost,
                             std::shared_ptr<spdlog::logger>& logger) const {
    auto filePath =
        folderPath + "/" + benchmarkID + "|" + std::to_string(cost) + ".bin";
    if (!std::filesystem::exists(filePath)) {
      SPDLOG_LOGGER_ERROR(logger, "|loading-slice-fta-not-found|{}|{}",
                          benchmarkID, cost);
      return Slice<FTAType>();
    }
    auto start = std::chrono::high_resolution_clock::now();
    Slice<FTAType> sliceToLoad;
    std::ifstream ifs(filePath, std::ios::binary);
    {
      cereal::BinaryInputArchive archive(ifs);
      archive(sliceToLoad);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    SPDLOG_LOGGER_TRACE(logger, "|loading-slice-fta-loaded|{}|{}|{}",
                        benchmarkID, cost, duration.count());
    return sliceToLoad;
  }

  template <class SemanticsType, class AbstractorType>
  void buildSlices(
      const sql::SQLBenchmark& benchmark, FTAType& fta,
      const foresighter::implementation::basics::SlicingOracle& slicingOracle,
      const sql::Grammar& grammar,
      const std::shared_ptr<spdlog::logger>& logger) {
    SPDLOG_LOGGER_TRACE(logger, "|start-building-slices|{}",
                        benchmark.getName());
    auto start = std::chrono::high_resolution_clock::now();
    const auto& stateManager = fta.getStateManager();
    const auto& transitionManager = fta.getTransitionManager();
    std::vector<std::string> result;
    //initialize variable states
    foresighter::implementation::basics::VariableControlledSourceSet
        variableStates = getVariableStates<SemanticsType, AbstractorType>(
            fta, benchmark, grammar);

    //initialize output states
    SPDLOG_LOGGER_TRACE(logger, "|start-find-output-states|");
    auto outputStates = getOutputStates<SemanticsType, AbstractorType>(
        fta, benchmark, grammar, logger);
    if (outputStates.empty()) {
      SPDLOG_LOGGER_TRACE(logger, "|can-not-find-output-states|");
      return;
    }
    for (int h = 1;
         h <= sql::constants::Constants::slicingOracleSetting.maxExactDistance;
         ++h) {
      SPDLOG_LOGGER_TRACE(logger, "|slice-building-start|{}|{}",
                          benchmark.getName(), h);
      SPDLOG_LOGGER_TRACE(logger, "|slice-finding-start|{}", h);
      auto start = std::chrono::high_resolution_clock::now();
      //for each h, get slicing
      std::vector<IDType> outputStatesFilteredByCost;
      for (auto& state : outputStates) {
        if (fta.getStateManager().read(state).getCost() == h) {
          outputStatesFilteredByCost.push_back(state);
        }
      }
      SPDLOG_LOGGER_TRACE(logger, "|output-states-filtered-by-cost|{}",
                          outputStatesFilteredByCost.size());

      auto outputStatesSetFilteredByCost = emhash8hashset::HashSet<IDType>(
          outputStatesFilteredByCost.begin(), outputStatesFilteredByCost.end());

      auto slicing = slicingOracle.getSlicing(
          variableStates, outputStatesFilteredByCost,
          (int)benchmark.getHints().size(), fta, logger);
      auto end = std::chrono::high_resolution_clock::now();
      auto duration =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      SPDLOG_LOGGER_TRACE(logger, "|slice-finding-end|{}|{}|{}|{}", h,
                          slicing.first.size(), slicing.second.size(),
                          duration.count());

      saveSlicing(benchmark.getName(), h, slicing, fta, logger,
                  outputStatesSetFilteredByCost);
      end = std::chrono::high_resolution_clock::now();
      duration =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      SPDLOG_LOGGER_TRACE(logger, "|slice-building-end|{}|{}|{}",
                          benchmark.getName(), h, duration.count());
    }
    SPDLOG_LOGGER_TRACE(logger, "|end-building-slices|{}|{}",
                        benchmark.getName(),
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::high_resolution_clock::now() - start)
                            .count());
  }

  // Added for no-oracle ablation (slicing_server_building_ablation)
  template <class SemanticsType, class AbstractorType>
  void buildSliceFromOutputs(
      const sql::SQLBenchmark& benchmark, FTAType& fta,
      const auto& slicingOracle, const sql::Grammar& grammar,
      const std::vector<IDType>& outputStates,
      const SlicingOracle::CombinationBitVec& allowedCombination, int h,
      const std::shared_ptr<spdlog::logger>& logger,
      double previous_cost = 0.0) {
    foresighter::implementation::basics::VariableControlledSourceSet
        variableStates = getVariableStates<SemanticsType, AbstractorType>(
            fta, benchmark, grammar);

    auto topoSortedSymbols = algorithms::grammarTopoSort(grammar);

    SPDLOG_LOGGER_TRACE(logger, "|slice-building-start|{}|{}",
                        benchmark.getName(), h);
    SPDLOG_LOGGER_TRACE(logger, "|slice-finding-start|{}", h);
    auto start = std::chrono::high_resolution_clock::now();

    SlicingOracle::Slice slicing;
    if (!outputStates.empty()) {
      slicing = slicingOracle.getSlicing(
          variableStates, outputStates, (int)benchmark.getHints().size(), fta,
          allowedCombination, logger, h, topoSortedSymbols);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    SPDLOG_LOGGER_TRACE(logger, "|slice-finding-final-state-search|{}|{}|{}", h,
                        outputStates.size(), previous_cost);
    SPDLOG_LOGGER_TRACE(logger, "|slice-finding-after-final-state|{}|{}", h,
                        duration.count());
    SPDLOG_LOGGER_TRACE(logger, "|slice-finding-end|{}|{}|{}|{}", h,
                        slicing.first.size(), slicing.second.size(),
                        duration.count() + previous_cost);

    emhash8hashset::HashSet<IDType> outputStatesSet(outputStates.begin(),
                                                    outputStates.end());
    saveSlicing(benchmark.getName(), h, slicing, fta, logger, outputStatesSet);
    end = std::chrono::high_resolution_clock::now();
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    SPDLOG_LOGGER_TRACE(logger, "|slice-building-end|{}|{}|{}",
                        benchmark.getName(), h, duration.count());
  }

 private:
  std::string folderPath;
};
};  // namespace foresighter::implementation::basics