/**
 * @file grammar_generator.hpp
 * @brief Generates Grammar<Symbol> from JSON configuration settings.
 *
 * Reads SQL grammar parameters (max columns, enabled operators, etc.) from a JSON
 * settings file and constructs the corresponding context-free grammar with all
 * productions for the SQL synthesis domain.
 * @author Rui Dong
 * @date 4/17/24
 */

#include <boost/json.hpp>
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/algorithms/unroll_grammar.hpp>
#include <foresighter/implementation/sql/types.hpp>
#include <fstream>
#include <string>
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/symbol.hpp"
#include "foresighter/implementation/sql/symbol/nt_column_index.hpp"
#include "foresighter/implementation/sql/symbol/nt_column_index_interval.hpp"
#include "foresighter/implementation/sql/symbol/nt_group_by_list.hpp"
#include "foresighter/implementation/sql/symbol/nt_join_predicate_clause.hpp"
#include "foresighter/implementation/sql/symbol/nt_projection_list.hpp"
#include "foresighter/implementation/sql/symbol/nt_select_predicate.hpp"
#include "foresighter/implementation/sql/symbol/nt_select_predicate_clause.hpp"
#include "foresighter/implementation/sql/symbol/op_group_by_list.hpp"
#include "foresighter/implementation/sql/symbol/op_group_by_order_by_count_limit.hpp"
#include "foresighter/implementation/sql/symbol/op_group_without_key.hpp"
#include "foresighter/implementation/sql/symbol/op_identity.hpp"
#include "foresighter/implementation/sql/symbol/op_index_in_interval.hpp"
#include "foresighter/implementation/sql/symbol/op_inner_join.hpp"
#include "foresighter/implementation/sql/symbol/op_projection_distinct.hpp"
#pragma once

namespace foresighter::implementation::sql {
struct SQLGrammarSettings {
  //idx range
  int MaxColIdx;
  int MaxTableIdx;
  int MaxSymbolicConstantNum;

  //list range
  int MaxProjectionListLength;
  int MaxAggListLength;
  int MaxWindowListLength;
  int MaxGroupByListLength;
  int MaxFilterPredicateNum;

  //operators
  bool EnableProjection;
  bool EnableFilter;
  bool EnableGroupWithoutKey;
  bool EnableGroupWithKey;
  bool EnableWindow;
  bool EnableInnerJoin;
  bool EnableLeftJoin;

  //aggregation types
  bool EnableSumAgg;
  bool EnableAvgAgg;
  bool EnableCountAgg;
  bool EnableMinAgg;
  bool EnableMaxAgg;

  //window types
  bool EnableWindowFunctionPartitionOrderBy;
  bool EnableWindowFunctionNoPartitionOrderBy;
  bool EnableWindowFunctionRankPartition;
  bool EnableWindowFunctionRankNoPartition;

  //window function types
  bool EnableWindowFunctionMax;
  bool EnableWindowFunctionMin;
  bool EnableWindowFunctionSum;
  bool EnableWindowFunctionCount;

  //order by
  bool EnableOrderByAsc;
  bool EnableOrderByDesc;

  //expansion limit
  uint32_t QExpansionLimit;
  uint32_t JoinClauseExpansionLimit;
  uint32_t MaxProjectDistinctExpansionTimes;
  uint32_t MaxProjectDistinctMaxExpansionTimes;
  uint32_t MaxGroupByExpansionTimes;
  uint32_t MaxJoinPredicateExpansionTimes;

  [[nodiscard]] inline std::string toString() const {
    std::string Result = "-----Grammar Settings-----\n";
    Result += "MaxColIdx: " + std::to_string(MaxColIdx) + "\n";
    Result += "MaxTableIdx: " + std::to_string(MaxTableIdx) + "\n";
    Result +=
        "MaxProjectionListLength: " + std::to_string(MaxProjectionListLength) +
        "\n";
    Result += "MaxAggListLength: " + std::to_string(MaxAggListLength) + "\n";
    Result +=
        "MaxWindowListLength: " + std::to_string(MaxWindowListLength) + "\n";
    Result +=
        "MaxGroupByListLength: " + std::to_string(MaxGroupByListLength) + "\n";
    Result +=
        "EnableProjection: " + std::to_string(EnableGroupWithoutKey) + "\n";
    Result +=
        "EnableGroupWithoutKey: " + std::to_string(EnableGroupWithoutKey) +
        "\n";
    Result +=
        "EnableGroupWithKey: " + std::to_string(EnableGroupWithKey) + "\n";
    Result += "EnableWindow: " + std::to_string(EnableWindow) + "\n";
    Result += "EnableInnerJoin: " + std::to_string(EnableInnerJoin) + "\n";
    Result += "EnableLeftJoin: " + std::to_string(EnableLeftJoin) + "\n";
    Result += "EnableSumAgg: " + std::to_string(EnableSumAgg) + "\n";
    Result += "EnableAvgAgg: " + std::to_string(EnableAvgAgg) + "\n";
    Result += "EnableCountAgg: " + std::to_string(EnableCountAgg) + "\n";
    Result += "EnableMinAgg: " + std::to_string(EnableMinAgg) + "\n";
    Result += "EnableMaxAgg: " + std::to_string(EnableMaxAgg) + "\n";
    Result += "EnableWindowFunctionPartitionOrderBy: " +
              std::to_string(EnableWindowFunctionPartitionOrderBy) + "\n";
    Result += "EnableWindowFunctionNoPartitionOrderBy: " +
              std::to_string(EnableWindowFunctionNoPartitionOrderBy) + "\n";
    Result += "EnableWindowFunctionRankPartition: " +
              std::to_string(EnableWindowFunctionRankPartition) + "\n";
    Result += "EnableWindowFunctionRankNoPartition: " +
              std::to_string(EnableWindowFunctionRankNoPartition) + "\n";
    Result +=
        "EnableWindowFunctionMax: " + std::to_string(EnableWindowFunctionMax) +
        "\n";
    Result +=
        "EnableWindowFunctionMin: " + std::to_string(EnableWindowFunctionMin) +
        "\n";
    Result +=
        "EnableWindowFunctionSum: " + std::to_string(EnableWindowFunctionSum) +
        "\n";
    Result += "EnableWindowFunctionCount: " +
              std::to_string(EnableWindowFunctionCount) + "\n";
    Result += "EnableOrderByAsc: " + std::to_string(EnableOrderByAsc) + "\n";
    Result += "EnableOrderByDesc: " + std::to_string(EnableOrderByDesc) + "\n";
    Result += "QExpansionLimit: " + std::to_string(QExpansionLimit) + "\n";
    Result += "JoinClauseExpansionLimit: " +
              std::to_string(JoinClauseExpansionLimit) + "\n";
    Result += "MaxProjectDistinctExpansionTimes: " +
              std::to_string(MaxProjectDistinctExpansionTimes) + "\n";
    Result += "MaxProjectDistinctMaxExpansionTimes: " +
              std::to_string(MaxProjectDistinctMaxExpansionTimes) + "\n";
    Result += "MaxGroupByExpansionTimes: " +
              std::to_string(MaxGroupByExpansionTimes) + "\n";
    Result += "--------------------------\n";
    return Result;
  }
};

inline std::vector<std::vector<Symbol>> generateIncreasingColumnIndices(
    int size, int max) {
  std::vector<std::vector<int>> result;
  std::vector<int> combination(size);

  int min = 0;
  // Initialize the first combination
  for (int i = 0; i < size; ++i) {
    combination[i] = min + i;
  }

  while (true) {
    result.push_back(combination);

    int index = size - 1;
    while (index >= 0 && combination[index] == max - (size - 1 - index)) {
      --index;
    }

    if (index < 0) {
      break;
    }

    ++combination[index];
    for (int i = index + 1; i < size; ++i) {
      combination[i] = combination[i - 1] + 1;
    }
  }
  std::vector<std::vector<Symbol>> returnResult;
  for (auto& combination : result) {
    std::vector<Symbol> symbolCombination;
    symbolCombination.reserve(combination.size());
    for (auto& index : combination) {
      symbolCombination.push_back((Symbol)ConstColumnIndex(index));
    }
    returnResult.push_back(symbolCombination);
  }

  return returnResult;
}

inline Grammar generateGrammarFromSettings(const SQLGrammarSettings& settings) {
  Grammar result;
  //Q -> Table(MaxTableIdx)
  for (auto i = 0; i < settings.MaxTableIdx; ++i) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpIdentity(),
                         {(Symbol)VarTable(i)}, 1);
  }
  //Q -> ProjectDistinct(Q, Lg)
  if (settings.EnableProjection) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpProjectionDistinct(),
                         {(Symbol)NtQuery(), (Symbol)NtGroupByList()}, 3);
  }
  //Q -> OpOrderByColLimit(Q, NtOrderByList)
  if (settings.EnableProjection) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpOrderByColLimit(),
                         {(Symbol)NtQuery(), (Symbol)NtOrderByList()}, 3);
  }
  //Q -> OpGroupByOrderByCountLimit(Q, NtOrderByList)
  if (settings.EnableProjection) {
    result.addProduction((Symbol)NtQuery(),
                         (Symbol)OpGroupByOrderByCountLimit(),
                         {(Symbol)NtQuery(), (Symbol)NtOrderByList()}, 3);
  }
  //Q -> GroupWithoutKey(Q, La)
  if (settings.EnableGroupWithoutKey) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpGroupWithoutKey(),
                         {(Symbol)NtQuery(), (Symbol)NtAggregationList()}, 3);
  }

  //Q -> GroupWithKey(Q, Lg, La)
  if (settings.EnableGroupWithKey) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpGroupWithKey(),
                         {(Symbol)NtQuery(), (Symbol)NtGroupByList(),
                          (Symbol)NtAggregationList()},
                         2);
  }

  //Q -> Filter(Q, Phi_S)
  if (settings.EnableFilter) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpSelect(),
                         {(Symbol)NtQuery(), (Symbol)NtSelectPredicate()}, 1);
  }

  //Q -> Window(Q, Lw)
  if (settings.EnableWindow) {
    result.addProduction((Symbol)NtQuery(), (Symbol)OpWindow(),
                         {(Symbol)NtQuery(), (Symbol)NtWindowFunctionList()},
                         4);
  }

  //Q -> InnerJoin(Q, Q, Phi_j)
  if (settings.EnableInnerJoin) {
    result.addProduction(
        (Symbol)NtQuery(), (Symbol)OpInnerJoin(),
        {(Symbol)NtQuery(), (Symbol)NtQuery(), (Symbol)NtJoinPredicate()}, 3);
  }

  //Q -> LeftJoin(Q, Q, Phi_j)
  if (settings.EnableLeftJoin) {
    result.addProduction(
        (Symbol)NtQuery(), (Symbol)OpLeftJoin(),
        {(Symbol)NtQuery(), (Symbol)NtQuery(), (Symbol)NtJoinPredicate()}, 3);
  }

  if (settings.EnableGroupWithKey || settings.EnableGroupWithoutKey) {
    //La -> ListAgg(Agg,...)
    for (auto i = 1; i <= settings.MaxAggListLength; ++i) {
      result.addProduction((Symbol)NtAggregationList(), (Symbol)OpAggList(),
                           std::vector<Symbol>(i, (Symbol)NtAggregation()), 1);
    }
  }

  if (settings.EnableProjection) {
    //Lp -> ListProj(Col,...)
    for (auto i = 1; i <= settings.MaxProjectionListLength; ++i) {
      result.addProduction(
          (Symbol)NtProjectionList(), (Symbol)OpProjList(),
          std::vector<Symbol>(i, (Symbol)NtColumnIndexInterval()), 1);
    }
  }

  if (settings.EnableGroupWithKey) {
    //Lg -> ListGroup(Col,...)
    for (auto i = 1; i <= settings.MaxGroupByListLength; ++i) {
      result.addProduction(
          (Symbol)NtGroupByList(), (Symbol)OpGroupByList(),
          std::vector<Symbol>(i, (Symbol)NtColumnIndexInterval()), 1);
    }
    //NtOrderedGroupByList -> OpGroupByList()
    //here, we aim to break the symmetry of the group by column indices
    // for (auto i = 1; i <= settings.MaxGroupByListLength; ++i) {
    //   auto columnIndices =
    //       generateIncreasingColumnIndices(i, settings.MaxColIdx);
    //   for (auto& indices : columnIndices) {
    //     result.addProduction((Symbol)NtOrderedGroupByList(),
    //                          (Symbol)OpGroupByList(), indices, 1 + i);
    //   }
    // }
  }

  //Lw -> List(w, ...)
  if (settings.EnableWindow) {
    for (auto i = 1; i <= settings.MaxWindowListLength; ++i) {
      result.addProduction((Symbol)NtWindowFunctionList(),
                           (Symbol)OpWindowList(),
                           std::vector<Symbol>(i, (Symbol)NtWindowClause()), 0);
    }
  }

  if (settings.EnableInnerJoin || settings.EnableLeftJoin) {
    result.addProduction((Symbol)NtJoinPredicate(), (Symbol)OpIdentity(),
                         {(Symbol)NtJoinPredicateClause()}, 1);

    //Phi_j -> \Phi_j /\ Phi_j
    // result.addProduction((Symbol)NtJoinPredicate(), (Symbol)OpJoinAnd(),
    //                      {(Symbol)NtJoinPredicate(), (Symbol)NtJoinPredicate()},
    //                      0);

    //phi_j -> c = c
    result.addProduction(
        (Symbol)NtJoinPredicateClause(), (Symbol)OpJoinEq(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtColumnIndexInterval()}, 1);
  }

  //Aggregation -> MAX/MIN/SUM/COUNT/AVG(c)
  if (settings.EnableGroupWithoutKey or settings.EnableGroupWithKey) {
    if (settings.EnableMaxAgg) {
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpMax(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableMinAgg) {
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpMin(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableSumAgg) {
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpSum(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableCountAgg) {
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpCount(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpCountDistinct(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableAvgAgg) {
      result.addProduction((Symbol)NtAggregation(), (Symbol)OpAvg(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
  }

  //window clause
  if (settings.EnableWindow) {
    // w -> WindowFunc(fw, c, c, K)
    if (settings.EnableWindowFunctionPartitionOrderBy) {
      result.addProduction(
          (Symbol)NtWindowClause(),
          (Symbol)OpWindowFunctionWithPartitionOrder(),
          {(Symbol)NtWindowFunction(), (Symbol)NtColumnIndexInterval(),
           (Symbol)NtColumnIndexInterval(), (Symbol)NtOrderKey()},
          1);
    }
    // w -> WindowFunc(fw, c, K)
    if (settings.EnableWindowFunctionNoPartitionOrderBy) {
      result.addProduction(
          (Symbol)NtWindowClause(), (Symbol)OpRankNoPartitionOrder(),
          {(Symbol)NtWindowFunction(), (Symbol)NtColumnIndexInterval(),
           (Symbol)NtOrderKey()},
          1);
    }
    // w -> Rank(k)
    if (settings.EnableWindowFunctionRankNoPartition) {
      result.addProduction((Symbol)NtWindowClause(),
                           (Symbol)OpRankNoPartitionOrder(),
                           {(Symbol)NtOrderKey()}, 1);
    }
    // w -> Rank(c, k)
    if (settings.EnableWindowFunctionRankPartition) {
      result.addProduction(
          (Symbol)NtWindowClause(), (Symbol)OpRankWithPartitionOrder(),
          {(Symbol)NtColumnIndexInterval(), (Symbol)NtOrderKey()}, 1);
    }
  }

  //fw -> MAX | MIN | COUNT | SUM
  if (settings.EnableWindow) {
    if (settings.EnableWindowFunctionMax) {
      result.addProduction((Symbol)NtWindowFunction(), (Symbol)OpMax(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableWindowFunctionMin) {
      result.addProduction((Symbol)NtWindowFunction(), (Symbol)OpMin(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableWindowFunctionCount) {
      result.addProduction((Symbol)NtWindowFunction(), (Symbol)OpCount(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
    if (settings.EnableWindowFunctionSum) {
      result.addProduction((Symbol)NtWindowFunction(), (Symbol)OpSum(),
                           {(Symbol)NtColumnIndexInterval()}, 1);
    }
  }

  //NtOrderByList -> OpOrderByDesc(NtColumnIndexInterval)
  result.addProduction((Symbol)NtOrderByList(), (Symbol)OpOrderByDesc(),
                       {(Symbol)NtColumnIndexInterval()}, 1);
  //NtOrderByList -> OpOrderByAsc(NtColumnIndexInterval)
  result.addProduction((Symbol)NtOrderByList(), (Symbol)OpOrderByAsc(),
                       {(Symbol)NtColumnIndexInterval()}, 1);

  // K -> Asc(c) | Desc(c)
  if (settings.EnableOrderByAsc) {
    result.addProduction((Symbol)NtOrderKey(), (Symbol)OpAsc(),
                         {(Symbol)NtColumnIndexInterval()}, 1);
  }
  if (settings.EnableOrderByDesc) {
    result.addProduction((Symbol)NtOrderKey(), (Symbol)OpDesc(),
                         {(Symbol)NtColumnIndexInterval()}, 1);
  }

  result.addProduction((Symbol)NtColumnIndexInterval(),
                       (Symbol)OpIndexInInterval(), {(Symbol)NtColumnIndex()},
                       0);
  //c -> <0> | ...
  for (auto i = 0; i < settings.MaxColIdx; ++i) {
    result.addProduction((Symbol)NtColumnIndex(), (Symbol)OpIdentity(),
                         {(Symbol)ConstColumnIndex(i)}, 1);
  }

  //Select Predicate related
  if (settings.EnableFilter) {
    //Phi_S -> phi_S
    result.addProduction((Symbol)NtSelectPredicate(), (Symbol)OpIdentity(),
                         {(Symbol)NtSelectPredicateClause()}, 1);

    //Phi_S -> Phi_S \/ Phi_S
    result.addProduction(
        (Symbol)NtSelectPredicate(), (Symbol)OpFilterOr(),
        {(Symbol)NtSelectPredicate(), (Symbol)NtSelectPredicate()}, 1);

    //phi_S -> phi_S /\ phi_S
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterAnd(),
        {(Symbol)NtSelectPredicateClause(), (Symbol)NtSelectPredicateClause()},
        1);

    //phi_S -> NtColumnIndexInterval = NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterEq(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndexInterval != NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterNeq(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndexInterval > NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterGt(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndexInterval < NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterLt(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndex < NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterLike(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndexInterval >= NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterGeq(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);
    //phi_S -> NtColumnIndexInterval <= NtFilterConstant
    result.addProduction(
        (Symbol)NtSelectPredicateClause(), (Symbol)OpFilterLeq(),
        {(Symbol)NtColumnIndexInterval(), (Symbol)NtFilterConstant()}, 1);

    // NtFilterConstant -> {0} | ...
    for (auto i = 0; i < settings.MaxSymbolicConstantNum; ++i) {
      result.addProduction((Symbol)NtFilterConstant(), (Symbol)OpIdentity(),
                           {(Symbol)VarFilterConst(i)}, 1);
    }
  }

  result.addToStartSymbols((Symbol)NtQuery());

  auto unrolledGrammar = algorithms::convertToDAG(
      result, {1, 2,
               foresighter::implementation::sql::constants::Constants::
                   slicingOracleSetting.maxExactDistance});

  return unrolledGrammar;
}

/**
 * @brief This function gets the SQLGrammarSettings from a json file
 * @details The json file should have the following format:
 * {
 *
 * "MaxTableIdx": <int>,
 * "MaxColIdx": <int>,
 * "MaxSymbolicConstantNum": <int>,
 *
 * "MaxProjectionListLength": <int>,
 * "MaxAggListLength": <int>,
 * "MaxWindowListLength": <int>,
 * "MaxGroupByListLength": <int>,
* "MaxFilterPredicateNum": <int>,
 *
 * "EnableProjection": <bool>,
 * "EnableFilter": <bool>,
 * "EnableGroupWithoutKey": <bool>,
 * "EnableGroupWithKey": <bool>,
 * "EnableWindow": <bool>,
 * "EnableInnerJoin": <bool>,
 * "EnableLeftJoin": <bool>,
 *
 * "EnableSumAgg": <bool>,
 * "EnableAvgAgg": <bool>,
 * "EnableCountAgg": <bool>,
 * "EnableMinAgg": <bool>,
 * "EnableMaxAgg": <bool>,
 *
 * "EnableWindowFunctionPartitionOrderBy": <bool>,
 * "EnableWindowFunctionNoPartitionOrderBy": <bool>,
 * "EnableWindowFunctionRankPartition": <bool>,
 * "EnableWindowFunctionRankNoPartition": <bool>,
 *
 * "EnableWindowFunctionMax": <bool>,
 * "EnableWindowFunctionMin": <bool>,
 * "EnableWindowFunctionSum": <bool>,
 * "EnableWindowFunctionCount": <bool>,
 *
 * "EnableOrderByAsc": <bool>,
 * "EnableOrderByDesc": <bool>,
 *
 * "QExpansionLimit": <int>,
 * "JoinClauseExpansionLimit": <int>
 * }
 * @param jsonFile
 * @return a parsed SQLGrammarSettings
 */
inline SQLGrammarSettings getGrammarSettingsFromJson(
    const std::string& jsonFile) {
  std::ifstream ifs(jsonFile);
  if (!ifs.is_open()) {
    throw foresighter::common::InvalidArgument("Failed to open json file: " +
                                               jsonFile);
  }
  auto json = boost::json::parse(ifs);
  SQLGrammarSettings settings{};
  settings.MaxTableIdx = (int)json.at("MaxTableIdx").as_int64();
  settings.MaxColIdx = (int)json.at("MaxColIdx").as_int64();
  settings.MaxSymbolicConstantNum =
      (int)json.at("MaxSymbolicConstantNum").as_int64();
  settings.MaxProjectionListLength =
      (int)json.at("MaxProjectionListLength").as_int64();
  settings.MaxAggListLength = (int)json.at("MaxAggListLength").as_int64();
  settings.MaxWindowListLength = (int)json.at("MaxWindowListLength").as_int64();
  settings.MaxGroupByListLength =
      (int)json.at("MaxGroupByListLength").as_int64();
  settings.MaxFilterPredicateNum =
      (int)json.at("MaxFilterPredicateNum").as_int64();
  settings.EnableProjection = json.at("EnableProjection").as_bool();
  settings.EnableFilter = json.at("EnableFilter").as_bool();
  settings.EnableGroupWithoutKey = json.at("EnableGroupWithoutKey").as_bool();
  settings.EnableGroupWithKey = json.at("EnableGroupWithKey").as_bool();
  settings.EnableWindow = json.at("EnableWindow").as_bool();
  settings.EnableInnerJoin = json.at("EnableInnerJoin").as_bool();
  settings.EnableLeftJoin = json.at("EnableLeftJoin").as_bool();
  settings.EnableSumAgg = json.at("EnableSumAgg").as_bool();
  settings.EnableAvgAgg = json.at("EnableAvgAgg").as_bool();
  settings.EnableCountAgg = json.at("EnableCountAgg").as_bool();
  settings.EnableMinAgg = json.at("EnableMinAgg").as_bool();
  settings.EnableMaxAgg = json.at("EnableMaxAgg").as_bool();
  settings.EnableWindowFunctionPartitionOrderBy =
      json.at("EnableWindowFunctionPartitionOrderBy").as_bool();
  settings.EnableWindowFunctionNoPartitionOrderBy =
      json.at("EnableWindowFunctionNoPartitionOrderBy").as_bool();
  settings.EnableWindowFunctionRankPartition =
      json.at("EnableWindowFunctionRankPartition").as_bool();
  settings.EnableWindowFunctionRankNoPartition =
      json.at("EnableWindowFunctionRankNoPartition").as_bool();
  settings.EnableWindowFunctionMax =
      json.at("EnableWindowFunctionMax").as_bool();
  settings.EnableWindowFunctionMin =
      json.at("EnableWindowFunctionMin").as_bool();
  settings.EnableWindowFunctionSum =
      json.at("EnableWindowFunctionSum").as_bool();
  settings.EnableWindowFunctionCount =
      json.at("EnableWindowFunctionCount").as_bool();
  settings.EnableOrderByAsc = json.at("EnableOrderByAsc").as_bool();
  settings.EnableOrderByDesc = json.at("EnableOrderByDesc").as_bool();
  settings.QExpansionLimit = json.at("QExpansionLimit").as_int64();
  settings.JoinClauseExpansionLimit =
      json.at("JoinClauseExpansionLimit").as_int64();
  settings.MaxProjectDistinctExpansionTimes =
      json.at("MaxProjectDistinctExpansionTimes").as_int64();
  settings.MaxProjectDistinctMaxExpansionTimes =
      json.at("MaxProjectDistinctMaxExpansionTimes").as_int64();
  settings.MaxGroupByExpansionTimes =
      json.at("MaxGroupByExpansionTimes").as_int64();
  settings.MaxJoinPredicateExpansionTimes =
      json.at("MaxJoinPredicateExpansionTimes").as_int64();

  return settings;
}

}  // namespace foresighter::implementation::sql
