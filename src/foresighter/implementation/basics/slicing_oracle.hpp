#pragma once
/**
 * @file slicing_oracle.hpp
 * @brief Reachability oracle O for fast online slicing of the FTA.
 *
 * Built offline (BuildOracle) by propagating variable-leaf reachability information
 * bottom-up through the FTA in topological order. Each state maps to a CombinationBitVec
 * encoding which leaf-node combinations can reach it. Online, getSlicing() intersects
 * allowed combinations with the oracle to extract a sub-FTA for a given example.
 * Also contains SlicingOracleAblationDirectlyFromFTA for the no-oracle ablation (RQ3).
 * See Section 4 and Algorithm 1 in the paper.
 */
#include <cereal/access.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>
#include <filesystem>
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hash.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/hashset.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/common/roaring.hh>
#include <foresighter/implementation/basics/types.hpp>
#include <foresighter/implementation/basics/variable_controlled_source_set.hpp>
#include <variant>
#include <foresighter/implementation/sql/symbol/nt_query.hpp>
#include <foresighter/implementation/sql/symbol/var_filter_const.hpp>
#include <foresighter/implementation/sql/symbol/var_table.hpp>
#include <foresighter/implementation/sql/symbol.hpp>
#include <fstream>
#include <memory>
#include <set>
#include <stack>
#include <unordered_set>
#include <utility>
#include <vector>
#include "foresighter/common/cartesian_product.hpp"

namespace foresighter::implementation::basics {
using foresighter::common::InvalidArgument;

namespace {

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
    for (auto& neighbor : adjacencyList.at(id)) {
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
  emhash8hashmap::HashMap<IDType, std::vector<IDType>> adjacencyList{};
};

template <class FTA>
std::vector<IDType> getTopologicalSortedStateOrder(
    const FTA& fta, const std::shared_ptr<spdlog::logger>& logger) {
  auto& transitionManager = fta.getTransitionManager();
  HelperGraphForTopologicalSort helperGraph;
  SPDLOG_LOGGER_TRACE(
      logger,
      "Start to get topological sorted order for all states in the FTA");
  for (auto& transition : fta.getTransitions()) {
    auto& readTransition = transitionManager.read(transition);
    auto head = readTransition.getHead();
    auto& tails = readTransition.getTails();
    //for each tail, add (tail, head) to the graph
    for (auto& tail : tails) {
      helperGraph.addEdge(tail, head);
    }
  }
  SPDLOG_LOGGER_TRACE(logger,
                      "Finish constructing helper graph for topological sort.");
  auto topoSortedResult = helperGraph.getTopologicalSortOrder();
  SPDLOG_LOGGER_TRACE(
      logger,
      "Finish getting topological sorted order for all states in "
      "the FTA. Number of states: {}.",
      topoSortedResult.size());
  return topoSortedResult;
}
}  // namespace

class SlicingOracle {
 public:
  using Slice = std::pair<std::vector<IDType>, std::vector<IDType>>;

  void dumpOracleToCsv(const std::string& filePath) const {
    std::ofstream ofs(filePath);
    for (const auto& [id, combination] : idToCombination) {
      ofs << id << "|" << combination.toString() << std::endl;
    }
  }

  static SlicingOracle loadFromFile(
      const std::string& filePath,
      const std::shared_ptr<spdlog::logger>& logger) {
    SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-deserialization-start|");
    SlicingOracle emptyOracle;
    auto start = std::chrono::high_resolution_clock::now();
    std::ifstream ifs(filePath, std::ios::binary);
    {
      cereal::BinaryInputArchive archive(ifs);
      archive(emptyOracle);
    }
    ifs.close();
    auto end = std::chrono::high_resolution_clock::now();
    //print used time in ms
    SPDLOG_LOGGER_TRACE(
        logger, "|slicing-oracle-deserialization-time-ms|{}",
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count());
    //print slicing oracle file size in MB
    SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-file-size-mb|{}",
                        std::filesystem::file_size(filePath) / 1024 / 1024);
    return emptyOracle;
  }

  template <class FTAType>
  static SlicingOracle buildFromFTA(const FTAType& fta,
                                    std::shared_ptr<spdlog::logger> logger) {

    auto config = buildLeafNodesConfigFromFTA(fta, logger);
    SlicingOracle oracle;

    oracle.config = config;

    //build the oracle

    //first, toposort the states
    auto topoSortedOrder = getTopologicalSortedStateOrder(fta, logger);

    int cur = 0;
    //then, follow the topological order to build the oracle
    for (const auto& stateID : topoSortedOrder) {
      auto& state = fta.getStateManager().read(stateID);
      auto& stateSymbol = state.getSymbol();

      SPDLOG_LOGGER_TRACE(logger, "Building distance oracle: {}/{}: {}", cur,
                          topoSortedOrder.size(), stateSymbol.toString());
      ++cur;

      if (stateSymbol.isConstant()) {
        //for constant, no variable is used
        auto defaultCombination = config.getDefaultCombination();
        CombinationBitVec combination;
        combination.addCombination(
            config.encodeCombination(defaultCombination));
        oracle.addEntry(stateID, combination);
        continue;
      }
      if (stateSymbol.isVariable()) {
        if (std::holds_alternative<sql::VarTable>(stateSymbol.getSymbol())) {
          auto varTable = std::get<sql::VarTable>(stateSymbol.getSymbol());
          auto varTableIdx = varTable.getIndex();
          auto varTableLeafNodeIdx =
              config.translateLeafNodeIdxToIdxInVar(varTableIdx, stateID);
          CombinationBitVec combination;
          auto defaultCombination = config.getDefaultCombination();
          defaultCombination[varTableIdx] = varTableLeafNodeIdx;
          combination.addCombination(
              config.encodeCombination(defaultCombination));
          oracle.addEntry(stateID, combination);
          continue;
        } else if (std::holds_alternative<sql::VarFilterConst>(
                       stateSymbol.getSymbol())) {
          auto varFilter =
              std::get<sql::VarFilterConst>(stateSymbol.getSymbol());
          auto varFilterIdx = varFilter.getIndex();
          auto symbolIndexInVar = config.getNumberOfVarTables() + varFilterIdx;
          auto varFilterLeafNodeIdx =
              config.translateLeafNodeIdxToIdxInVar(symbolIndexInVar, stateID);
          CombinationBitVec combination;
          auto defaultCombination = config.getDefaultCombination();
          defaultCombination[symbolIndexInVar] = varFilterLeafNodeIdx;
          combination.addCombination(
              config.encodeCombination(defaultCombination));
          oracle.addEntry(stateID, combination);
          continue;
        }
      }

      //now, the current state is not a constant or variable. We are in the recursive case
      auto transitions = fta.getTransitionsByHead(stateID);
      SPDLOG_LOGGER_TRACE(logger,
                          "State {} with symbol {} has {} transitions as head",
                          stateID, stateSymbol.toString(), transitions.size());

      int curTransition = 0;
      CombinationBitVec combinationResult;
      for (const auto& transition : transitions) {
        if (curTransition % 10000 == 0) {
          SPDLOG_LOGGER_TRACE(logger, "> Dealing with transition {}/{}",
                              curTransition, transitions.size());
        }
        ++curTransition;
        //deal with transition
        auto& transitionEntity = fta.getTransitionManager().read(transition);

        //update headToTailsMapping (this is used for quick online slicing)
        auto& tails = transitionEntity.getTails();
        for (const auto& tail : tails) {
          oracle.headToTails[transitionEntity.getHead()].insert(tail);
        }

        CombinationBitVec combination =
            oracle.getCombination(transitionEntity.getTails()[0]);
        for (int i = 1; i < transitionEntity.getTails().size(); ++i) {
          combination = CombinationBitVec::merge(
              combination,
              oracle.getCombination(transitionEntity.getTails()[i]), config);
        }
        combinationResult.unionWith(combination);
      }
      oracle.addEntry(stateID, combinationResult);
    }

    SPDLOG_LOGGER_TRACE(
        logger, "construction finished. Entry number of slicing oracle:{}",
        oracle.idToCombination.size());
    return oracle;
  }

  template <class FTAType>
  [[nodiscard]] std::pair<std::vector<IDType>, std::vector<IDType>>
  pruneStatesAndTransitions(const std::vector<IDType>& finalStates,
                            const std::vector<IDType>& transitions,
                            const FTAType& fta) const {
    std::vector<IDType> prunedStates;
    std::vector<IDType> prunedTransitions;
    emhash8hashset::HashSet<IDType> allowedTransitions(transitions.begin(),
                                                       transitions.end());

    emhash8hashset::HashSet<IDType> visitedStates;
    std::queue<IDType> stateQueue;
    emhash8hashmap::HashMap<IDType, std::vector<IDType>> headToTransitions;

    for (auto transition : transitions) {
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      headToTransitions[transitionEntity.getHead()].push_back(transition);
    }

    for (auto state : finalStates) {
      stateQueue.push(state);
      visitedStates.insert(state);
      prunedStates.push_back(state);
    }

    while (!stateQueue.empty()) {
      auto state = stateQueue.front();
      stateQueue.pop();
      for (auto transition : headToTransitions[state]) {
        prunedTransitions.push_back(transition);
        auto& transitionEntity = fta.getTransitionManager().read(transition);
        auto& tails = transitionEntity.getTails();
        for (auto tail : tails) {
          if (visitedStates.find(tail) == visitedStates.end()) {
            visitedStates.insert(tail);
            prunedStates.push_back(tail);
            stateQueue.push(tail);
          }
        }
      }
    }
    return {prunedStates, prunedTransitions};
  }

  template <class FTAType>
  [[nodiscard]] Slice getSlicing(
      const basics::VariableControlledSourceSet& variables,
      const std::vector<IDType>& outputStates, int numHints, const FTAType& fta,
      const std::shared_ptr<spdlog::logger>& logger) const {

    CombinationBitVec allowedCombination =
        getAllowedCombinationBitVecFromVarControlledSourceSet(
            variables, numHints, config, logger);

    auto allStatesInTheSlice =
        getAllStatesInTheSlice(outputStates, allowedCombination, fta, logger);

    std::vector<IDType> allOutputStatesInNewSlice;
    for (auto state : outputStates) {
      if (allStatesInTheSlice.find(state) != allStatesInTheSlice.end()) {
        allOutputStatesInNewSlice.push_back(state);
      }
    }

    std::vector<IDType> allStatesInTheSliceVec;
    allStatesInTheSliceVec.reserve(allStatesInTheSlice.size());
    for (auto state : allStatesInTheSlice) {
      allStatesInTheSliceVec.push_back(state);
    }
    auto allTransitionsInTheSlice =
        getAllTransitionsInTheSlice(allStatesInTheSlice, fta);

    auto prunedStatesAndTransitions = pruneStatesAndTransitions(
        allOutputStatesInNewSlice, allTransitionsInTheSlice, fta);
    return {prunedStatesAndTransitions.first,
            prunedStatesAndTransitions.second};
  }

  // Made public for no-oracle ablation entry point (slicing_server_building_ablation)
 public:
  SlicingOracle() = default;

  class LeafNodesConfig {
   public:
    explicit LeafNodesConfig(int numOfVars) {
      varIDToLeafNodeIdx.resize(numOfVars);
    }

    using LeafNodeCombination = std::vector<unsigned>;

    void addLeafNodeForVar(int varID, IDType leafNodeIdx) {
      varIDToLeafNodeIdx[varID].push_back((int)leafNodeIdx);
    }

    [[nodiscard]] size_t totalLeafNodes() const {
      size_t total = 0;
      for (const auto& leafNodes : varIDToLeafNodeIdx) {
        total += leafNodes.size();
      }
      return total;
    }

    [[nodiscard]] uint64_t totalLeafNodesCombinationSpace() const {
      uint64_t total = 1;
      for (const auto& leafNodes : varIDToLeafNodeIdx) {
        total *= (leafNodes.size() + 1);
      }
      return total;
    }

    [[nodiscard]] size_t encodeCombination(
        const LeafNodeCombination& leafNodesInEncodedIdx) const {
      size_t code = 0;
      for (auto i = 0; i < leafNodesInEncodedIdx.size(); ++i) {
        code *= (varIDToLeafNodeIdx.at(i).size() + 1);
        code += leafNodesInEncodedIdx[i];
      }
      return code;
    }

    [[nodiscard]] LeafNodeCombination getDefaultCombination() const {
      LeafNodeCombination defaultCombination(varIDToLeafNodeIdx.size());
      for (auto i = 0; i < varIDToLeafNodeIdx.size(); ++i) {
        defaultCombination[i] = varIDToLeafNodeIdx.at(i).size();
      }
      return defaultCombination;
    }

    [[nodiscard]] LeafNodeCombination decodeCombination(size_t code) const {
      LeafNodeCombination leafNodesInEncodedIdx(varIDToLeafNodeIdx.size());
      for (int i = (int)varIDToLeafNodeIdx.size() - 1; i >= 0; --i) {
        leafNodesInEncodedIdx[i] = code % (varIDToLeafNodeIdx.at(i).size() + 1);
        code /= (varIDToLeafNodeIdx.at(i).size() + 1);
      }
      return leafNodesInEncodedIdx;
    }

    [[nodiscard]] size_t tryMergeCombination(
        const LeafNodeCombination& combination1,
        const LeafNodeCombination& combination2) const {
      LeafNodeCombination mergedCombination(combination1.size());
      for (auto i = 0; i < combination1.size(); ++i) {
        if (combination1[i] == varIDToLeafNodeIdx.at(i).size()) {
          mergedCombination[i] = combination2[i];
        } else if (combination2[i] == varIDToLeafNodeIdx.at(i).size()) {
          mergedCombination[i] = combination1[i];
        } else if (combination1[i] == combination2[i]) {
          mergedCombination[i] = combination1[i];
        } else {
          return -1;
        }
      }
      return encodeCombination(mergedCombination);
    }

    size_t translateLeafNodeIdxToIdxInVar(int varID, IDType leafNodeIdx) const {
      auto& leafNodes = varIDToLeafNodeIdx[varID];
      auto idx = std::find(leafNodes.begin(), leafNodes.end(), leafNodeIdx);
      if (idx == leafNodes.end()) {
        throw InvalidArgument("Leaf node index not found");
      }
      return std::distance(leafNodes.begin(), idx);
    }

    [[nodiscard]] size_t getNumberOfVars() const {
      return varIDToLeafNodeIdx.size();
    }

    [[nodiscard]] size_t getNumberOfLeafNodesForVar(int varID) const {
      return varIDToLeafNodeIdx[varID].size();
    }

    [[nodiscard]] size_t getNumberOfVarTables() const { return numOfVarTables; }

    void setNumberOfVarTables(size_t numOfVarTables) {
      this->numOfVarTables = (int)numOfVarTables;
    }

   private:
    friend class SlicingOracle;

    LeafNodesConfig() = default;
    std::vector<std::vector<int>> varIDToLeafNodeIdx;

    int numOfVarTables = 0;

    friend class cereal::access;

    template <class Archive>
    void serialize(Archive& ar) {
      ar(varIDToLeafNodeIdx, numOfVarTables);
    }
  };

  class CombinationBitVec {
   public:
    void addCombination(const LeafNodesConfig::LeafNodeCombination& combination,
                        const LeafNodesConfig& config) {
      size_t code = config.encodeCombination(combination);
      bitmap.add(code);
    }

    void addCombination(size_t code) { bitmap.add(code); }

    [[nodiscard]] bool intersectNotEmpty(const CombinationBitVec& other) const {
      return bitmap.intersect(other.bitmap);
    }

    void unionWith(const CombinationBitVec& other) { bitmap |= other.bitmap; }

    uint64_t size() const { return bitmap.cardinality(); }

    [[nodiscard]] bool contains(size_t code) const {
      return bitmap.contains(code);
    }

    [[nodiscard]] std::string toString() const { return bitmap.toString(); }

    static CombinationBitVec merge(const CombinationBitVec& vec1,
                                   const CombinationBitVec& vec2,
                                   const LeafNodesConfig& config) {
      CombinationBitVec merged;
      for (auto id_a : vec1.bitmap) {
        auto combination_a = config.decodeCombination(id_a);
        for (auto id_b : vec2.bitmap) {
          auto combination_b = config.decodeCombination(id_b);
          auto merged_combination =
              config.tryMergeCombination(combination_a, combination_b);
          if (merged_combination != -1) {
            merged.addCombination(merged_combination);
          }
        }
      }
      return merged;
    }

    // Made public for no-oracle ablation entry point
    roaring::Roaring bitmap;

   private:
    friend class cereal::access;

    template <class Archive>
    void save(Archive& ar) const {
      std::size_t expected_size_in_bytes = bitmap.getSizeInBytes();
      std::vector<char> buffer(expected_size_in_bytes);
      std::size_t size_in_bytes = bitmap.write(buffer.data());
      ar(expected_size_in_bytes, buffer);
    }

    template <class Archive>
    void load(Archive& ar) {
      std::size_t size_in_bytes = 0;
      ar(size_in_bytes);
      std::vector<char> buffer(size_in_bytes);
      ar(buffer);
      bitmap = roaring::Roaring::readSafe(buffer.data(), size_in_bytes);
    }
  };

  // 8-param overload: takes allowedCombination directly (used by state_map-based slicing)
  template <class FTAType>
  [[nodiscard]] Slice getSlicing(
      const basics::VariableControlledSourceSet& variables,
      const std::vector<IDType>& outputStates, int numHints, const FTAType& fta,
      const CombinationBitVec& allowedCombination,
      const std::shared_ptr<spdlog::logger>& logger, auto cost,
      auto& topoSortedSymbols) const {

    auto allStatesInTheSlice =
        getAllStatesInTheSlice(outputStates, allowedCombination, fta, logger);

    std::vector<IDType> allOutputStatesInNewSlice;
    allOutputStatesInNewSlice.reserve(outputStates.size());
    for (auto state : outputStates) {
      if (allStatesInTheSlice.find(state) != allStatesInTheSlice.end()) {
        allOutputStatesInNewSlice.push_back(state);
      }
    }

    std::vector<IDType> allStatesInTheSliceVec;
    allStatesInTheSliceVec.reserve(allStatesInTheSlice.size());
    for (auto state : allStatesInTheSlice) {
      allStatesInTheSliceVec.push_back(state);
    }
    auto allTransitionsInTheSlice =
        getAllTransitionsInTheSlice(allStatesInTheSlice, fta);

    auto prunedStatesAndTransitions = pruneStatesAndTransitions(
        allOutputStatesInNewSlice, allTransitionsInTheSlice, fta);
    return {prunedStatesAndTransitions.first,
            prunedStatesAndTransitions.second};
  }

  void addEntry(IDType id, CombinationBitVec combination) {
    idToCombination[id] = std::move(combination);
  }

  [[nodiscard]] CombinationBitVec getCombination(IDType id) const {
    return idToCombination.at(id);
  }

  emhash8hashmap::HashMap<IDType, CombinationBitVec> idToCombination;
  emhash8hashmap::HashMap<IDType, emhash8hashset::HashSet<IDType>> headToTails;
  LeafNodesConfig config;

  template <class FTAType>
  static LeafNodesConfig buildLeafNodesConfigFromFTA(
      const FTAType& fta, const std::shared_ptr<spdlog::logger>& logger) {
    LeafNodesConfig config;
    SPDLOG_LOGGER_TRACE(logger, "|oracle-leaf-nodes-config-building-start|");
    auto start = std::chrono::high_resolution_clock::now();
    emhash8hashmap::HashMap<IDType, std::vector<IDType>> varTableToStateIDs;
    emhash8hashmap::HashMap<IDType, std::vector<IDType>> varFilterToStateIDs;

    auto& stateManager = fta.getStateManager();
    for (auto targetID : fta.getStates()) {
      auto& state = stateManager.read(targetID);
      auto& stateSymbol = state.getSymbol();

      if (stateSymbol.isVariable()) {
        if (std::holds_alternative<sql::VarTable>(stateSymbol.getSymbol())) {
          auto varTable = std::get<sql::VarTable>(stateSymbol.getSymbol());
          varTableToStateIDs[varTable.getIndex()].push_back(targetID);
        } else if (std::holds_alternative<sql::VarFilterConst>(
                       stateSymbol.getSymbol())) {
          auto varFilter =
              std::get<sql::VarFilterConst>(stateSymbol.getSymbol());
          varFilterToStateIDs[varFilter.getIndex()].push_back(targetID);
        } else {
          throw InvalidArgument("Unknown variable symbol type" +
                                stateSymbol.toString());
        }
      }
    }

    //export (key, value) pairs in varTableToStateIDs
    std::vector<std::pair<IDType, std::vector<IDType>>> varTableToStateIDsVec;
    for (const auto& [key, value] : varTableToStateIDs) {
      varTableToStateIDsVec.emplace_back(key, value);
    }
    std::sort(varTableToStateIDsVec.begin(), varTableToStateIDsVec.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    //export (key, value) pairs in varFilterToStateIDs
    std::vector<std::pair<IDType, std::vector<IDType>>> varFilterToStateIDsVec;
    for (const auto& [key, value] : varFilterToStateIDs) {
      varFilterToStateIDsVec.emplace_back(key, value);
    }
    std::sort(varFilterToStateIDsVec.begin(), varFilterToStateIDsVec.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    //log the number of variables
    for (const auto& [key, value] : varTableToStateIDsVec) {
      SPDLOG_LOGGER_TRACE(logger,
                          "|oracle-leaf-nodes-vartable-idx-length|{}|{}", key,
                          value.size());
    }
    for (const auto& [key, value] : varFilterToStateIDsVec) {
      SPDLOG_LOGGER_TRACE(logger,
                          "|oracle-leaf-nodes-varfilter-idx-length|{}|{}", key,
                          value.size());
    }

    //build the config
    int numOfVarTables = (int)varTableToStateIDsVec.size();
    int numOfVarFilters = (int)varFilterToStateIDsVec.size();
    config.varIDToLeafNodeIdx.resize(numOfVarTables + numOfVarFilters);
    for (const auto& [key, value] : varTableToStateIDsVec) {
      for (const auto& stateID : value) {
        config.addLeafNodeForVar((int)key, stateID);
      }
    }
    for (const auto& [key, value] : varFilterToStateIDsVec) {
      for (const auto& stateID : value) {
        config.addLeafNodeForVar((int)key + numOfVarTables, stateID);
      }
    }

    config.setNumberOfVarTables(numOfVarTables);

    auto end = std::chrono::high_resolution_clock::now();
    SPDLOG_LOGGER_TRACE(
        logger, "|oracle-leaf-nodes-config-building-time-ms|{}",
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count());
    return config;
  }

  [[nodiscard]] static CombinationBitVec
  getAllowedCombinationBitVecFromVarControlledSourceSet(
      const basics::VariableControlledSourceSet& variables, int numHints,
      const LeafNodesConfig& config,
      const std::shared_ptr<spdlog::logger>& logger) {

    std::vector<std::vector<IDType>> allowedLeafNodes;

    SPDLOG_LOGGER_TRACE(logger, "|oracle-allowed-combination-building-start|");
    SPDLOG_LOGGER_TRACE(logger, "|num-vars-in-config|{}",
                        config.getNumberOfVars());
    SPDLOG_LOGGER_TRACE(logger, "|num-vartables-in-config|{}",
                        config.getNumberOfVarTables());
    SPDLOG_LOGGER_TRACE(
        logger, "|num-varfilters-in-config|{}",
        config.getNumberOfVars() - config.getNumberOfVarTables());

    //first, assemble the varTable
    for (int i = 0; i < config.getNumberOfVarTables(); ++i) {
      std::vector<IDType> allowedLeafNodesForVar;
      if (variables.getSourceSet()[i].has_value()) {
        allowedLeafNodesForVar.push_back(config.translateLeafNodeIdxToIdxInVar(
            i, variables.getSourceSet()[i].value()));
        //we can also not using this variable. In this case that's the "empty" leaf node
        allowedLeafNodesForVar.push_back(config.getNumberOfLeafNodesForVar(i));
      } else {
        //we are not allowed to use this variable. In this case that's the "empty" leaf node
        allowedLeafNodesForVar.push_back(config.getNumberOfLeafNodesForVar(i));
      }
      allowedLeafNodes.push_back(allowedLeafNodesForVar);
    }

    //second, assemble the varFilter
    for (int i = 0;
         i < config.getNumberOfVars() - config.getNumberOfVarTables(); ++i) {
      std::vector<IDType> allowedLeafNodesForVar;
      if (i < numHints) {
        //we are allowed to use this variable. We can choose to use it or not
        allowedLeafNodesForVar.push_back(0);
        allowedLeafNodesForVar.push_back(1);
      } else {
        //we are not allowed to use this variable. In this case that's the "empty" leaf node
        allowedLeafNodesForVar.push_back(1);
      }
      allowedLeafNodes.push_back(allowedLeafNodesForVar);
    }

    //third, assemble the combination (get all cartesian product, and then encode it, and then union the combinationBitVec)
    CombinationBitVec allowedCombination;
    auto iter = common::CartesianProductIterator(allowedLeafNodes);
    SPDLOG_LOGGER_TRACE(logger,
                        "|oracle-allowed-combination-cartesian-product|{}",
                        iter.size());
    while (iter.hasNext()) {
      auto combination = iter.next();
      allowedCombination.addCombination(combination, config);
    }
    return allowedCombination;
  }

  template <class FTAType>
  [[nodiscard]] emhash8hashset::HashSet<IDType> getAllStatesInTheSlice(
      const std::vector<IDType>& outputStates,
      const CombinationBitVec& allowedCombination, const FTAType& fta,
      const std::shared_ptr<spdlog::logger>& logger) const {
    emhash8hashset::HashSet<IDType> statesInSlice;
    emhash8hashset::HashSet<IDType> visited;
    std::queue<IDType> queue;
    //initial case of the queue
    for (auto state : outputStates) {
      if (allowedCombination.intersectNotEmpty(idToCombination.at(state))) {
        visited.insert(state);
        statesInSlice.insert(state);
        queue.push(state);
      }
    }
    SPDLOG_LOGGER_TRACE(logger, "|oracle-bfs-start|{}", queue.size());
    //BFS. The states in queue will always satisfy:
    //1. it is not visited before, and it is not in the slice
    //2. it passes the combination check
    while (!queue.empty()) {
      auto state = queue.front();
      queue.pop();
      auto& statesToAddNext = headToTails.at(state);
      for (auto stateToAddNext : statesToAddNext) {
        //first, check if the state is visited
        if (visited.find(stateToAddNext) != visited.end()) {
          continue;
        }
        //second, check if the state passes the combination check
        if (allowedCombination.intersectNotEmpty(
                idToCombination.at(stateToAddNext))) {
          visited.insert(stateToAddNext);
          statesInSlice.insert(stateToAddNext);
          queue.push(stateToAddNext);
        }
      }
    }
    return statesInSlice;
  }

  template <class FTAType>
  [[nodiscard]] std::vector<IDType> getAllTransitionsInTheSlice(
      const emhash8hashset::HashSet<IDType>& allStates,
      const FTAType& fta) const {
    //add all transitions such that
    //1. all the children are in the slice
    //2. the target is in the slice
    std::vector<IDType> transitionsInSlice;
    auto& transitionManager = fta.getTransitionManager();
    for (auto state : allStates) {
      for (auto transition : fta.getTransitionsByHead(state)) {
        std::vector<IDType> children =
            transitionManager.read(transition).getTails();
        bool allFound = true;
        for (auto child : children) {
          if (allStates.find(child) == allStates.end()) {
            allFound = false;
            break;
          }
        }
        if (allFound) {
          transitionsInSlice.push_back(transition);
        }
      }
    }
    return transitionsInSlice;
  }

  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(idToCombination, headToTails, config);
  }
};

// Ablation class: slicing without oracle guidance (for no-oracle ablation study, RQ3)
class SlicingOracleAblationDirectlyFromFTA {
 public:
  using Slice = std::pair<std::vector<IDType>, std::vector<IDType>>;

  template <class FTAType>
  [[nodiscard]] Slice getSlicing(
      const basics::VariableControlledSourceSet& variables,
      const std::vector<IDType>& outputStates, int numHints, const FTAType& fta,
      const auto& allowedCombination,
      const std::shared_ptr<spdlog::logger>& logger, WeightType cost,
      const auto& topoSortedSymbols) const {
    if (outputStates.empty()) {
      return {std::vector<IDType>(), std::vector<IDType>()};
    }
    auto start = std::chrono::high_resolution_clock::now();
    Slice bottomUpSlicing =
        getBottomUpSlicing(variables, outputStates, numHints, fta, cost,
                           topoSortedSymbols, logger);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    SPDLOG_LOGGER_TRACE(logger, "|bottom-up-slicing-time-ms|{}|{}", cost,
                        duration.count());

    SPDLOG_LOGGER_TRACE(logger, "|debug-bottom-up-slicing|{}|{}|{}", cost,
                        bottomUpSlicing.first.size(),
                        bottomUpSlicing.second.size());

    auto start2 = std::chrono::high_resolution_clock::now();
    Slice topDownPrunedSlicing =
        getTopDownPrunedSlicing(outputStates, bottomUpSlicing, fta);
    auto end2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration2 = end2 - start2;
    SPDLOG_LOGGER_TRACE(logger, "|top-down-pruned-slicing-time-ms|{}|{}", cost,
                        duration2.count());
    SPDLOG_LOGGER_TRACE(logger, "|debug-top-down-pruned-slicing|{}|{}",
                        topDownPrunedSlicing.first.size(),
                        topDownPrunedSlicing.second.size());

    return topDownPrunedSlicing;
  }

 private:
  template <class FTAType>
  [[nodiscard]] Slice getBottomUpSlicing(
      const basics::VariableControlledSourceSet& variables,
      const std::vector<IDType>& outputStates, int numHints, const FTAType& fta,
      WeightType cost, const auto& topoSortedSymbols,
      const std::shared_ptr<spdlog::logger>& logger) const {
    emhash8hashset::HashSet<IDType> addedStates;
    emhash8hashset::HashSet<IDType> addedTransitions;

    emhash8hashmap::HashMap<sql::Symbol, std::vector<IDType>> statesBySymbol;
    auto allVariableStates = variables.getSourceSet();
    for (auto& state : allVariableStates) {
      if (state.has_value()) {
        addedStates.insert(state.value());
        auto& symbol = fta.getStateManager().read(state.value()).getSymbol();
        statesBySymbol[symbol].push_back(state.value());
      }
    }
    for (auto& symbol : topoSortedSymbols) {
      if (symbol.isConstant()) {
        auto& states = fta.getStatesBySymbol(symbol);
        for (auto& state : states) {
          if (fta.getStateManager().read(state).getCost() <= cost) {
            addedStates.insert(state);
            statesBySymbol[symbol].push_back(state);
          }
        }
      } else if (symbol.isVariable()) {
        if (std::holds_alternative<sql::VarTable>(symbol.getSymbol())) {
        } else if (std::holds_alternative<sql::VarFilterConst>(
                       symbol.getSymbol())) {
          if (std::get<sql::VarFilterConst>(symbol.getSymbol()).getIndex() <
              numHints) {
            for (auto& state : fta.getStatesBySymbol(symbol)) {
              if (fta.getStateManager().read(state).getCost() <= cost) {
                addedStates.insert(state);
                statesBySymbol[symbol].push_back(state);
              }
            }
          }
        } else {
          throw InvalidArgument("Invalid symbol type");
        }
      }

      auto& states = statesBySymbol[symbol];
      emhash8hashset::HashSet<IDType> transitionsInvolvedThisLayer;
      for (auto& state : states) {
        auto& transitionsWithStateBeingChild = fta.getTransitionsByTail(state);
        transitionsInvolvedThisLayer.insert(
            transitionsWithStateBeingChild.begin(),
            transitionsWithStateBeingChild.end());
      }
      for (auto& transition : transitionsInvolvedThisLayer) {
        auto& transitionEntity = fta.getTransitionManager().read(transition);
        auto head = transitionEntity.getHead();
        if (fta.getStateManager().read(head).getCost() > cost) {
          continue;
        }
        auto& tails = transitionEntity.getTails();
        bool allChildrenAdded = true;
        for (auto& tail : tails) {
          if (addedStates.find(tail) == addedStates.end()) {
            allChildrenAdded = false;
            break;
          }
        }
        if (allChildrenAdded) {
          addedStates.insert(transitionEntity.getHead());
          statesBySymbol[fta.getStateManager()
                             .read(transitionEntity.getHead())
                             .getSymbol()]
              .push_back(transitionEntity.getHead());
          addedTransitions.insert(transition);
        }
      }
    }
    std::vector<IDType> statesInTheSlice;
    std::vector<IDType> transitionsInTheSlice;
    statesInTheSlice.reserve(addedStates.size());
    transitionsInTheSlice.reserve(addedTransitions.size());
    for (auto& state : addedStates) {
      statesInTheSlice.push_back(state);
    }
    for (auto& transition : addedTransitions) {
      transitionsInTheSlice.push_back(transition);
    }
    return {statesInTheSlice, transitionsInTheSlice};
  }

  template <class FTAType>
  [[nodiscard]] Slice getTopDownPrunedSlicing(
      const std::vector<IDType>& outputStates, const Slice& slice,
      const FTAType& fta) const {
    emhash8hashmap::HashMap<IDType, std::vector<IDType>> headToTransitions;
    emhash8hashset::HashSet<IDType> statesInOutput(slice.first.begin(),
                                                   slice.first.end());
    for (auto& transition : slice.second) {
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      headToTransitions[transitionEntity.getHead()].push_back(transition);
    }
    std::set<IDType> prunedStates;
    std::set<IDType> prunedTransitions;

    std::queue<IDType> stateQueue;
    for (auto& state : outputStates) {
      if (statesInOutput.find(state) == statesInOutput.end()) {
        continue;
      }
      stateQueue.push(state);
      prunedStates.insert(state);
    }
    while (!stateQueue.empty()) {
      auto state = stateQueue.front();
      stateQueue.pop();
      for (auto& transition : headToTransitions[state]) {
        prunedTransitions.insert(transition);
        auto& transitionEntity = fta.getTransitionManager().read(transition);
        auto& tails = transitionEntity.getTails();
        for (auto& tail : tails) {
          if (prunedStates.find(tail) == prunedStates.end()) {
            prunedStates.insert(tail);
            stateQueue.push(tail);
          }
        }
      }
    }
    std::vector<IDType> prunedStatesVec;
    std::vector<IDType> prunedTransitionsVec;
    prunedStatesVec.reserve(prunedStates.size());
    prunedTransitionsVec.reserve(prunedTransitions.size());
    for (auto& state : prunedStates) {
      prunedStatesVec.push_back(state);
    }
    for (auto& transition : prunedTransitions) {
      prunedTransitionsVec.push_back(transition);
    }
    return {prunedStatesVec, prunedTransitionsVec};
  }
};

}  // namespace foresighter::implementation::basics