/**
 * @file synthesize_with_slicing.hpp
 * @brief Online synthesis: Slice + Search (Algorithm 1, lines 4-6).
 *
 * Given a pre-built FTA and slicing oracle, loads the slice for a benchmark at each
 * cost level, then searches the sub-FTA for a program consistent with the IO examples.
 * Search enumerates terms bottom-up, evaluates them concretely, and returns the first
 * correct program. Implements iterative deepening by cost.
 * @author Rui Dong
 * @date 5/13/24
 */

#pragma once
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <optional>

// NEW IMPORTS
#include <iostream>
#include <fstream>
#include <foresighter/implementation/sql/query_timer.hpp>

#include <queue>

#include <foresighter/implementation/sql/symbol/var_table.hpp>
#include <foresighter/implementation/sql/types.hpp>

#include <foresighter/implementation/sql/semantics/abstractors/concrete_to_offline_construction_abstractor.hpp>

#include <foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp>

#include <foresighter/implementation/sql/semantics/values/offline_construction_value.hpp>
#include <foresighter/implementation/sql/semantics/values/offline_plus_soi_plus_coltype_value.hpp>
#include <foresighter/implementation/sql/semantics/values/offline_plus_soi_value.hpp>

#include <foresighter/common/hash.hpp>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/common/exceptions.hpp"
#include "foresighter/implementation/basics/fta.hpp"
#include "foresighter/implementation/basics/transformer.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_superset_of_output.hpp"
#include "foresighter/implementation/sql/abstract_value/top.hpp"
#include "foresighter/implementation/sql/abstract_value/unabstracted_term.hpp"
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"
#include "foresighter/implementation/sql/semantics/values/concrete_value.hpp"
#include "foresighter/implementation/sql/semantics/values/online_inverse_semantics_value.hpp"
#include "foresighter/implementation/sql/symbol.hpp"
#include "foresighter/implementation/sql/symbol/var_filter_const.hpp"

#include <foresighter/implementation/basics/slicing_oracle.hpp>
#include <foresighter/implementation/basics/variable_controlled_source_set.hpp>

#include <boost/coroutine2/all.hpp>
#include <foresighter/implementation/algorithms/cfta_term_extraction.hpp>
#include <foresighter/implementation/sql/cegis_runner.hpp>
#include <foresighter/implementation/sql/constants.hpp>
#include <foresighter/implementation/sql/semantics/transformers/online_inverse_transformer.hpp>
#include <foresighter/implementation/sql/sql_rewrite_utils.hpp>
#include <type_traits>

namespace coro = boost::coroutines2;

namespace {
using IDType = foresighter::implementation::basics::IDType;
using foresighter::implementation::sql::wrapSQLWithFinalProjection;

//see if value1 implies value2 under canonical order and projection
//each column has type and SOI(i)
//we categorize columns according to (type, SOI(i)) keys
//A implies B if cat(A) is superset of cat(B)
//this is hardcoded for 3 offline predicates for now
using namespace foresighter::implementation::sql::semantics::values;
using namespace foresighter::implementation::sql;

template <typename RefinedLatticeType, typename FTAType>
std::vector<IDType> getTopoSortedOrderForStates(
    const std::vector<IDType>& targetStates, const FTAType& fta,
    const std::shared_ptr<spdlog::logger>& logger) {
  using ValueType = std::remove_const<typename std::remove_reference<
      decltype(std::declval<FTAType>().getStateManager().read(0).getValue())>::
                                          type>::type;

  // sort the target states by their values
  // two values a and b can be implies, or not implies
  // we do a topological sort such that if a implies b, then a should be before b
  // we build a graph such that a-> b if a implies b
  // b -> a if b implies a
  // if a implies b and b implies a, then a and b are equal. in this case no edge is added
  // we use a topological sort to get the order

  HelperGraphForTopologicalSort helperGraph;

  //first, for each pair of states, add an edge if one implies the other
  for (int i = 0; i < targetStates.size(); ++i) {
    auto& state = targetStates[i];
    auto& stateEntity = fta.getStateManager().read(state);
    auto& value = stateEntity.getValue();
    helperGraph.addNode(state);
    for (int j = i + 1; j < targetStates.size(); ++j) {
      auto& other = targetStates[j];
      auto& otherEntity = fta.getStateManager().read(other);
      auto& otherValue = otherEntity.getValue();
      bool aImpliesB = RefinedLatticeType::implies(value, otherValue);
      bool bImpliesA = RefinedLatticeType::implies(otherValue, value);
      if (aImpliesB and not bImpliesA) {
        helperGraph.addEdge(state, other);
      }
      if (bImpliesA and not aImpliesB) {
        helperGraph.addEdge(other, state);
      }
    }
  }
  auto result = helperGraph.getTopologicalSortOrder();
  if (result.size() != targetStates.size()) {
    throw InvalidArgument("result size != targetStates size" +
                          std::to_string(result.size()) +
                          " != " + std::to_string(targetStates.size()));
  }

  return result;
}

}  // namespace

namespace foresighter::implementation::algorithms {

class Synthesizer {
 public:
  using IDType = foresighter::implementation::basics::IDType;
  using WeightType = foresighter::implementation::basics::WeightType;
  using Slicing = const std::pair<std::vector<IDType>, std::vector<IDType>>;

  Synthesizer() = delete;

  explicit Synthesizer(std::shared_ptr<spdlog::logger> logger)
      : logger(std::move(logger)) {}

  //forbid copy and move
  Synthesizer(const Synthesizer&) = delete;
  Synthesizer(Synthesizer&&) = delete;
  Synthesizer& operator=(const Synthesizer&) = delete;
  Synthesizer& operator=(Synthesizer&&) = delete;

  void setSynthesisStart() {
    synthesis_start_timestamp_ns = std::chrono::high_resolution_clock::now();
  }

  template <typename LatticeType>
  IDType mapConcreteStateToAFTAState(auto& concreteState, auto& symbol,
                                     auto& concreteValue,
                                     auto& aftaSatesTopologicalOrder,
                                     auto& aftaStateManager, auto& abstractor,
                                     auto& inputRowNums,
                                     auto& cftaToAftaMappingcache,
                                     auto originalMappedState) {
    auto it = cftaToAftaMappingcache.find(concreteState);
    if (it != cftaToAftaMappingcache.end()) {
      gTotalMappingCacheHit++;
      return it->second;
    }
    auto state = findFirstImplies<LatticeType>(
        symbol, concreteValue, aftaSatesTopologicalOrder, aftaStateManager,
        abstractor, inputRowNums, originalMappedState);
    if (state == -1) {
      // some NULL value issue. map back to original and not fixing for now.
      state = originalMappedState;
      cftaToAftaMappingcache[concreteState] = state;
    } else {
      cftaToAftaMappingcache[concreteState] = state;
    }
    return state;
  }
  
  template <typename LatticeType>
  IDType findFirstImplies(auto& symbol, auto& concreteValue,
                          auto& aftaStatesTopologicalOrder,
                          auto& aftaStateManager, auto& abstractor,
                          auto& inputRowNums, auto originalMappedState) {
    auto& candidateStates = aftaStatesTopologicalOrder[symbol];
    auto strongestAbstractValue = abstractor.abstract(concreteValue);
    for (auto& state : candidateStates) {
      auto& stateEntity = aftaStateManager.read(state);
      auto& aftaEntityValue = aftaStateManager.read(state).getValue();
      if (LatticeType::implies(
              strongestAbstractValue,
              abstractor.removeRowNumRange(aftaEntityValue)) and
          abstractor.impliesRowNumRange(
              inputRowNums,
              std::get<ConcreteTable>(concreteValue.getValue()).getNumRows(),
              aftaEntityValue)) {
        return state;
      }
    }
    return -1;
  }

  /**
 * @brief given a slice, find a program that satisfies the input-output example by building a CFTA bottom-up
 * @details The algorithm has the following steps:
  * 1. Given the slice, do a topo-sort on the slice
  * 2. for each node, if it is a leaf node, then concretize it (base case)
  * 3. if it is not a leaf node, then for each child, try out all possible combinations of argument choices
  * 4. get the return state from the concrete interpreter
  * 5. if the return state is one of the output state, then do a top-down backtracking to get the program
  * 6. if the return state is not one of the output state, then continue the search
 * @param slicing 
 * @param fta 
 * @param benchmark 
 * @param grammar 
 * @param targetStates
  * @param sliceCost NOTE: include this to filter those spurious programs caused by merging of target states' slices.
 * @return std::vector<TermType> 
 */
  template <class RefinedLattice, class SliceType, class BenchmarkType,
            class GrammarType, class InterpreterType>
  std::vector<std::string> findProgramSatisfyingIOExampleFromSlicing(
      const Slicing& slicing, const SliceType& slice,
      const BenchmarkType& benchmark, const GrammarType& grammar,
      InterpreterType& interpreter, int sliceCost,
      const std::vector<std::string>& hintsForFilterConstants, long timeoutInMs,
      foresighter::implementation::sql::CEGISRunner& cegisRunner,
      emhash8hashmap::HashMap<IDType, IDType>& previouslyMappedCFTAState,
      basics::FTA<typename GrammarType::SymbolType,
                  typename InterpreterType::ValueType>& cftaForSlicing,
      emhash8hashmap::HashMap<IDType, emhash8hashset::HashSet<IDType>>&
          mappingAbstractStateToConcreteStates,
        foresighter::implementation::sql::QueryTimer& queryTimer,
      auto& abstractor) {
    using SymbolType = GrammarType::SymbolType;
    using ConcreteFTAType =
        basics::FTA<SymbolType, typename InterpreterType::ValueType>;
    using TermType = sql::Term;

    auto& fta = slice.getFTA();

    auto timeoutStart = std::chrono::high_resolution_clock::now();
    auto this_round_concretization_start =
        std::chrono::high_resolution_clock::now();
    auto checkTimeOut = [&timeoutStart, timeoutInMs]() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::high_resolution_clock::now() - timeoutStart)
                 .count() > timeoutInMs;
    };

    bool found = false;
    std::vector<std::string> returnPrograms;

    gSliceCost = sliceCost;
    gSketchID = 0;
    gCurSketchMatchTime = 0;
    gCurSketchConcretizationTime = 0;
    gCurSketchSolutionExtractionTime = 0;
    gCurSketchValidationTime = 0;
    gTotalMappingChecked = 0;
    gTotalMappingCacheHit = 0;
    gTotalMappingJumpped = 0;
    gCurSketchRemappingTime = 0;

    auto termExtractionPrevious = solution_extraction_used_time_ns;

    std::vector<IDType> targetStates = fta.getFinalStates();
    std::vector<sql::Symbol> topoSortedSymbols = grammarTopoSort(grammar);

    //then, topo-sort the slicing and get a list of states following topological order
    auto topoSortedStatesPerSymbol =
        getTopoSortedStatesForSlicingPerSymbol<RefinedLattice>(slicing, fta);

    //a mapping that keeps track of the abstract transitions that are mapped to each target node
    emhash8hashmap::HashMap<IDType, std::vector<IDType>>
        lookUpTransitionsGivenTargetNode;

    //input row nums, will be used later
    std::vector<int> inputRowNums(benchmark.getIOPairs()[0].getInput().size());
    for (int i = 0; i < inputRowNums.size(); ++i) {
      inputRowNums[i] = benchmark.getIOPairs()[0].getInput()[i].getNumRows();
    }

    //iteratively build from bottom up
    auto& aftaStateManager = fta.getStateManager();
    auto& aftaTransitionManager = fta.getTransitionManager();
    auto& cftaStateManager = cftaForSlicing.getStateManager();
    auto& cftaTransitionManager = cftaForSlicing.getTransitionManager();

    //initialize term extractor
    FTATermExtractor<TermType, ConcreteFTAType> termExtractor(cftaForSlicing,
                                                              logger);

    //initialize lookUpTransitionsGivenTargetNode
    for (auto& transition : slicing.second) {
      auto& transitionEntity = aftaTransitionManager.read(transition);
      auto head = transitionEntity.getHead();
      lookUpTransitionsGivenTargetNode[head].push_back(transition);
    }

    int processed = 0;

    // ! START OF CONCRETIZATION OF AFTA SLICE
    auto concretizeSliceStartTime = std::chrono::high_resolution_clock::now();

    // we process symbol by symbol.
    for (auto& symbol : topoSortedSymbols) {
      auto start_concretization = std::chrono::high_resolution_clock::now();
      for (auto& curState : topoSortedStatesPerSymbol[symbol]) {

        if (mappingAbstractStateToConcreteStates.find(slice.getGlobalState(
                curState)) != mappingAbstractStateToConcreteStates.end()) {
          //in this case, it is previously concretized. No need to concretize again.
          continue;
        }

        //check timeout
        if (checkTimeOut()) {
          SPDLOG_LOGGER_TRACE(
              logger, "|timeout-during-fta-construction|{}|{}", curState,
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::high_resolution_clock::now() - timeoutStart)
                  .count());
          logStatistics();
          logger->flush();
          break;
        }

        //there are three cases:
        //1. curState is a constant leaf node
        //2. curState is a variable leaf node
        //3. curState is a non-leaf node
        //for case 1, we can directly concretize it by adding a unabstracted state to the CFTA
        //for case 2, we need to concretize it by adding the concretized table corresponding to the variable to the CFTA
        //for case 3, we need to
        //  (1) get cartesian product of concrete states mapped to each abstract state
        //  (2) get the return state from the concrete transformer
        //  (3) add the return state to the CFTA if it is not already in the CFTA
        auto& stateEntity = aftaStateManager.read(curState);

        //for case 1, we can directly concretize it by adding an unabstracted state to the CFTA
        if (stateEntity.getSymbol().isConstant()) {
          //add a new const state in cfta.
          sql::semantics::values::ConcreteValue::VariantType value =
              sql::UnAbstractedTerm((sql::Term)(stateEntity.getSymbol()));
          auto newlyBuiltConcreteStateID = cftaStateManager.create(
              stateEntity.getSymbol(),
              sql::semantics::abstractors::ConcreteValue(value),
              stateEntity.getCost());
          cftaForSlicing.addState(newlyBuiltConcreteStateID);
          mappingAbstractStateToConcreteStates[slice.getGlobalState(curState)]
              .insert(newlyBuiltConcreteStateID);
        } else if (stateEntity.getSymbol().isVariable()) {
          //if it's varTable
          if (std::holds_alternative<sql::VarTable>(
                  stateEntity.getSymbol().getSymbol())) {
            //add a new variable state in cfta. we need to bind the concrete table in I/O pair to the variable
            //this means that the state should be (varSymbol, concreteTable)
            auto varSymbolIdx =
                std::get<sql::VarTable>(stateEntity.getSymbol().getSymbol())
                    .getIndex();
            auto newlyBuiltConcreteStateID = cftaStateManager.create(
                stateEntity.getSymbol(),
                sql::semantics::values::ConcreteValue(
                    benchmark.getIOPairs()[0].getInput()[varSymbolIdx]),
                stateEntity.getCost());
            cftaForSlicing.addState(newlyBuiltConcreteStateID);
            mappingAbstractStateToConcreteStates[slice.getGlobalState(curState)]
                .insert(newlyBuiltConcreteStateID);
          }
          //if it's filter constant
          else if (std::holds_alternative<sql::VarFilterConst>(
                       stateEntity.getSymbol().getSymbol())) {
            auto filterConstIdx = std::get<sql::VarFilterConst>(
                                      stateEntity.getSymbol().getSymbol())
                                      .getIndex();
            auto term =
                sql::Term((sql::Symbol)sql::VarFilterConst(filterConstIdx));
            auto newlyBuiltConcreteStateID = cftaStateManager.create(
                stateEntity.getSymbol(),
                (sql::semantics::values::ConcreteValue)sql::UnAbstractedTerm(
                    term),
                stateEntity.getCost());
            cftaForSlicing.addState(newlyBuiltConcreteStateID);
            mappingAbstractStateToConcreteStates[slice.getGlobalState(curState)]
                .insert(newlyBuiltConcreteStateID);
          }
        } else {
          //it is a non-leaf node. we need to get all transitions pointing to it from the afta, and then get the cartesian product of the concrete states belonging to each abstract state.
          auto& allTransitionsGivenTargetNode =
              lookUpTransitionsGivenTargetNode[curState];
          for (auto& transitionGivenTargetNode :
               allTransitionsGivenTargetNode) {
            if (checkTimeOut()) {
              SPDLOG_LOGGER_TRACE(
                  logger, "|timeout-during-fta-construction|{}|{}", curState,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::high_resolution_clock::now() - timeoutStart)
                      .count());
              logger->flush();
              logStatistics();
              break;
            }
            //for each transition, find the concrete states that are mapped to the abstract states in tails. Do the cartesian product of these concrete states
            std::vector<std::vector<IDType>> concreteStatesForTails;
            auto& transitionEntity =
                aftaTransitionManager.read(transitionGivenTargetNode);
            auto& tails = transitionEntity.getTails();
            for (auto& tail : tails) {
              concreteStatesForTails.emplace_back(
                  mappingAbstractStateToConcreteStates[slice.getGlobalState(
                                                           tail)]
                      .begin(),
                  mappingAbstractStateToConcreteStates[slice.getGlobalState(
                                                           tail)]
                      .end());
            }
            //get the cartesian product of the concrete states
            std::vector<std::vector<IDType>> argCartesianProduct =
                foresighter::common::cartesianProduct(concreteStatesForTails);
            //for each cartesian product, get the return state from the concrete transformer and add it to the CFTA
            for (auto& arg : argCartesianProduct) {
              if (checkTimeOut()) {
                SPDLOG_LOGGER_TRACE(
                    logger, "|timeout-during-fta-construction|{}|{}", curState,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() -
                        timeoutStart)
                        .count());
                logger->flush();
                logStatistics();
                break;
              }

              std::vector<sql::semantics::values::ConcreteValue>
                  argConcreteValues;
              for (auto& argState : arg) {
                argConcreteValues.push_back(
                    cftaStateManager.read(argState).getValue());
              }
              auto returnValueAfterTransform = interpreter.transform(
                  transitionEntity.getSymbol(), argConcreteValues,
                  transitionEntity.getOrder());
              //if error or bottom, then skip
              if (std::holds_alternative<sql::Error>(
                      returnValueAfterTransform.getValue()) ||
                  std::holds_alternative<sql::Bottom>(
                      returnValueAfterTransform.getValue())) {
                continue;
              }
              auto newlyBuiltConcreteStateID = cftaStateManager.create(
                  stateEntity.getSymbol(), returnValueAfterTransform,
                  stateEntity.getCost());
              cftaForSlicing.addState(newlyBuiltConcreteStateID);
              auto newTransitionID = cftaTransitionManager.create(
                  newlyBuiltConcreteStateID, transitionEntity.getSymbol(), arg,
                  transitionEntity.getCost(), transitionEntity.getOrder());
              cftaForSlicing.addTransition(newTransitionID);

              if (std::holds_alternative<sql::ConcreteTable>(
                      returnValueAfterTransform.getValue())) {
                auto startRemapping = std::chrono::high_resolution_clock::now();
                auto mappedAFTAState =
                    mapConcreteStateToAFTAState<RefinedLattice>(
                        newlyBuiltConcreteStateID, stateEntity.getSymbol(),
                        returnValueAfterTransform, topoSortedStatesPerSymbol,
                        aftaStateManager, abstractor, inputRowNums,
                        previouslyMappedCFTAState, curState);
                gTotalMappingChecked++;
                if (mappedAFTAState != curState) {
                  gTotalMappingJumpped++;
                }
                mappingAbstractStateToConcreteStates[slice.getGlobalState(
                                                         mappedAFTAState)]
                    .insert(newlyBuiltConcreteStateID);
                auto endRemapping = std::chrono::high_resolution_clock::now();
                gCurSketchRemappingTime +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        endRemapping - startRemapping)
                        .count();
              } else {
                mappingAbstractStateToConcreteStates[slice.getGlobalState(
                                                         curState)]
                    .insert(newlyBuiltConcreteStateID);
              }

              // ! START OF SOLUTION EXTRACTION FOR THIS CONCRETE STATE
              auto equivOverheadStartTime = std::chrono::high_resolution_clock::now();

              //check if the return value is consistent with the output
              //if cur state is in the target states, and if the return value is consistent with the output, then we have found a program
              //if the return value is consistent with the concrete output value, then we can extract the program from the CFTA.
              //this extraction has to be guided using AFTA, because since we are merging concrete same states during the construction
              //of CFTA, there might be surious states in the CFTA that are not in the AFTA.
              if (std::find(targetStates.begin(), targetStates.end(),
                            curState) != targetStates.end()) {
                auto consistentMappings =
                    std::get<sql::ConcreteTable>(
                        returnValueAfterTransform.getValue())
                        .getConsistentMappingsUnderProjection(
                            benchmark.getIOPairs()[0].getOutput());
                if (consistentMappings.empty()) {
                  continue;
                }
                //now it has consistent mappings - for each mapping, we can get one program (wrap projection outside of the inner extracted programs)
                auto startTermExtraction =
                    std::chrono::high_resolution_clock::now();
                //do a top-down backtracking to get the program
                auto extractedTerms =
                    termExtractor
                        .extractAcceptedTermsFromStateWithFirstTransitionEnforced(
                            newlyBuiltConcreteStateID, newTransitionID);
                auto endTermExtraction =
                    std::chrono::high_resolution_clock::now();
                solution_extraction_used_time_ns +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        endTermExtraction - startTermExtraction)
                        .count();
                gCurSketchSolutionExtractionTime +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        endTermExtraction - startTermExtraction)
                        .count();
                for (auto& term : extractedTerms) {
                  numberOfProgramsEnumerated++;
                  auto startSolutionValidation =
                      std::chrono::high_resolution_clock::now();
                  auto sql = interpreter.getSQLFromTerm(term);
                  auto endSolutionValidation =
                      std::chrono::high_resolution_clock::now();
                  gCurSketchValidationTime +=
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                          endSolutionValidation - startSolutionValidation)
                          .count();
                  for (auto& mapping : consistentMappings) {
                    auto startSolutionValidation =
                        std::chrono::high_resolution_clock::now();
                    auto sqlWrapped = wrapSQLWithFinalProjection(sql, mapping);

                    // ! START OF VERIFYING THE CURRENT CANDIDATE QUERY SATISFIES ALL EXAMPLES VIA CEGIS
                    auto cegisVerifStartTime = std::chrono::high_resolution_clock::now();

                    //go to cegis runner
                    auto cegisRunnerResult = cegisRunner.run(sqlWrapped);
                    auto endSolutionValidation =
                        std::chrono::high_resolution_clock::now();
                    gCurSketchValidationTime +=
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            endSolutionValidation - startSolutionValidation)
                            .count();
                    if (cegisRunnerResult.status != -1) {
                      if (cegisRunnerResult.status >= 0) {

                        if (cegisRunnerResult.status > soFarMostIndexReached) {
                          soFarMostIndexReached = cegisRunnerResult.status;
                          auto solution_found_time =
                              std::chrono::high_resolution_clock::now();
                          SPDLOG_LOGGER_TRACE(
                              logger,
                              "|sql-solution-found|{}|{}|{}|{}|{}|{}|{}|{}|{}|{"
                              "}|{"
                              "}|{"
                              "}|{}|{"
                              "}|{"
                              "}|{"
                              "}|{}|{"
                              "}|{}|{"
                              "}",
                              abstractor_construction_used_time_ns * 1.0 /
                                  1000000,
                              flush_used_time_ns * 1.0 / 1000000,
                              cegisRunner.getTotalInterpreterTimeInNs() * 1.0 /
                                  1000000,
                              cegisRunner.getTotalInterpreterCalls(),
                              gCurSketchValidationTime * 1.0 / 1000000,
                              numberOfProgramsEnumerated,
                              cegisRunnerResult.status,  //solution status
                              std::chrono::duration_cast<
                                  std::chrono::nanoseconds>(
                                  solution_found_time -
                                  synthesis_start_timestamp_ns)
                                      .count() *
                                  1.0 /
                                  1000000,  //total time taken to find the solution in ms -13
                              afta_slicing_used_time_ns * 1.0 /
                                  1000000,  //time taken for slicing in ms -12
                              afta_refinement_used_time_ns * 1.0 /
                                  1000000,  //time taken for refinement in ms -11
                              afta_pruning_used_time_ns * 1.0 /
                                  1000000,  //time taken for pruning in ms -10
                              solution_extraction_used_time_ns * 1.0 /
                                  1000000,  //time taken for solution extraction in ms, -9
                              interpreter.getTotalInterpreterTimeInNs() * 1.0 /
                                  1000000,  //time taken for interpreter in ms -8
                              cfta_concretization_used_time_ns * 1.0 /
                                  1000000,  //time taken for concretization in ms -7
                              cftaForSlicing.getStates()
                                  .size(),  //number of states in the CFTA -6,
                              cftaForSlicing.getTransitions()
                                  .size(),  //number of transitions in the CFTA -5,
                              interpreter
                                  .getNumInterpreterCalls(),  //number of interpreter calls -4
                              term.getTotalWeight(),  //slice cost -3
                              term.getASTSizeWithoutIdentityAndIntermediateProjection() +
                                  2 + mapping.size(),  //size of the AST -2
                              sqlWrapped               //the SQL program -1
                          );
                          SPDLOG_LOGGER_TRACE(logger,
                                              "|sql-solution-found-dsl|{}|{}",
                                              cegisRunnerResult.status,
                                              term.toDSLSketchString());
                        }
                      } else {
                        //this case, some error occurred. print the error message
                        SPDLOG_LOGGER_TRACE(logger,
                                            "|CEGISRunner-failed|{}|{}|{}",
                                            cegisRunnerResult.status,
                                            cegisRunnerResult.info, sqlWrapped);
                      }
                      continue;
                    }

                    // ! END: CURRENT CANDIDATE QUERY HAS BEEN VERIFIED TO SATISFY ALL EXAMPLES VIA CEGIS
                    auto cegisVerifEndTime = std::chrono::high_resolution_clock::now();
                    std::chrono::nanoseconds cegisVerifDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(cegisVerifEndTime - cegisVerifStartTime);

                    returnPrograms.push_back(sqlWrapped);

                    // ! TIME THIS VERIFIED REWRITE ON THE TARGET DATABASE
                    if (auto execNs = queryTimer.medianExecTimeNs(sqlWrapped)) {
                      if (fastestRewriteExecTimeNs < 0 ||
                          *execNs < fastestRewriteExecTimeNs) {
                        fastestRewriteExecTimeNs = *execNs;
                        fastestRewriteSQL = sqlWrapped;
                        fastestRewriteIdx = (int)returnPrograms.size() - 1;
                        auto bestFoundElapsedMs =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::high_resolution_clock::now() -
                                synthesis_start_timestamp_ns)
                                .count();
                        SPDLOG_LOGGER_TRACE(logger,
                                            "|baseline-best-rewrite-updated|{}|{}|{}",
                                            bestFoundElapsedMs, *execNs, sqlWrapped);
                      }
                      SPDLOG_LOGGER_TRACE(logger, "|rewrite-timing-ns|{}|{}",
                                          *execNs, sqlWrapped);
                    } else {
                      SPDLOG_LOGGER_TRACE(logger, "|rewrite-timing-skipped|{}",
                                          sqlWrapped);
                    }


                    auto solution_found_time =
                        std::chrono::high_resolution_clock::now();
                    SPDLOG_LOGGER_TRACE(
                        logger,
                        "|sql-solution-found|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{"
                        "}|{"
                        "}|{"
                        "}|{}|{"
                        "}|{"
                        "}|{"
                        "}|{}|{"
                        "}",
                        abstractor_construction_used_time_ns * 1.0 / 1000000,
                        flush_used_time_ns * 1.0 / 1000000,
                        cegisRunner.getTotalInterpreterTimeInNs() * 1.0 /
                            1000000,
                        cegisRunner.getTotalInterpreterCalls(),
                        gCurSketchValidationTime * 1.0 / 1000000,
                        numberOfProgramsEnumerated,
                        cegisRunnerResult.status,  //solution status
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            solution_found_time - synthesis_start_timestamp_ns)
                                .count() *
                            1.0 /
                            1000000,  //total time taken to find the solution in ms -13
                        afta_slicing_used_time_ns * 1.0 / 1000000,
                        afta_refinement_used_time_ns * 1.0 / 1000000,
                        afta_pruning_used_time_ns * 1.0 / 1000000,
                        solution_extraction_used_time_ns * 1.0 / 1000000,
                        interpreter.getTotalInterpreterTimeInNs() * 1.0 /
                            1000000,
                        cfta_concretization_used_time_ns * 1.0 / 1000000,
                        cftaForSlicing.getStates()
                            .size(),  //number of states in the CFTA -6,
                        cftaForSlicing.getTransitions()
                            .size(),  //number of transitions in the CFTA -5,
                        interpreter
                            .getNumInterpreterCalls(),  //number of interpreter calls -4
                        term.getTotalWeight(),          //slice cost -3
                        term.getASTSizeWithoutIdentityAndIntermediateProjection() +
                            2 + mapping.size(),  //size of the AST -2
                        sqlWrapped               //the SQL program -1
                    );
                    SPDLOG_LOGGER_TRACE(logger, "|sql-solution-found-dsl|{}|{}",
                                        cegisRunnerResult.status,
                                        term.toDSLSketchString());
                    solution_found_num++;
                    SPDLOG_LOGGER_TRACE(
                        logger, "|per-sketch-statistics|{}|{}|{}|{}|{}|{}|{}",
                        gSliceCost, gSketchID,
                        gCurSketchMatchTime * 1.0 / 1000000,
                        gCurSketchConcretizationTime * 1.0 / 1000000,
                        gCurSketchSolutionExtractionTime * 1.0 / 1000000,
                        gCurSketchValidationTime * 1.0 / 1000000,
                        gCurSketchRemappingTime * 1.0 / 1000000);
                    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-states|{}",
                                        cftaForSlicing.getStates().size());
                    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-transitions|{}",
                                        cftaForSlicing.getTransitions().size());
                    SPDLOG_LOGGER_TRACE(logger, "|remapping-status|{}|{}|{}",
                                        gTotalMappingChecked,
                                        gTotalMappingCacheHit,
                                        gTotalMappingJumpped);

                    // THIS IS THE CRITICAL POINT TO CHANGE: DO NOT ALLOW SEARCH() TO IMMEDIATELY RETURN UPON FINDING A SATISFACTORY PROGRAM,
                    // MAKE IT FIND EVERY SINGLE SATISFACTORY PROGRAM TO FIND THE FASTEST REWRITE

                    //we return when find the first solution
                    // return {returnPrograms.begin(), returnPrograms.end()};
                  }
                }
              }

              // ! END OF SOLUTION EXTRACTION FOR THIS CONCRETE STATE
              auto equivOverheadEndTime = std::chrono::high_resolution_clock::now();
              equivOverheadTotalTime += std::chrono::duration_cast<std::chrono::nanoseconds>(equivOverheadEndTime - equivOverheadStartTime).count();
            }
          }
        }
      }
      auto end_concretization = std::chrono::high_resolution_clock::now();
      gCurSketchConcretizationTime +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              end_concretization - start_concretization)
              .count();
    }
    auto this_slice_end_time = std::chrono::high_resolution_clock::now();

    // ! END OF CONCRETIZATION OF AFTA SLICE
    auto concretizeSliceEndTime = std::chrono::high_resolution_clock::now();
    concretizeSliceTotalTime = std::chrono::duration_cast<std::chrono::nanoseconds>(concretizeSliceEndTime - concretizeSliceStartTime).count();


    SPDLOG_LOGGER_TRACE(logger, "|per-sketch-statistics|{}|{}|{}|{}|{}|{}|{}",
                        gSliceCost, gSketchID,
                        gCurSketchMatchTime * 1.0 / 1000000,
                        gCurSketchConcretizationTime * 1.0 / 1000000,
                        gCurSketchSolutionExtractionTime * 1.0 / 1000000,
                        gCurSketchValidationTime * 1.0 / 1000000,
                        gCurSketchRemappingTime * 1.0 / 1000000);
    //CFTA built. Now print the statistics of the CFTA
    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-states|{}",
                        cftaForSlicing.getStates().size());
    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-transitions|{}",
                        cftaForSlicing.getTransitions().size());

    SPDLOG_LOGGER_TRACE(logger, "|remapping-status|{}|{}|{}",
                        gTotalMappingChecked, gTotalMappingCacheHit,
                        gTotalMappingJumpped);

    SPDLOG_LOGGER_TRACE(
        logger, "|term-extraction-time-layer-ms|{}|{}", sliceCost,
        (solution_extraction_used_time_ns - termExtractionPrevious) * 1.0 /
            1000000);

    //output the time to concretize all slices up til now, and the afta slicing time
    SPDLOG_LOGGER_TRACE(logger, "|afta-slicing-used-time-accumulated-ms|{}|{}",
                        sliceCost, afta_slicing_used_time_ns * 1.0 / 1000000);
    SPDLOG_LOGGER_TRACE(logger,
                        "|cfta-concretization-used-time-accumulated-ms|{}|{}",
                        sliceCost,
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            this_slice_end_time - synthesis_start_timestamp_ns)
                                .count() *
                            1.0 / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|flush-used-time-accumulated-ms|{}|{}",
                        sliceCost, flush_used_time_ns * 1.0 / 1000000);
    SPDLOG_LOGGER_TRACE(
        logger, "|abstractor-construction-used-time-accumulated-ms|{}|{}",
        sliceCost, abstractor_construction_used_time_ns * 1.0 / 1000000);

    return {returnPrograms.begin(), returnPrograms.end()};
  }

  template <class ConcreteSemanticsType, class BenchmarkType, class GrammarType>
  size_t synthesizeWithSlicing(
      const BenchmarkType& benchmark, const GrammarType& grammar,
      const std::vector<std::string>& hintsForFilterConst,
      const auto& slicingServer, int timeoutInMs,
      foresighter::implementation::sql::CEGISRunner& cegisRunner) {
    using TermType = basics::Term<typename GrammarType::SymbolType>;
    auto timeoutStart = std::chrono::high_resolution_clock::now();
    synthesis_start_timestamp_ns = std::chrono::high_resolution_clock::now();

    auto checkTimeOut = [&timeoutStart, timeoutInMs]() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::high_resolution_clock::now() - timeoutStart)
                 .count() > timeoutInMs;
    };

    SPDLOG_LOGGER_TRACE(logger, "|start-synthesis");

    std::vector<std::string> result;

    emhash8hashmap::HashMap<IDType, IDType> previouslyMappedCFTAState;
    foresighter::implementation::basics::FTA<
        typename GrammarType::SymbolType,
        foresighter::implementation::sql::semantics::values::ConcreteValue>
        cfta;
    emhash8hashmap::HashMap<IDType, emhash8hashset::HashSet<IDType>>
        mappingAbstractStateToConcreteStates;

    ConcreteSemanticsType interpreter(benchmark);

    const char* pgConnEnv = std::getenv("FORESIGHTER_PG_CONNINFO");
    int timingRuns = 5;
    if (const char* r = std::getenv("FORESIGHTER_TIMING_RUNS"))
      timingRuns = std::max(1, std::atoi(r));
    long timingRowBudget = 20000;
    if (const char* rb = std::getenv("FORESIGHTER_TIMING_ROW_BUDGET"))
      timingRowBudget = std::max(1L, std::atol(rb));
    foresighter::implementation::sql::QueryTimer queryTimer(
        pgConnEnv ? std::string(pgConnEnv) : std::string(), timingRuns, logger);
    // Provisions a scratch schema from this benchmark's own JSON-embedded
    // input tables (real table/column names, padded to timingRowBudget rows)
    // so rewrites and the ground truth can be timed without any externally
    // supplied database. No-op if timing is disabled (no conninfo).
    queryTimer.provisionSchema(benchmark.getName(), benchmark, timingRowBudget);

    auto start_abstractor_construction =
        std::chrono::high_resolution_clock::now();
    auto abstractor = foresighter::implementation::sql::semantics::abstractors::
        ConcreteToOfflineConstructionAbstractor(benchmark);
    auto end_abstractor_construction =
        std::chrono::high_resolution_clock::now();
    abstractor_construction_used_time_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_abstractor_construction - start_abstractor_construction)
            .count();

    auto start = std::chrono::high_resolution_clock::now();

    unsigned long long interpreterTimeLastRound = 0;

    //for h = 1, 2, 3, ..., H
    for (auto h = 1; h < 40; ++h) {
      //check timeout
      if (checkTimeOut()) {
        SPDLOG_LOGGER_TRACE(
            logger, "|timeout-for-h|{}|{}", h,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - timeoutStart)
                .count());
        logger->flush();
        logStatistics();
        break;
      }
      SPDLOG_LOGGER_TRACE(logger, "|slice-finding-start|{}", h);
      start = std::chrono::high_resolution_clock::now();

      //this slicing FTA should be modified by adding the projections - it's necessary because then we can do top down restriction propagation.

      auto slice = slicingServer.loadSlicing(benchmark.getName(), h, logger);

      auto slicing = std::make_pair(slice.getFTA().getStates(),
                                    slice.getFTA().getTransitions());
      SPDLOG_LOGGER_TRACE(logger, "|slicing-finding-end|{}|{}|{}|{}", h,
                          slicing.first.size(), slicing.second.size(),
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::high_resolution_clock::now() - start)
                              .count());
      afta_slicing_used_time_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::high_resolution_clock::now() - start)
              .count();

      auto start_flush = std::chrono::high_resolution_clock::now();
      logger->flush();
      auto end_flush = std::chrono::high_resolution_clock::now();
      flush_used_time_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(end_flush -
                                                               start_flush)
              .count();
      start = std::chrono::high_resolution_clock::now();

      long remainingTimeOutInMs =
          timeoutInMs -
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::high_resolution_clock::now() - timeoutStart)
              .count();

      auto startConcretizing = std::chrono::high_resolution_clock::now();
      auto programSatisfyingIOExample =
          findProgramSatisfyingIOExampleFromSlicing<
              foresighter::implementation::sql::semantics::abstractors::
                  ConcreteToOfflineConstructionAbstractor::Lattice>(
              slicing, slice, benchmark, grammar, interpreter, h,
              benchmark.getHints(), remainingTimeOutInMs, cegisRunner,
              previouslyMappedCFTAState, cfta,
              mappingAbstractStateToConcreteStates, queryTimer, abstractor);
      auto endTimeConcretizing = std::chrono::high_resolution_clock::now();
      cfta_concretization_used_time_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              endTimeConcretizing - startConcretizing)
              .count();
      SPDLOG_LOGGER_TRACE(logger, "|concretization-finish|{}|{}", h,
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::high_resolution_clock::now() - start)
                                  .count() *
                              1.0 / 1000000);
      //record interpreter call times
      SPDLOG_LOGGER_TRACE(logger, "|interpreter-calls-layer|{}|{}", h,
                          interpreter.getNumInterpreterCalls());
      SPDLOG_LOGGER_TRACE(logger, "|interpreter-queries-evaluated-layer|{}|{}",
                          h, interpreter.getNumQueriesEvaluated());
      SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-created-layer|{}|{}", h,
                          interpreter.getNumTablesCreated());
      SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-in-cache-layer|{}|{}", h,
                          interpreter.getNumTablesInCache());
      SPDLOG_LOGGER_TRACE(
          logger, "|interpreter-tables-not-created-due-to-cache-layer|{}|{}", h,
          interpreter.getNumTablesNotCreatedDueToCache());
      SPDLOG_LOGGER_TRACE(logger, "|interpreter-total-time-ms-layer|{}|{}", h,
                          (interpreter.getTotalInterpreterTimeInNs() -
                           interpreterTimeLastRound) /
                              1000000);
      interpreterTimeLastRound = interpreter.getTotalInterpreterTimeInNs();
      auto start_flush_at_iteration_end =
          std::chrono::high_resolution_clock::now();
      logger->flush();
      auto end_flush_at_iteration_end =
          std::chrono::high_resolution_clock::now();
      flush_used_time_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              end_flush_at_iteration_end - start_flush_at_iteration_end)
              .count();
      if (!programSatisfyingIOExample.empty()) {
        SPDLOG_LOGGER_TRACE(logger, "|program-found-num|{}|{}", h,
                            programSatisfyingIOExample.size());
        result.insert(result.end(), programSatisfyingIOExample.begin(),
                      programSatisfyingIOExample.end());
        // Do not stop at the first cost level with a solution: keep
        // deepening so higher-cost (larger AST) slices are also searched
        // for a faster-executing rewrite. Termination is now driven solely
        // by checkTimeOut() above / the h<40 bound, not by "found anything yet".
      }
    };

    // std::ofstream dumpFile("/artifact/Foresighter/build/synthesis_debug_output.txt", std::ios::app);
    // if (dumpFile.is_open()) {
    //     dumpFile << "\n========================================\n"
    //              << "BENCHMARK DUMP ATTEMPTED\n"
    //              << "Benchmark Name: " << benchmark.getName() << "\n"
    //              << "CFTA Concretization Time: " << (cfta_concretization_used_time_ns * 1.0 / 1000000) << " ms\n"
    //              << "AFTA Slicing Used Time: " << (afta_slicing_used_time_ns * 1.0 / 1000000) << " ms\n"
    //              << "# of Solutions Found: " << result.size() << "\n"
    //              << "========================================\n";
    //     dumpFile.flush(); // Force the OS to write to disk immediately
    //     dumpFile.close();
    // } else {
    //     // If it can't open in the build directory, try a generic local path
    //     std::ofstream fallbackFile("emergency_dump.txt", std::ios::app);
    //     fallbackFile << "Benchmark Name: " << benchmark.getName() << " | Solutions: " << result.size() << "\n";
    //     fallbackFile.flush();
    //     fallbackFile.close();
    // }

    // ! REPORT THE RESULTS FOR THIS BENCHMARK
    long long origTimeNs = -1;
    if (auto o = queryTimer.medianExecTimeNs(benchmark.getGroundTruth()))
      origTimeNs = *o;
    // Timing is done for this benchmark; drop its scratch schema now rather
    // than holding it open through the stats logging below.
    queryTimer.dropSchema();

    double speedup = (origTimeNs > 0 && fastestRewriteExecTimeNs > 0)
        ? (double)origTimeNs / (double)fastestRewriteExecTimeNs
        : -1.0;

    std::cout
        << "Benchmark: " << benchmark.getName()
        << "\nCFTA Construction Time (ms): " << (concretizeSliceTotalTime / 1e6)
        << "\nSolution Extraction Time (ms): "
        << (solution_extraction_used_time_ns / 1e6)
        << "\nEquivalence Pipeline Overhead (ms): "
        << (equivOverheadTotalTime / 1e6)
        << "\n# Solutions Extracted: " << numberOfProgramsEnumerated
        << "\n# Solutions Passing Equivalence: " << solution_found_num
        << "\nOriginal Exec (ms, median): "
        << (origTimeNs >= 0 ? origTimeNs / 1e6 : -1)
        << "\nBest Rewrite Exec (ms, median): "
        << (fastestRewriteExecTimeNs >= 0 ? fastestRewriteExecTimeNs / 1e6 : -1)
        << "\nSpeedup (orig / best): " << speedup
        << "\nBest Rewrite SQL: "
        << (fastestRewriteSQL.empty() ? "<none timed>" : fastestRewriteSQL)
        << "\n";


    // logging for interpreter
    SPDLOG_LOGGER_TRACE(logger, "|interpreter-calls|{}",
                        interpreter.getNumInterpreterCalls());
    SPDLOG_LOGGER_TRACE(logger, "|interpreter-queries-evaluated|{}",
                        interpreter.getNumQueriesEvaluated());
    SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-created|{}",
                        interpreter.getNumTablesCreated());
    SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-in-cache|{}",
                        interpreter.getNumTablesInCache());
    SPDLOG_LOGGER_TRACE(logger,
                        "|interpreter-tables-not-created-due-to-cache|{}",
                        interpreter.getNumTablesNotCreatedDueToCache());
    SPDLOG_LOGGER_TRACE(logger, "|cfta-concretization-used-time-ms|{}",
                        cfta_concretization_used_time_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|synthesis-total-time-ms|{}",
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::high_resolution_clock::now() -
                            synthesis_start_timestamp_ns)
                                .count() *
                            1.0 / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|interpreter-total-time-ms|{}",
                        interpreter.getTotalInterpreterTimeInNs() / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|afta-slicing-used-time-ms|{}",
                        afta_slicing_used_time_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|afta-refinement-used-time-ms|{}",
                        afta_refinement_used_time_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|afta-pruning-used-time-ms|{}",
                        afta_pruning_used_time_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|solution-extraction-used-time-ms|{}",
                        solution_extraction_used_time_ns / 1000000);

    // ! REPORT THE RESULTS FOR THIS BENCHMARK
    // std::cout << "Benchmark ID: " << benchmarkIdx << "\nCFTA Construction Time: "<< (concretizeSliceTotalTime * 1.0 / 1000000) <<
    // "ms\nSolution Extraction Time: " << (solution_extraction_used_time_ns * 1.0 / 1000000) << "ms\nOverhead for Equivalence Pipeline: "
    // << (equivOverheadTotalTime * 1.0 / 1000000) << "ms\n# of Solutions Extracted: " << numberOfProgramsEnumerated <<
    // "\n# of Solutions That Pass the Equivalence Pipeline: " << solution_found_num << "\nOriginal Performance: " <<
    // "\nBest Solution Performance: " << "\nBest Solution Speed Improvement: " << "\nBest Solution SQL: " << std::endl;

    return result.size();
  }




 private:
  void checkConsistencyWithOutputAndExtractSolutions(
      auto& concreteStateToBeChecked, auto& incomingTransition,
      auto& sketchExtractionCache, const auto& topDownConcretizedFTA,
      auto& validatedConcreteStates, auto& termExtractor, auto& interpreter,
      const auto& cegisRunner, auto& returnPrograms, auto& fta,
      const auto& benchmark) {

    if (sketchExtractionCache.contains(
            {concreteStateToBeChecked, incomingTransition})) {
      return;
    }
    sketchExtractionCache.insert(
        {concreteStateToBeChecked, incomingTransition});
    //for each concreteState, check if it iss consistant with output. If so, extract the terms
    auto& concreteStateManager =
        topDownConcretizedFTA.getFTA().getStateManager();
    auto& concreteStateEntity =
        concreteStateManager.read(concreteStateToBeChecked);

    bool satisfy = false;
    if (validatedConcreteStates.contains(concreteStateToBeChecked)) {
      satisfy = true;
    } else if (auto validMappings =
                   std::get<sql::ConcreteTable>(
                       concreteStateEntity.getValue().getValue())
                       .getConsistentMappingsUnderProjection(
                           benchmark.getIOPairs()[0].getOutput());
               validMappings.size() > 0) {
      satisfy = true;
      validatedConcreteStates[concreteStateToBeChecked] = validMappings;
    }
    if (satisfy) {
      auto& validMappings = validatedConcreteStates[concreteStateToBeChecked];
      //extract the terms.

      auto curSolutionExtractionStart =
          std::chrono::high_resolution_clock::now();
      auto extractedTerms =
          termExtractor
              .extractAcceptedTermsFromStateWithFirstTransitionEnforced(
                  concreteStateToBeChecked, incomingTransition);
      auto curSolutionExtractionEnd = std::chrono::high_resolution_clock::now();
      gCurSketchSolutionExtractionTime +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              curSolutionExtractionEnd - curSolutionExtractionStart)
              .count();
      auto startValidationTime = std::chrono::high_resolution_clock::now();
      for (auto& term : extractedTerms) {
        auto sql = interpreter.getSQLFromTerm(term);
        for (auto& validMapping : validMappings) {
          auto wrappedSQL = wrapSQLWithFinalProjection(sql, validMapping);
          auto cegisRunnerResult = cegisRunner.run(wrappedSQL);
          if (cegisRunnerResult.status != -1) {
            if (cegisRunnerResult.status >= 0) {

            } else {
              //this case, some error occurred. print the error message
              SPDLOG_LOGGER_TRACE(logger, "|CEGISRunner-failed|{}|{}|{}",
                                  cegisRunnerResult.status,
                                  cegisRunnerResult.info, wrappedSQL);
            }
            continue;
          }
          returnPrograms.push_back(wrappedSQL);

          auto solution_found_time = std::chrono::high_resolution_clock::now();

          SPDLOG_LOGGER_TRACE(
              logger,
              "|sql-solution-found|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{"
              "}",
              solution_found_num,  //solution index
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  solution_found_time - synthesis_start_timestamp_ns)
                  .count(),  //total time taken to find the solution in ms -11
              afta_slicing_used_time_ns /
                  1000000,  //time taken for slicing in ms -10
              afta_refinement_used_time_ns /
                  1000000,  //time taken for refinement in ms -9
              afta_pruning_used_time_ns /
                  1000000,  //time taken for pruning in ms -8
              solution_extraction_used_time_ns /
                  1000000,  //time taken for solution extraction in ms, -7
              interpreter.getTotalInterpreterTimeInNs() /
                  1000000,  //time taken for interpreter in ms -6
              cfta_concretization_used_time_ns /
                  1000000,  //time taken for concretization in ms -5
              interpreter
                  .getNumInterpreterCalls(),  //number of interpreter calls -4
              term.getTotalWeight(),          //slice cost -3
              term.getASTSize(),              //size of the AST -2
              wrappedSQL                      //the SQL program -1
          );
          SPDLOG_LOGGER_TRACE(logger, "|sql-solution-found-dsl|{}|",
                              term.toDSLSketchString());
          logStatistics();
          logCacheStatistics(topDownConcretizedFTA, fta);
          ++solution_found_num;
          //log sketch id with stats
          SPDLOG_LOGGER_TRACE(
              logger, "|per-sketch-statistics|{}|{}|{}|{}|{}|{}", gSliceCost,
              gSketchID, gCurSketchMatchTime / 1000000,
              gCurSketchConcretizationTime / 1000000,
              gCurSketchSolutionExtractionTime / 1000000,
              gCurSketchValidationTime / 1000000);
          //we return when we find the first solution
          return;
        }
      }
      auto endValidationTime = std::chrono::high_resolution_clock::now();
      gCurSketchValidationTime +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              endValidationTime - startValidationTime)
              .count();
    }
  }


  class HelperGraphForTopologicalSort {
   public:
    HelperGraphForTopologicalSort() = default;

    void addNode(IDType id) {
      if (adjacencyList.find(id) == adjacencyList.end()) {
        adjacencyList[id] = std::vector<IDType>();
      }
    }

    void addEdge(IDType from, IDType to) {
      addNode(from);
      addNode(to);
      adjacencyList[from].push_back(to);
    }

    void dfs(IDType id, emhash8hashset::HashSet<IDType>& visited,
             std::vector<IDType>& result) {
      visited.insert(id);
      for (auto& neighbor : adjacencyList[id]) {
        if (visited.find(neighbor) == visited.end()) {
          dfs(neighbor, visited, result);
        }
      }
      result.push_back(id);
    }

    std::vector<IDType> getTopologicalSortOrder() {
      std::vector<IDType> result;
      emhash8hashset::HashSet<IDType> visited;
      for (auto& [id, _] : adjacencyList) {
        if (visited.find(id) == visited.end()) {
          dfs(id, visited, result);
        }
      }
      std::reverse(result.begin(), result.end());
      return result;
    }

   private:
    emhash8hashmap::HashMap<IDType, std::vector<IDType>> adjacencyList;
  };

  template <typename FTAType>
  std::vector<IDType> getTopoSortedStatesForTargetStateSlicing(
      IDType targetState, const Slicing& slicing, const FTAType& fta,
      const emhash8hashmap::HashMap<IDType, std::vector<IDType>>&
          transitionsWithStateAsHead) {
    //first, get all states and transitions that are reachable from the target state
    emhash8hashset::HashSet<IDType> reachableStates;
    std::vector<IDType> reachableTransitions;
    std::queue<IDType> q;
    q.push(targetState);
    reachableStates.insert(targetState);
    while (!q.empty()) {
      auto curState = q.front();
      q.pop();
      //this is the leaf node
      if (transitionsWithStateAsHead.find(curState) ==
          transitionsWithStateAsHead.end()) {
        continue;
      }
      auto& transitions = transitionsWithStateAsHead.at(curState);
      for (auto& transition : transitions) {
        reachableTransitions.push_back(transition);
        auto& transitionEntity = fta.getTransitionManager().read(transition);
        auto& tails = transitionEntity.getTails();
        for (auto& tail : tails) {
          if (reachableStates.find(tail) == reachableStates.end()) {
            reachableStates.insert(tail);
            q.push(tail);
          }
        }
      }
    }
    //second, get the topo sorted order of the reachable states
    HelperGraphForTopologicalSort helperGraph;
    for (auto& transition : reachableTransitions) {
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      auto& tails = transitionEntity.getTails();
      auto head = transitionEntity.getHead();
      for (auto& tail : tails) {
        helperGraph.addEdge(tail, head);
      }
    }
    return helperGraph.getTopologicalSortOrder();
  }

  template <typename FTAType>
  emhash8hashmap::HashMap<IDType, std::vector<IDType>>
  buildTransitionHeadMapping(
      const std::pair<std::vector<IDType>, std::vector<IDType>>& slicing,
      const FTAType& fta) {
    emhash8hashmap::HashMap<IDType, std::vector<IDType>>
        transitionsWithStateAsHead;
    for (auto& transition : slicing.second) {
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      auto head = transitionEntity.getHead();
      transitionsWithStateAsHead[head].push_back(transition);
    }
    return transitionsWithStateAsHead;
  }

  template <typename FinalLatticeType, typename FTAType>
  emhash8hashmap::HashMap<sql::Symbol, std::vector<IDType>>
  getTopoSortedStatesForSlicingPerSymbol(
      const std::pair<std::vector<IDType>, std::vector<IDType>>& slicing,
      const FTAType& fta) {

    //first, divide states using their symbols
    emhash8hashmap::HashMap<sql::Symbol, std::vector<IDType>> statesBySymbol;
    for (auto& state : fta.getStates()) {
      auto& stateEntity = fta.getStateManager().read(state);
      statesBySymbol[stateEntity.getSymbol()].push_back(state);
    }
    //second, get topo sorted order for each symbol
    emhash8hashmap::HashMap<sql::Symbol, std::vector<IDType>>
        topoSortedStatesBySymbol;
    for (auto& [symbol, states] : statesBySymbol) {
      topoSortedStatesBySymbol[symbol] =
          getTopoSortedOrderForStates<FinalLatticeType>(states, fta, logger);
    }
    return topoSortedStatesBySymbol;
  }



  void printPercentileSaved(const std::vector<std::pair<long long, long long>>&
                                cartesian_product_info) {
    std::vector<double> saved_percentages;
    for (auto& info : cartesian_product_info) {
      if (info.second > 0) {
        saved_percentages.push_back(1.0 -
                                    (double)info.first / (double)info.second);
      } else {
        saved_percentages.push_back(0);
      }
    }
    if (saved_percentages.empty()) {
      return;
    }
    std::sort(saved_percentages.begin(), saved_percentages.end());
    //print 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 percentile (the size of saved_percentages is unknown though)
    for (int i = 0; i <= 9; i++) {
      SPDLOG_LOGGER_TRACE(logger, "|percentile-cartesian-product-saved|{}|{}",
                          i * 10,
                          saved_percentages[i * saved_percentages.size() / 10]);
    }
    SPDLOG_LOGGER_TRACE(logger, "|percentile-cartesian-product-saved|100|{}",
                        saved_percentages[saved_percentages.size() - 1]);
  }

  void printCartesianProductNum(
      const std::vector<std::pair<long long, long long>>&
          cartesian_product_info) {
    std::vector<long long> cartesian_product_num;
    for (auto& info : cartesian_product_info) {
      cartesian_product_num.push_back(info.second);
    }
    if (cartesian_product_num.empty()) {
      return;
    }
    std::sort(cartesian_product_num.begin(), cartesian_product_num.end());
    for (int i = 0; i <= 9; i++) {
      SPDLOG_LOGGER_TRACE(
          logger, "|percentile-cartesian-product-num|{}|{}", i * 10,
          cartesian_product_num[i * cartesian_product_num.size() / 10]);
    }
    SPDLOG_LOGGER_TRACE(
        logger, "|percentile-cartesian-product-num|100|{}",
        cartesian_product_num[cartesian_product_num.size() - 1]);
  }

  void printStatistics(const auto& data, auto& logger,
                       const std::string& name) {
    //print 0 percentile to 100 percentile of the data
    if (data.empty()) {
      SPDLOG_LOGGER_TRACE(logger, "|{}|empty,nodata", name);
      return;
    }
    //first, sort the data use a vector that has the same type as data
    std::vector<long> sorted_data(data.begin(), data.end());
    std::sort(sorted_data.begin(), sorted_data.end());
    //then, print 0 percentile to 100 percentile of the data
    for (int i = 0; i <= 9; i++) {
      SPDLOG_LOGGER_TRACE(logger, "|{}-{}-percentile|{}", name, i * 10,
                          sorted_data[i * sorted_data.size() / 10]);
    }
    SPDLOG_LOGGER_TRACE(logger, "|{}-100-percentile|{}", name,
                        sorted_data[sorted_data.size() - 1]);
    return;
  }

  void logStatistics() {
    //performance stats
    SPDLOG_LOGGER_TRACE(
        logger, "|time-spent-on-bottom-up-enumeration-ms|{}",
        time_spent_on_bottom_up_enumeration_before_check_ns / 1000000);
    SPDLOG_LOGGER_TRACE(
        logger, "|time-spent-on-bottom-up-enumeration-check-and-caching-ms|{}",
        time_spent_on_bottom_up_enumeration_check_and_caching_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|time-spent-on-abstraction-ms|{}",
                        time_spent_on_abstraction_ns / 1000000);
    SPDLOG_LOGGER_TRACE(logger, "|pruning-power-reduced-unsatisfied-tables|{}",
                        pruning_power_reduced_unsatisfied_tables);
    SPDLOG_LOGGER_TRACE(logger, "|cartesian-product-info-size|{}",
                        cartesian_product_info.size());
    printPercentileSaved(cartesian_product_info);
    printCartesianProductNum(cartesian_product_info);
  }

  template <class TopDownConcretizedFTAType, class AFTAType>
  void logCacheStatistics(
      const TopDownConcretizedFTAType& topDownConcretizedFTA,
      const AFTAType& afta) {
    SPDLOG_LOGGER_TRACE(logger, "|cfta-statistics|");
    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-states|{}",
                        topDownConcretizedFTA.getFTA().getStates().size());
    SPDLOG_LOGGER_TRACE(logger, "|cfta-num-transitions|{}",
                        topDownConcretizedFTA.getFTA().getTransitions().size());
    //cache is of the form (qb, qa) -> [c]

    SPDLOG_LOGGER_TRACE(logger, "|cache-statistics|");
    //question: how many qb is in the cache, comparing to total AFTA states?
    SPDLOG_LOGGER_TRACE(logger, "|total-afta-states|{}",
                        afta.getStates().size());
    SPDLOG_LOGGER_TRACE(
        logger, "|total-num-of-cached-states-in-afta|{}",
        topDownConcretizedFTA.getTotalNumOfCachedStatesInAFTA());
    SPDLOG_LOGGER_TRACE(
        logger, "|total-num-of-cached-refinement-states|{}",
        topDownConcretizedFTA.getTotalNumOfCachedRefinementStates());

    SPDLOG_LOGGER_TRACE(logger, "|num-of-refined-states-per-afta-state|");
    printStatistics(topDownConcretizedFTA.getNumOfRefinedStatesPerAFTAState(),
                    logger, "num-of-refined-states-per-afta-state");
    SPDLOG_LOGGER_TRACE(logger,
                        "|num-of-cached-concrete-states-per-refinement|");
    printStatistics(
        topDownConcretizedFTA.getNumOfCachedConcreteStatesForEachRefinement(),
        logger, "num-of-cached-concrete-states-per-refinement");
  }

  std::shared_ptr<spdlog::logger> logger;
  int solution_found_num = 0;
  unsigned long long cfta_concretization_used_time_ns = 0;
  unsigned long long afta_slicing_used_time_ns = 0;
  unsigned long long afta_refinement_used_time_ns = 0;
  unsigned long long afta_pruning_used_time_ns = 0;
  unsigned long long solution_extraction_used_time_ns = 0;

  unsigned long long time_spent_on_bottom_up_enumeration_before_check_ns = 0;
  unsigned long long time_spent_on_bottom_up_enumeration_check_and_caching_ns =
      0;
  unsigned long long time_spent_on_abstraction_ns = 0;

  unsigned long long time_spent_on_cache_write_ns = 0;
  unsigned long long time_spent_on_cache_read_ns = 0;
  unsigned long long flush_used_time_ns = 0;
  unsigned long long abstractor_construction_used_time_ns = 0;
  std::vector<std::pair<long long, long long>> cartesian_product_info;
  emhash8hashmap::HashMap<IDType, long long>
      before_filtering_concretization_num;

  unsigned long long pruning_power_reduced_unsatisfied_tables = 0;

  class InterpreterCall {
   public:
    InterpreterCall(
        const std::vector<IDType>& afta_states,
        const std::vector<IDType>& cfta_states,
        const std::vector<
            sql::semantics::abstractors::OnlineInverseSemanticsValue>&
            abstract_values,
        const sql::Symbol& symbol)
        : afta_states(afta_states),
          cfta_states(cfta_states),
          abstract_values(abstract_values),
          symbol(symbol) {}

    bool operator==(const InterpreterCall& other) const {
      return afta_states == other.afta_states &&
             cfta_states == other.cfta_states &&
             abstract_values == other.abstract_values && symbol == other.symbol;
    }

    [[nodiscard]] size_t hash() const {
      size_t result = 0;
      for (auto state : afta_states) {
        common::hashCombine(result, state);
      }
      for (auto state : cfta_states) {
        common::hashCombine(result, state);
      }
      for (auto& value : abstract_values) {
        common::hashCombine(result, value);
      }
      common::hashCombine(result, symbol);
      return result;
    }

   private:
    sql::Symbol symbol;
    std::vector<IDType> afta_states;
    std::vector<IDType> cfta_states;
    std::vector<sql::semantics::abstractors::OnlineInverseSemanticsValue>
        abstract_values;
  };


  WeightType gSliceCost = 0;
  IDType gSketchID = 0;
  unsigned long long gCurSketchMatchTime = 0;
  unsigned long long gCurSketchConcretizationTime = 0;
  unsigned long long gCurSketchSolutionExtractionTime = 0;
  unsigned long long gCurSketchValidationTime = 0;
  unsigned long long gTotalMappingChecked = 0;
  unsigned long long gTotalMappingCacheHit = 0;
  unsigned long long gTotalMappingJumpped = 0;
  unsigned long long gCurSketchRemappingTime = 0;

  int soFarMostIndexReached = -1;
  int numberOfProgramsEnumerated = 0;

  // NEW STUFF
  int benchmarkIdx = 0;
  int fastestRewriteIdx = -1;
  long long fastestRewriteExecTimeNs = -1;  // median DB exec time of the winner
  std::string fastestRewriteSQL;            // the fastest verified rewrite
  long long equivOverheadTotalTime = 0;     // ns
  long long concretizeSliceTotalTime = 0;   // ns


  struct InterpreterCallHash {
    size_t operator()(const InterpreterCall& call) const { return call.hash(); }
  };  // namespace foresighter::implementation::algorithms

  emhash8hashset::HashSet<InterpreterCall, InterpreterCallHash>
      cached_interpreter_calls;

  std::chrono::time_point<std::chrono::high_resolution_clock>
      synthesis_start_timestamp_ns;
};
}  // namespace foresighter::implementation::algorithms
