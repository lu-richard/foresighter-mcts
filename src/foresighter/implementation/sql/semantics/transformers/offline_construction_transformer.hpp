#pragma once
/**
 * @file offline_construction_transformer.hpp
 * @brief Abstract predicate propagation for offline FTA construction (Fig. 9).
 *
 * Implements the abstract transformer that propagates predicates (column types,
 * subset-of-input, number-of-columns, row-num ranges) through SQL operators.
 * Each operator's abstract semantics computes which predicates hold on the output
 * given predicates on the inputs, enabling sound over-approximation in the FTA.
 */

#include <sys/queue.h>
#include <boost/system/result.hpp>
#include <foresighter/common/cartesian_product.hpp>
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/variant.hpp>
#include <foresighter/implementation/basics/lattice.hpp>
#include <foresighter/implementation/basics/value.hpp>
#include <foresighter/implementation/sql/abstract_value/bottom.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_num_of_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_ordered_tuple.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_select_nth_column.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_subset_of_input.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_total_n_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_used_nth_column.hpp>
#include <foresighter/implementation/sql/abstract_value/top.hpp>
#include <foresighter/implementation/sql/constants.hpp>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include "foresighter/implementation/basics/term.hpp"
#include "foresighter/implementation/basics/transformer.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_col_type.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_constant_type.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_row_num_range.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_select_interval_columns.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/grammar_generator.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"
#include "foresighter/implementation/sql/symbol.hpp"
#include "foresighter/implementation/sql/symbol/const_column_index.hpp"
#include "foresighter/implementation/sql/symbol/op_projection.hpp"
#include "foresighter/implementation/sql/symbol/var_filter_const.hpp"
#include "foresighter/implementation/sql/symbol/var_table.hpp"

namespace foresighter::implementation::sql::semantics::transformers {
using namespace foresighter::implementation::basics;

class OfflineConstructionTransformer : Transformer {
 public:
  using SymbolType = foresighter::implementation::sql::Symbol;
  using ValueType = values::OfflineConstructionValue;
  using OperatorType = SymbolType;
  using LatticeType = Lattice<ValueType>;
  using TermType = foresighter::implementation::basics::Term<SymbolType>;
  using ContextType = Context<SymbolType, ValueType>;

  // a special case for terminal symbols in offline construction
  // its special because it is a non-deterministic transformer

  static std::vector<ValueType> getInitialValues(
      const SymbolType& symbol, const std::optional<SQLBenchmark>& benchmark,
      int numOfPredicates) {
    auto symbolVariant = symbol.getSymbol();
    std::vector<ValueType> result;

    std::visit(
        common::overloaded{
            // Var and constant handlers
            [&](const ConstColumnIndex& op) {
              result.emplace_back(std::in_place_type<PredicateSelectNthColumn>,
                                  op.getIndex());
            },
            [&](const VarFilterConst& op) {
              auto filterIdx =
                  std::get<VarFilterConst>(symbolVariant).getIndex();
              if (!benchmark || filterIdx < (*benchmark).getHints().size()) {
                result.emplace_back(LatticeType::top());
              }
            },
            [&](const VarTable& op) {
              auto tableIdx = std::get<VarTable>(symbolVariant).getIndex();
              if (benchmark) {
                result = varTableHandlerGivenBenchmark(
                    (int)tableIdx, *benchmark, numOfPredicates);
              } else {
                result = varTableHandler((int)tableIdx, numOfPredicates);
              }
            },
            [&](const auto& op) {
              result = {};
            }},
        symbolVariant);
    return result;
  }

  static std::vector<int> getValidThresFromTable(const ValueType& value) {
    auto hashValue = getColumnHashValue(value);
    if (hashValue.size() == 0) {
      return {0,
              static_cast<int>(
                  constants::Constants::sqlSetting.maxNumberOfColumnsInInput)};
    }
    int numOfCols = (int)hashValue.size();
    std::vector<int> result = {0};
    int cur = 0;
    while (cur < numOfCols) {
      while (cur < numOfCols - 1 && hashValue[cur] == hashValue[cur + 1])
        cur++;
      result.push_back(++cur);
    }
    return result;
  }

  static std::vector<std::pair<int, int>> getValidIntervalsFromTable(
      const ValueType& value) {
    int numOfCols = -1;
    std::vector<int> hashValue;
    if (std::holds_alternative<ValueType::ConjunctionType>(value.getValue())) {
      auto& conjunction =
          std::get<ValueType::ConjunctionType>(value.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateNumOfColumns>(predicate)) {
          numOfCols =
              std::get<PredicateNumOfColumns>(predicate).getNumOfColumn();
        }
      }
    } else if (std::holds_alternative<PredicateNumOfColumns>(
                   value.getValue())) {
      numOfCols =
          std::get<PredicateNumOfColumns>(value.getValue()).getNumOfColumn();
    }
    if (numOfCols == -1) {
      return {std::make_pair(
          0, constants::Constants::sqlSetting.maxNumberOfColumnsInInput - 1)};
    }

    hashValue.resize(numOfCols);
    if (std::holds_alternative<ValueType::ConjunctionType>(value.getValue())) {
      auto& conjunction =
          std::get<ValueType::ConjunctionType>(value.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateSubsetOfInput>(predicate)) {
          auto value = std::get<PredicateSubsetOfInput>(predicate);
          auto inputTableIdx = value.getInputTableIdx();
          auto idxInCur = value.getIdxInCur();
          hashValue[idxInCur] += inputTableIdx + 1;
        }
        if (std::holds_alternative<PredicateColType>(predicate)) {
          auto value = std::get<PredicateColType>(predicate);
          auto colIdx = value.getColIdx();
          auto colType = value.getColType();
          hashValue[colIdx] += (colType + 1) * 100;
        }
      }
    }

    std::vector<std::pair<int, int>> result;
    int last = -1, cur = 0;
    while (cur < numOfCols) {
      while (cur < numOfCols - 1 && hashValue[cur] == hashValue[cur + 1])
        cur++;
      result.emplace_back(last + 1, cur);
      last = cur++;
    }

    return result;
  }

  static std::vector<std::pair<ValueType, std::vector<int>>> transform(
      const SymbolType& symbol, const std::vector<ValueType>& values,
      int numOfPredicates) {

    std::vector<ValueType> result;
    if (std::holds_alternative<OpIdentity>(symbol.getSymbol())) {
      result = values;
    }
    // Handle conjunction cases first
    // else if (checkConjunction(values)) {
    //   result.push_back(handleConjunction(symbol, values));
    // }
    // Handle bottom cases
    else if (checkBottom(values)) {
      return {};
    }
    // Handle atomic cases
    else {
      result = atomicHandler(symbol, values, numOfPredicates);
    }
    std::vector<std::pair<ValueType, std::vector<int>>> results;
    results.reserve(result.size());
    for (const auto& value : result) {
      // for projection in the last step, we don't need to order the columns
      if (std::holds_alternative<OpProjection>(symbol.getSymbol())) {
        results.push_back(getUnorderedValue(value));
      } else {
        results.push_back(getOrderedValue(value));
      }
    }
    return results;
  }

  //expose this to abstractor so that we can get the ordered value there

  static std::pair<ValueType, std::vector<int>> getOrderedValue(
      const ValueType& unorderedValue) {
    auto hashValue = getColumnHashValue(unorderedValue);
    if (hashValue.empty()) {
      std::vector<int> emptyOrder{};
      return std::make_pair(unorderedValue, emptyOrder);
    }
    int numOfCols = (int)hashValue.size();
    std::vector<int> order;
    order.resize(numOfCols);
    hashValue.resize(numOfCols);
    for (int i = 0; i < numOfCols; ++i) {
      order[i] = i;
    }

    ValueType rowNumPredicate = LatticeType::top();
    if (std::holds_alternative<ValueType::ConjunctionType>(
            unorderedValue.getValue())) {
      auto conjunction =
          std::get<ValueType::ConjunctionType>(unorderedValue.getValue())
              .getConjunction();
      for (const auto& predicate : conjunction) {
        if (std::holds_alternative<PredicateSubsetOfInput>(predicate)) {
          auto value = std::get<PredicateSubsetOfInput>(predicate);
          auto inputTableIdx = value.getInputTableIdx();
          auto idxInCur = value.getIdxInCur();
          hashValue[idxInCur] += inputTableIdx + 1;
        }
        if (std::holds_alternative<PredicateColType>(predicate)) {
          auto value = std::get<PredicateColType>(predicate);
          auto colIdx = value.getColIdx();
          auto colType = value.getColType();
          hashValue[colIdx] += (colType + 1) * 100;
        }
        if (std::holds_alternative<PredicateRowNumRange>(predicate)) {
          rowNumPredicate = LatticeType::meet(
              rowNumPredicate,
              ValueType(std::get<PredicateRowNumRange>(predicate)));
        }
      }
    }

    std::sort(order.begin(), order.end(), [&hashValue](int i, int j) {
      return hashValue[i] < hashValue[j];
    });
    std::vector<int> tmp;
    tmp.resize(numOfCols);
    for (int i = 0; i < numOfCols; ++i) {
      tmp[order[i]] = i;
    }

    ValueType orderedValue =
        ValueType{std::in_place_type<PredicateNumOfColumns>, numOfCols};

    if (std::holds_alternative<ValueType::ConjunctionType>(
            unorderedValue.getValue())) {
      auto conjunction =
          std::get<ValueType::ConjunctionType>(unorderedValue.getValue())
              .getConjunction();
      for (const auto& predicate : conjunction) {
        if (std::holds_alternative<PredicateSubsetOfInput>(predicate)) {
          auto value = std::get<PredicateSubsetOfInput>(predicate);
          auto inputTableIdx = value.getInputTableIdx();
          auto idxInCur = value.getIdxInCur();
          orderedValue = LatticeType::meet(
              orderedValue,
              ValueType{std::in_place_type<PredicateSubsetOfInput>,
                        inputTableIdx, tmp[idxInCur]});
        }
        if (std::holds_alternative<PredicateColType>(predicate)) {
          auto value = std::get<PredicateColType>(predicate);
          auto colIdx = value.getColIdx();
          auto colType = value.getColType();
          orderedValue = LatticeType::meet(
              orderedValue, ValueType{std::in_place_type<PredicateColType>,
                                      tmp[colIdx], colType});
        }
      }
    }
    orderedValue = LatticeType::meet(orderedValue, ValueType(rowNumPredicate));
    return std::make_pair(orderedValue, order);
  }

 private:
  static bool checkConjunction(const std::vector<ValueType>& values) {
    return std::any_of(values.begin(), values.end(), [](const auto& value) {
      return std::holds_alternative<ValueType::ConjunctionType>(
          value.getValue());
    });
  }

  static bool checkBottom(const std::vector<ValueType>& values) {
    return std::any_of(values.begin(), values.end(), [](const auto& value) {
      return std::holds_alternative<Bottom>(value.getValue());
    });
  }

  static bool containNumOfColumns(const auto& value) {
    //check if it is top
    if (std::holds_alternative<Top>(value)) {
      return false;
    }
    //check if it contains NumOfColumns
    if (std::holds_alternative<PredicateNumOfColumns>(value)) {
      return true;
    }
    if (std::holds_alternative<ValueType::ConjunctionType>(value)) {
      auto conjunction =
          std::get<ValueType::ConjunctionType>(value).getConjunction();
      for (const auto& predicate : conjunction) {
        if (std::holds_alternative<PredicateNumOfColumns>(predicate)) {
          return true;
        }
      }
    }
    return false;
  }

  // NtQuery : TotalNColumns /\ {SubsetOfInput} /\ {PredicateColType} /\ RowNumRange
  // NtOrderedGroupByList : OrderedTuple({SelectIntervalColumns})
  // NtColumnIndexInterval : SelectIntervalColumns
  // NtColumnIndex : SelectNthColumn
  // NtFilterConstantIndex : Top
  // NtProjectionList : OrderedTuple({SelectIntervalColumns})
  // NtGroupByList : OrderedTuple({SelectIntervalColumns})
  // NtOrderByList : SelectIntervalColumns
  // NtJoinPredicateClause : OrderedTuple(SelectIntervalColumns, SelectIntervalColumns)
  // NtAggregation : SelectIntervalColumns
  // NtSelectPredicateClause : SelectIntervalColumns
  // NtAggregationList : OrderedTuple({SelectIntervalColumns})
  // NtJoinPredicate : OrderedTuple(SelectIntervalColumns, SelectIntervalColumns)
  // NtSelectPredicate : SelectIntervalColumns
  static std::vector<ValueType> atomicHandler(
      const SymbolType& op, const std::vector<ValueType>& values,
      int numOfPredicates) {

    auto result = LatticeType::top();
    if (std::holds_alternative<OpIndexInInterval>(op.getSymbol())) {
      return indexInIntervalSoundHandler(op, values, numOfPredicates);
    }
    std::visit(
        common::overloaded{
            // Project handlers
            [&](const OpProjection&) {
              result = LatticeType::meet(projectSoundHandler(op, values),
                                         rowNumEqualHandler(values[0]));
            },
            [&](const OpProjectionDistinct&) {
              result = LatticeType::meet(projectSoundHandler(op, values),
                                         rowNumLEHandler(values[0]));
            },
            [&](const OpGroupByOrderByCountLimit&) {
              result = LatticeType::meet(projectSoundHandler(op, values),
                                         rowNumOneHandler(values[0]));
            },

            // Select handlers
            [&](const OpSelect&) {
              result = LatticeType::meet(selectSoundHandler(op, values),
                                         rowNumLEHandler(values[0]));
            },
            [&](const OpOrderByColLimit&) {
              result = LatticeType::meet(selectSoundHandler(op, values),
                                         rowNumOneHandler(values[0]));
            },

            // Group handlers
            [&](const OpGroupWithoutKey&) {
              result = LatticeType::meet(groupNcSoundHandler(op, values),
                                         rowNumOneHandler(values[0]));
            },
            [&](const OpGroupWithKey&) {
              result = LatticeType::meet(groupCSoundHandler(op, values),
                                         rowNumLEPositiveHandler(values[0]));
            },

            // Join handlers
            [&](const OpInnerJoin&) {
              result = LatticeType::meet(
                  joinSoundHandler(op, values),
                  rowNumInnerJoinHandler(values[0], values[1]));
            },
            [&](const OpLeftJoin&) {
              result = LatticeType::meet(
                  joinSoundHandler(op, values),
                  rowNumLeftJoinHandler(values[0], values[1]));
            },

            // Aggregation handlers
            [&](const OpMin&) { result = aggMinMaxSoundHandler(op, values); },
            [&](const OpMax&) { result = aggMinMaxSoundHandler(op, values); },
            [&](const OpCount&) { result = aggCountSoundHandler(op, values); },
            [&](const OpCountDistinct&) {
              result = aggCountSoundHandler(op, values);
            },
            [&](const OpSum&) { result = aggSumAvgSoundHandler(op, values); },
            [&](const OpAvg&) { result = aggSumAvgSoundHandler(op, values); },
            [&](const OpAggList&) {
              result = aggListSoundHandler(op, values, numOfPredicates);
            },

            // Filter handlers
            [&](const OpFilterEq&) { result = filterSoundHandler(op, values); },
            [&](const OpFilterNeq&) {
              result = filterSoundHandler(op, values);
            },
            [&](const OpFilterGt&) { result = filterSoundHandler(op, values); },
            [&](const OpFilterLt&) { result = filterSoundHandler(op, values); },
            [&](const OpFilterLike&) {
              result = filterSoundHandler(op, values);
            },
            [&](const OpFilterGeq&) {
              result = filterSoundHandler(op, values);
            },
            [&](const OpFilterLeq&) {
              result = filterSoundHandler(op, values);
            },

            [&](const OpFilterAnd&) {
              result = filterConjunctionDisjunctionSoundHandler(
                  op, values, numOfPredicates);
            },
            [&](const OpFilterOr&) {
              result = filterConjunctionDisjunctionSoundHandler(
                  op, values, numOfPredicates);
            },

            // List handlers
            [&](const OpProjList&) {
              result = projListSoundHandler(op, values, numOfPredicates);
            },
            [&](const OpGroupByList&) {
              result = projListSoundHandler(op, values, numOfPredicates);
            },
            [&](const OpJoinEq&) { result = joinEqSoundHandler(op, values); },
            [&](const OpOrderByAsc&) {
              result = orderByListSoundHandler(op, values);
            },
            [&](const OpOrderByDesc&) {
              result = orderByListSoundHandler(op, values);
            },

            // Default handler
            [&](const auto&) {
              throw common::InvalidArgument("Unexpected symbol. Symbol name: " +
                                            op.toString());
            }},
        op.getSymbol());
    return {result};
  }

  static std::vector<ValueType> varTableHandler(int tableIdx,
                                                int numOfPredicates) {
    if (numOfPredicates == 0) {
      return {LatticeType::top()};
    }
    auto colNumberMaxLimit =
        constants::Constants::sqlSetting.maxNumberOfColumnsInInput;
    std::vector<ValueType> result;
    for (int numOfCols = 1;
         numOfCols <=
         constants::Constants::sqlSetting.maxNumberOfColumnsInInput;
         ++numOfCols) {
      std::vector<ValueType::ConjunctionType::VariantType> val;
      if (numOfPredicates >= 2) {
        val.emplace_back(std::in_place_type<PredicateNumOfColumns>, numOfCols);
      }
      if (numOfPredicates >= 3) {
        for (int i = 0; i < numOfCols; ++i) {
          val.emplace_back(std::in_place_type<PredicateSubsetOfInput>, tableIdx,
                           i);
        }
      }
      if (numOfPredicates >= 1) {
        std::vector<int> tmp(
            constants::Constants::slicingOracleSetting.maxTableIdx);
        tmp[tableIdx] = 1;
        PredicateRowNumRange::RowNumWithParameter rowNum(false, tmp);
        val.emplace_back(std::in_place_type<PredicateRowNumRange>, rowNum,
                         rowNum);
      }
      if (numOfPredicates >= 4) {
        for (int thres = 0; thres <= numOfCols; ++thres) {
          auto curVal = val;
          // [0,thres): INT, [thres, n): STR
          for (int i = 0; i < thres; ++i) {
            curVal.emplace_back(std::in_place_type<PredicateColType>, i,
                                PredicateColType::NUMERICType());
          }
          for (int i = thres; i < numOfCols; ++i) {
            curVal.emplace_back(std::in_place_type<PredicateColType>, i,
                                PredicateColType::TEXTType());
          }
          result.emplace_back(std::in_place_type<ValueType::ConjunctionType>,
                              curVal);
        }
      } else {
        result.emplace_back(std::in_place_type<ValueType::ConjunctionType>,
                            val);
      }
    }
    return result;
  }

  static std::vector<ValueType> varTableHandlerGivenBenchmark(
      int tableIdx, const SQLBenchmark& benchmark, int numOfPredicates) {
    if (benchmark.getIOPairs()[0].getInput().size() <= tableIdx) {
      return {};
    }
    if (numOfPredicates == 0) {
      return {LatticeType::top()};
    }
    auto colNumberMaxLimit =
        constants::Constants::sqlSetting.maxNumberOfColumnsInInput;
    std::vector<ValueType> result;
    std::vector<ValueType::ConjunctionType::VariantType> val;
    auto currentTable = benchmark.getIOPairs()[0].getInput()[tableIdx];
    auto numOfCols = currentTable.getNumColumns();
    if (numOfPredicates >= 2) {
      val.emplace_back(std::in_place_type<PredicateNumOfColumns>, numOfCols);
    }
    if (numOfPredicates >= 3) {
      for (int i = 0; i < numOfCols; ++i) {
        val.emplace_back(std::in_place_type<PredicateSubsetOfInput>, tableIdx,
                         i);
      }
    }
    if (numOfPredicates >= 1) {
      std::vector<int> tmp(
          constants::Constants::slicingOracleSetting.maxTableIdx);
      tmp[tableIdx] = 1;
      PredicateRowNumRange::RowNumWithParameter rowNum(false, tmp);
      val.emplace_back(std::in_place_type<PredicateRowNumRange>, rowNum,
                       rowNum);
    }
    if (numOfPredicates >= 4) {
      auto colTypes = currentTable.getColumnDataTypes();
      int thres = 0;
      for (auto colType : colTypes) {
        if (colType.getType() == 0)
          thres++;
      }
      // [0,thres): INT, [thres, n): STR
      for (int i = 0; i < thres; ++i) {
        val.emplace_back(std::in_place_type<PredicateColType>, i,
                         PredicateColType::NUMERICType());
      }
      for (int i = thres; i < numOfCols; ++i) {
        val.emplace_back(std::in_place_type<PredicateColType>, i,
                         PredicateColType::TEXTType());
      }
    }
    result.emplace_back(std::in_place_type<ValueType::ConjunctionType>, val);
    return result;
  }

  // static ValueType handleConjunction(const OperatorType& op,
  //                                    const std::vector<ValueType>& argValues) {
  //   //f(v1 /\ v2, v3 /\ v4) = f(v1 /\ v3) /\ f(v1 /\ v4) /\ f(v2 /\ v3) /\ f(v2 /\ v4)
  //   std::vector<std::vector<ValueType>> valueArgsCombinations;
  //   for (const auto& arg : argValues) {
  //     auto& argV = arg.getValue();
  //     if (std::holds_alternative<ValueType::ConjunctionType>(argV)) {
  //       auto& conjunction = std::get<ValueType::ConjunctionType>(argV);
  //       std::vector<ValueType> valueArgs;
  //       for (const auto& predicate : conjunction.getConjunction()) {
  //         valueArgs.emplace_back(common::variant_cast(predicate));
  //       }
  //       valueArgsCombinations.emplace_back(valueArgs);
  //     } else {
  //       valueArgsCombinations.emplace_back(std::vector<ValueType>{arg});
  //     }
  //   }
  //   auto argCartisian = common::cartesianProduct(valueArgsCombinations);
  //   auto result = LatticeType::top();
  //   for (const auto& args : argCartisian) {
  //     auto transformed = atomicHandler(op, args);
  //     for (const auto& value : transformed) {
  //       if (std::holds_alternative<Bottom>(value.getValue())) {
  //         return LatticeType::bottom();
  //       }
  //       result = LatticeType::meet(result, value);
  //     }
  //   }
  //   return result;
  // }

  static ValueType handleBottom(
      [[maybe_unused]] const std::vector<ValueType>& argValues) {
    return LatticeType::bottom();
  }

  // NtOrderByList := OpOrderByDesc(NtColumnIndexInterval) | OpOrderByAsc(NtColumnIndexInterval)
  static ValueType orderByListSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    return argValues[0];
  }

  // NtQuery := OpProjection(NtQuery, NtProjectionList)
  // NtQuery := OpProjectionDistinct(NtQuery, NtGroupByList)
  // NtQuery := OpGroupByOrderByCountLimit(NtQuery, NtOrderByList)
  static ValueType projectSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    assert(argValues.size() == 2);
    std::vector<ValueType> intervalValues;
    if (std::holds_alternative<PredicateSelectIntervalColumns>(
            argValues[1].getValue())) {
      intervalValues.push_back(argValues[1]);
    } else {
      assert(std::holds_alternative<PredicateOrderedTuple>(
          argValues[1].getValue()));
      const auto& values =
          std::get<PredicateOrderedTuple>(argValues[1].getValue()).getValues();
      for (auto value : values) {
        intervalValues.emplace_back(common::variant_cast(value));
      }
    }
    if (!isValidIntervals(getValidThresFromTable(argValues[0]),
                          intervalValues)) {
      return LatticeType::bottom();
    }
    // printf("valid\n");
    // fflush(stdout);
    if (!containNumOfColumns(argValues[0].getValue())) {
      return LatticeType::top();
    }
    // a special case for last projection (where op == OpProjection, and argValues[2] is an UnAbstractedTerm)
    //this is what we added when we need to do top down propogation for the last projection
    if (std::holds_alternative<OpProjection>(op.getSymbol()) &&
        std::holds_alternative<UnAbstractedTerm>(argValues[1].getValue())) {
      std::vector<int> selectedCols;
      auto unAbstractedTerm =
          std::get<UnAbstractedTerm>(argValues[1].getValue());
      for (const auto& child : unAbstractedTerm.getTerm().getChildren()) {
        selectedCols.push_back(
            std::get<ConstColumnIndex>(child.getSymbol().getSymbol())
                .getIndex());
      }
      auto hashValue = getColumnHashValue(argValues[0]);
      int numOfCols = (int)hashValue.size();
      auto result = ValueType{std::in_place_type<PredicateNumOfColumns>,
                              selectedCols.size()};
      for (int i = 0; i < selectedCols.size(); i++) {
        auto& selectColIdx = selectedCols[i];
        result = LatticeType::meet(
            result,
            ValueType{std::in_place_type<PredicateColType>, i,
                      getColTypeFromHashValue(hashValue, selectColIdx)});
        if (getInputTableIdxFromHashValue(hashValue, selectColIdx) != -1) {
          result = LatticeType::meet(
              result,
              ValueType{std::in_place_type<PredicateSubsetOfInput>,
                        getInputTableIdxFromHashValue(hashValue, selectColIdx),
                        i});
        }
      }
      return result;
    } else {
      // this is the default case
      std::vector<PredicateOrderedTuple::VariantType> selectCols;
      if (std::holds_alternative<PredicateOrderedTuple>(
              argValues[1].getValue())) {
        selectCols = std::get<PredicateOrderedTuple>(argValues[1].getValue())
                         .getValues();
      } else {
        selectCols.emplace_back(common::variant_cast(argValues[1].getValue()));
      }

      auto hashValue = getColumnHashValue(argValues[0]);
      int numOfCols = (int)hashValue.size();
      assert(numOfCols);
      auto result = ValueType{std::in_place_type<PredicateNumOfColumns>,
                              selectCols.size()};
      for (int i = 0; i < selectCols.size(); i++) {
        auto& selectCol = selectCols[i];
        assert(
            std::holds_alternative<PredicateSelectIntervalColumns>(selectCol));
        auto curColIdxL =
            std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalL();
        auto curColIdxR =
            std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalR();
        auto aggType =
            std::get<PredicateSelectIntervalColumns>(selectCol).getAggType();
        if (numOfCols != -1 && curColIdxR >= numOfCols) {
          return LatticeType::bottom();
        }
        assert(aggType == -1);
        if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
            type != -1) {
          result = LatticeType::meet(
              result, ValueType{std::in_place_type<PredicateColType>, i, type});
        }
        if (int idx = getInputTableIdxFromHashValue(hashValue, curColIdxL);
            idx != -1) {
          result = LatticeType::meet(
              result,
              ValueType{std::in_place_type<PredicateSubsetOfInput>, idx, i});
        }
      }
      return result;
    }
  }

  // NtQuery := OpSelect(NtQuery, NtSelectPredicate)
  // NtQuery := OpOrderByColLimit(NtQuery, NtOrderByList)
  static ValueType selectSoundHandler(const OperatorType& op,
                                      const std::vector<ValueType>& argValues) {
    if (!containNumOfColumns(argValues[0].getValue())) {
      return LatticeType::top();
    }
    // two cases: NtSelectPredicate can either return a PredicateSelectIntervalColumns or a PredicateOrderedTuples
    if (std::holds_alternative<PredicateSelectIntervalColumns>(
            argValues[1].getValue())) {
      const std::vector<ValueType>& intervalValues = {argValues.begin() + 1,
                                                      argValues.end()};
      if (!isValidIntervals(getValidThresFromTable(argValues[0]),
                            intervalValues)) {
        return LatticeType::bottom();
      }

      auto curColIdxL =
          std::get<PredicateSelectIntervalColumns>(argValues[1].getValue())
              .getIntervalL();
      auto curColIdxR =
          std::get<PredicateSelectIntervalColumns>(argValues[1].getValue())
              .getIntervalR();
      auto aggType =
          std::get<PredicateSelectIntervalColumns>(argValues[1].getValue())
              .getAggType();

      auto hashValue = getColumnHashValue(argValues[0]);
      int numOfCols = (int)hashValue.size();

      if (numOfCols != -1 && curColIdxR >= numOfCols) {
        return LatticeType::bottom();
      }
      if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
          aggType != -1 && type != -1 && aggType != type) {
        return LatticeType::bottom();
      }
    } else if (std::holds_alternative<ValueType::ConjunctionType>(
                   argValues[1].getValue())) {
      const auto& predicates =
          std::get<ValueType::ConjunctionType>(argValues[1].getValue())
              .getConjunction();
      auto hashValue = getColumnHashValue(argValues[0]);
      int numOfCols = (int)hashValue.size();

      const auto& thres = getValidThresFromTable(argValues[0]);
      std::unordered_map<int, int> thres_to_idx;
      int tot = 0;
      for (auto x : thres)
        thres_to_idx[x] = tot++;

      for (const auto& predicate : predicates) {
        assert(
            std::holds_alternative<PredicateSelectIntervalColumns>(predicate));
        auto curColIdxL =
            std::get<PredicateSelectIntervalColumns>(predicate).getIntervalL();
        auto curColIdxR =
            std::get<PredicateSelectIntervalColumns>(predicate).getIntervalR();
        auto aggType =
            std::get<PredicateSelectIntervalColumns>(predicate).getAggType();
        if (thres_to_idx[curColIdxL] != thres_to_idx[curColIdxR + 1] - 1)
          return LatticeType::bottom();
        if (numOfCols != -1 && curColIdxR >= numOfCols) {
          return LatticeType::bottom();
        }
        if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
            aggType != -1 && type != -1 && aggType != type) {
          return LatticeType::bottom();
        }
      }
    } else {
      throw common::InvalidArgument("Unexpected value type. Value type: " +
                                    argValues[1].toString());
    }
    return removeRowNumRange(argValues[0]);
  }

  // NtQuery := OpGroupWithoutKey(NtQuery, NtAggregationList)
  static ValueType groupNcSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    if (!containNumOfColumns(argValues[0].getValue())) {
      return LatticeType::top();
    }
    assert(
        std::holds_alternative<PredicateOrderedTuple>(argValues[1].getValue()));
    auto selectCols =
        std::get<PredicateOrderedTuple>(argValues[1].getValue()).getValues();

    std::vector<ValueType> intervals;
    intervals.reserve(selectCols.size());
    for (auto predicate : selectCols) {
      intervals.emplace_back(common::variant_cast(predicate));
    }
    if (!isValidIntervals(getValidThresFromTable(argValues[0]), intervals)) {
      return LatticeType::bottom();
    }

    auto hashValue = getColumnHashValue(argValues[0]);
    int numOfCols = (int)hashValue.size();

    auto result =
        ValueType{std::in_place_type<PredicateNumOfColumns>, selectCols.size()};
    for (int i = 0; i < selectCols.size(); i++) {
      auto& selectCol = selectCols[i];
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(selectCol));
      auto curColIdxL =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalL();
      auto curColIdxR =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalR();
      auto aggType =
          std::get<PredicateSelectIntervalColumns>(selectCol).getAggType();
      if (numOfCols != -1 && curColIdxR >= numOfCols) {
        return LatticeType::bottom();
      }
      if (int idx = getInputTableIdxFromHashValue(hashValue, curColIdxL);
          aggType == PredicateSelectIntervalColumns::MINMAXType() &&
          idx != -1) {
        result = LatticeType::meet(
            result,
            ValueType{std::in_place_type<PredicateSubsetOfInput>, idx, i});
      }
      if (aggType != -1) {
        if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
            type != -1) {
          if (aggType == PredicateSelectIntervalColumns::MINMAXType()) {
            result = LatticeType::meet(
                result,
                ValueType{std::in_place_type<PredicateColType>, i, type});
          }
          if (aggType == PredicateSelectIntervalColumns::COUNTType()) {
            result = LatticeType::meet(
                result, ValueType{std::in_place_type<PredicateColType>, i,
                                  PredicateColType::NUMERICType()});
          }
          if (aggType == PredicateSelectIntervalColumns::SUMAVGType()) {
            if (type != PredicateColType::NUMERICType()) {
              return LatticeType::bottom();
            }
            result = LatticeType::meet(
                result, ValueType{std::in_place_type<PredicateColType>, i,
                                  PredicateColType::NUMERICType()});
          }
        }
      }
    }
    return result;
  };

  // NtQuery := OpGroupWithKey(NtQuery, NtOrderedGroupByList, NtAggregationList)
  static ValueType groupCSoundHandler(const OperatorType& op,
                                      const std::vector<ValueType>& argValues) {
    if (!containNumOfColumns(argValues[0].getValue())) {
      return LatticeType::top();
    }
    assert(
        std::holds_alternative<PredicateOrderedTuple>(argValues[1].getValue()));
    assert(
        std::holds_alternative<PredicateOrderedTuple>(argValues[2].getValue()));
    auto groupByCols =
        std::get<PredicateOrderedTuple>(argValues[1].getValue()).getValues();
    auto aggListCols =
        std::get<PredicateOrderedTuple>(argValues[2].getValue()).getValues();

    std::vector<ValueType> intervals;
    intervals.reserve(groupByCols.size());
    for (auto predicate : groupByCols) {
      intervals.emplace_back(common::variant_cast(predicate));
    }
    if (!isValidIntervals(getValidThresFromTable(argValues[0]), intervals)) {
      return LatticeType::bottom();
    }

    intervals.clear();
    intervals.reserve(aggListCols.size());
    for (auto predicate : aggListCols) {
      intervals.emplace_back(common::variant_cast(predicate));
    }
    if (!isValidIntervals(getValidThresFromTable(argValues[0]), intervals)) {
      return LatticeType::bottom();
    }

    auto hashValue = getColumnHashValue(argValues[0]);
    int numOfCols = (int)hashValue.size();

    auto result = ValueType{std::in_place_type<PredicateNumOfColumns>,
                            groupByCols.size() + aggListCols.size()};
    for (int i = 0; i < groupByCols.size(); i++) {
      auto& selectCol = groupByCols[i];
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(selectCol));
      auto curColIdxL =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalL();
      auto curColIdxR =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalR();
      auto aggType =
          std::get<PredicateSelectIntervalColumns>(selectCol).getAggType();
      if (numOfCols != -1 && curColIdxR >= numOfCols) {
        return LatticeType::bottom();
      }
      if (int idx = getInputTableIdxFromHashValue(hashValue, curColIdxL);
          idx != -1) {
        result = LatticeType::meet(
            result,
            ValueType{std::in_place_type<PredicateSubsetOfInput>, idx, i});
      }
      assert(aggType == -1);
      if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
          type != -1) {
        result = LatticeType::meet(
            result, ValueType{std::in_place_type<PredicateColType>, i, type});
      }
    }
    for (int i = 0; i < aggListCols.size(); i++) {
      auto selectCol = aggListCols[i];
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(selectCol));
      auto curColIdxL =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalL();
      auto curColIdxR =
          std::get<PredicateSelectIntervalColumns>(selectCol).getIntervalR();
      auto aggType =
          std::get<PredicateSelectIntervalColumns>(selectCol).getAggType();
      if (numOfCols != -1 && curColIdxR >= numOfCols) {
        return LatticeType::bottom();
      }
      if (int idx = getInputTableIdxFromHashValue(hashValue, curColIdxL);
          aggType == PredicateSelectIntervalColumns::MINMAXType() &&
          idx != -1) {
        result = LatticeType::meet(
            result, ValueType{std::in_place_type<PredicateSubsetOfInput>, idx,
                              i + groupByCols.size()});
      }
      if (aggType != -1) {
        if (int type = getColTypeFromHashValue(hashValue, curColIdxL);
            type != -1) {
          if (aggType == PredicateSelectIntervalColumns::MINMAXType()) {
            result = LatticeType::meet(
                result, ValueType{std::in_place_type<PredicateColType>,
                                  i + groupByCols.size(), type});
          }
          if (aggType == PredicateSelectIntervalColumns::COUNTType()) {
            result = LatticeType::meet(
                result, ValueType{std::in_place_type<PredicateColType>,
                                  i + groupByCols.size(),
                                  PredicateColType::NUMERICType()});
          }
          if (aggType == PredicateSelectIntervalColumns::SUMAVGType()) {
            if (type != PredicateColType::NUMERICType()) {
              return LatticeType::bottom();
            }
            result = LatticeType::meet(
                result, ValueType{std::in_place_type<PredicateColType>,
                                  i + groupByCols.size(),
                                  PredicateColType::NUMERICType()});
          }
        }
      }
    }
    return result;
  }

  static ValueType windowIdSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    if (!std::holds_alternative<PredicateNumOfColumns>(
            argValues[0].getValue())) {
      return argValues[0];
    }
    return LatticeType::top();
  }

  static ValueType windowNumColSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    return LatticeType::top();
  }

  // NtQuery := OpLeftJoin(NtQuery, NtQuery, NtJoinPredicate)
  // NtQuery := OpInnerJoin(NtQuery, NtQuery, NtJoinPredicate)
  static ValueType joinSoundHandler(const OperatorType& op,
                                    const std::vector<ValueType>& argValues) {
    if (!containNumOfColumns(argValues[0].getValue()) ||
        !containNumOfColumns(argValues[1].getValue())) {
      return LatticeType::top();
    }

    auto hashValue1 = getColumnHashValue(argValues[0]);
    int numOfCols1 = (int)hashValue1.size();
    const auto& thres1 = getValidThresFromTable(argValues[0]);

    auto hashValue2 = getColumnHashValue(argValues[1]);
    int numOfCols2 = (int)hashValue2.size();
    const auto& thres2 = getValidThresFromTable(argValues[1]);

    //check if join condition is valid
    assert(
        std::holds_alternative<PredicateOrderedTuple>(argValues[2].getValue()));

    auto joinCondition =
        std::get<PredicateOrderedTuple>(argValues[2].getValue()).getValues();

    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        joinCondition[0]));
    auto curColIdx1L =
        std::get<PredicateSelectIntervalColumns>(joinCondition[0])
            .getIntervalL();
    auto curColIdx1R =
        std::get<PredicateSelectIntervalColumns>(joinCondition[0])
            .getIntervalR();
    if (numOfCols1 != -1 && curColIdx1R >= numOfCols1) {
      return LatticeType::bottom();
    }
    bool flag = false;
    for (int i = 0; i < (int)thres1.size() - 1; ++i) {
      if (curColIdx1L == thres1[i] && curColIdx1R + 1 == thres1[i + 1]) {
        flag = true;
        break;
      }
    }
    if (!flag) {
      return LatticeType::bottom();
    }

    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        joinCondition[1]));
    auto curColIdx2L =
        std::get<PredicateSelectIntervalColumns>(joinCondition[1])
            .getIntervalL();
    auto curColIdx2R =
        std::get<PredicateSelectIntervalColumns>(joinCondition[1])
            .getIntervalR();
    if (numOfCols2 != -1 && curColIdx2R >= numOfCols2) {
      return LatticeType::bottom();
    }
    flag = false;
    for (int i = 0; i < (int)thres2.size() - 1; ++i) {
      if (curColIdx2L == thres2[i] && curColIdx2R + 1 == thres2[i + 1]) {
        flag = true;
        break;
      }
    }
    if (!flag) {
      return LatticeType::bottom();
    }

    if (int type1 = getColTypeFromHashValue(hashValue1, curColIdx1L),
        type2 = getColTypeFromHashValue(hashValue2, curColIdx2L);
        type1 != -1 && type2 != -1 && type1 != type2) {
      return LatticeType::bottom();
    }

    auto numOfColumns = numOfCols1 + numOfCols2;
    //guard: if the number of columns is greater than MaxNumberOfColumnsInIntermediateResult, then return bottom
    if (numOfColumns >
        constants::Constants::sqlSetting.maxNumberOfColumnsIntermediate) {
      return LatticeType::bottom();
    }

    auto result =
        ValueType{std::in_place_type<PredicateNumOfColumns>, numOfColumns};
    for (int i = 0; i < numOfCols1; ++i) {
      if (int type = getColTypeFromHashValue(hashValue1, i); type != -1) {
        result = LatticeType::meet(
            result, ValueType{std::in_place_type<PredicateColType>, i, type});
      }
    }
    for (int i = 0; i < numOfCols2; ++i) {
      if (int type = getColTypeFromHashValue(hashValue2, i); type != -1) {
        result = LatticeType::meet(
            result, ValueType{std::in_place_type<PredicateColType>,
                              i + numOfCols1, type});
      }
    }

    return result;
  }

  static ValueType aggSoundHandler(const OperatorType& op,
                                   const std::vector<ValueType>& argValues) {
    assert(argValues.size() == 1);
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    auto colIdxL =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalL();
    auto colIdxR =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalR();
    return ValueType{std::in_place_type<PredicateSelectIntervalColumns>,
                     colIdxL, colIdxR};
  }

  // NtAggregation := OpMin(NtColumnIndexInterval)
  // NtAggregation := OpMax(NtColumnIndexInterval)
  static ValueType aggMinMaxSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    assert(argValues.size() == 1);
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    auto colIdxL =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalL();
    auto colIdxR =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalR();
    return ValueType{std::in_place_type<PredicateSelectIntervalColumns>,
                     colIdxL, colIdxR,
                     PredicateSelectIntervalColumns::MINMAXType()};
  }

  // NtAggregation := OpCount(NtColumnIndexInterval)
  // NtAggregation := OpCountDistinct(NtColumnIndexInterval)
  static ValueType aggCountSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    assert(argValues.size() == 1);
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    auto colIdxL =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalL();
    auto colIdxR =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalR();
    return ValueType{std::in_place_type<PredicateSelectIntervalColumns>,
                     colIdxL, colIdxR,
                     PredicateSelectIntervalColumns::COUNTType()};
  }

  // NtAggregation := OpSum(NtColumnIndexInterval)
  // NtAggregation := OpAvg(NtColumnIndexInterval)
  static ValueType aggSumAvgSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues) {
    assert(argValues.size() == 1);
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    auto colIdxL =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalL();
    auto colIdxR =
        std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
            .getIntervalR();
    return ValueType{std::in_place_type<PredicateSelectIntervalColumns>,
                     colIdxL, colIdxR,
                     PredicateSelectIntervalColumns::SUMAVGType()};
  }

  // NtAggregationList := OpAggList(NtAggregation)
  // NtAggregationList := OpAggList(NtAggregation, NtAggregation)
  static ValueType aggListSoundHandler(const OperatorType& op,
                                       const std::vector<ValueType>& argValues,
                                       int numOfPredicates) {
    if (!isValidIntervals(argValues, numOfPredicates))
      return LatticeType::bottom();
    std::vector<PredicateOrderedTuple::VariantType> selectColumns;
    for (const auto& argValue : argValues) {
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(
          argValue.getValue()));
      selectColumns.emplace_back(common::variant_cast(argValue.getValue()));
    }
    return ValueType{std::in_place_type<PredicateOrderedTuple>, selectColumns};
  }

  // NtSelectPredicate := OpFilterOr(NtSelectPredicate, NtSelectPredicate)
  // NtSelectPredicateClause := OpFilterAnd(NtSelectPredicateClause, NtSelectPredicateClause)

  static ValueType filterConjunctionDisjunctionSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues,
      int numOfPredicates) {
    if (!isValidIntervals(argValues, numOfPredicates))
      return LatticeType::bottom();
    assert(argValues.size() == 2);
    ValueType result = LatticeType::top();
    for (const auto& argValue : argValues) {
      result = LatticeType::meet(result, argValue);
    }
    return result;
  }

  // NtSelectPredicateClause := OpFilterEq(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterNeq(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterGt(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterLt(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterLike(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterGeq(NtColumnIndexInterval, NtFilterConstantIndex)
  // NtSelectPredicateClause := OpFilterLeq(NtColumnIndexInterval, NtFilterConstantIndex)
  static ValueType filterSoundHandler(const OperatorType& op,
                                      const std::vector<ValueType>& argValues) {
    assert(std::holds_alternative<PredicateSelectIntervalColumns>(
        argValues[0].getValue()));
    if (std::holds_alternative<PredicateConstantType>(
            argValues[1].getValue())) {
      auto colIdxL =
          std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
              .getIntervalL();
      auto colIdxR =
          std::get<PredicateSelectIntervalColumns>(argValues[0].getValue())
              .getIntervalR();
      auto type =
          std::get<PredicateConstantType>(argValues[1].getValue()).getType();
      return ValueType{std::in_place_type<PredicateSelectIntervalColumns>,
                       colIdxL, colIdxR, type};
    }
    return argValues[0];
  }

  // NtGroupByList := OpGroupByList(NtColumnIndexInterval)
  // NtGroupByList := OpGroupByList(NtColumnIndexInterval, NtColumnIndexInterval)
  // NtProjectionList := OpProjList(NtColumnIndexInterval)
  // NtProjectionList := OpProjList(NtColumnIndexInterval, NtColumnIndexInterval)
  // NtProjectionList := OpProjList(NtColumnIndexInterval, NtColumnIndexInterval, NtColumnIndexInterval)
  static ValueType projListSoundHandler(const OperatorType& op,
                                        const std::vector<ValueType>& argValues,
                                        int numOfPredicates) {
    if (!isValidIntervals(argValues, numOfPredicates))
      return LatticeType::bottom();
    std::vector<PredicateOrderedTuple::VariantType> selectColumns;
    for (const auto& argValue : argValues) {
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(
          argValue.getValue()));
      selectColumns.emplace_back(common::variant_cast(argValue.getValue()));
    }
    return ValueType{std::in_place_type<PredicateOrderedTuple>, selectColumns};
  }

  // NtJoinPredicateClause := OpJoinEq(NtColumnIndexInterval, NtColumnIndexInterval)
  static ValueType joinEqSoundHandler(const OperatorType& op,
                                      const std::vector<ValueType>& argValues) {
    std::vector<PredicateOrderedTuple::VariantType> predicates;
    for (const auto& argValue : argValues) {
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(
          argValue.getValue()));
      predicates.emplace_back(common::variant_cast(argValue.getValue()));
    }
    return ValueType{std::in_place_type<PredicateOrderedTuple>, predicates};
  }

  // NtColumnIndexInterval := OpIndexInInterval(NtColumnIndex)
  static std::vector<ValueType> indexInIntervalSoundHandler(
      const OperatorType& op, const std::vector<ValueType>& argValues,
      int numOfPredicates) {
    assert(std::holds_alternative<PredicateSelectNthColumn>(
        argValues[0].getValue()));
    auto colIdx = std::get<PredicateSelectNthColumn>(argValues[0].getValue())
                      .getColumnIdx();
    auto colNumberMaxLimit =
        constants::Constants::sqlSetting.maxNumberOfColumnsInInput;
    std::vector<ValueType> results;
    if (numOfPredicates == 0 or numOfPredicates == 1) {
      if (colIdx >= colNumberMaxLimit) {
        return {};
      }
      results.emplace_back(std::in_place_type<PredicateSelectIntervalColumns>,
                           0, colNumberMaxLimit - 1);
    } else if (numOfPredicates == 2) {
      for (int r = colIdx; r < colNumberMaxLimit; ++r) {
        results.emplace_back(std::in_place_type<PredicateSelectIntervalColumns>,
                             0, r);
      }
    } else {
      for (int l = 0; l <= colIdx; ++l) {
        for (int r = colIdx; r < colNumberMaxLimit; ++r) {
          results.emplace_back(
              std::in_place_type<PredicateSelectIntervalColumns>, l, r);
        }
      }
    }
    return results;
  }

  static ValueType rowNumEqualHandler(const ValueType& argValue) {
    return getRowNumRange(argValue);
  }

  static ValueType rowNumLEHandler(const ValueType& argValue) {
    auto oldRange = getRowNumRange(argValue);
    if (std::holds_alternative<Top>(oldRange.getValue())) {
      return LatticeType::top();
    }
    assert(std::holds_alternative<PredicateRowNumRange>(oldRange.getValue()));
    auto value = std::get<PredicateRowNumRange>(oldRange.getValue());
    return ValueType{std::in_place_type<PredicateRowNumRange>, zeroBound(),
                     value.getUpperBound()};
  }

  static ValueType rowNumLEPositiveHandler(const ValueType& argValue) {
    auto oldRange = getRowNumRange(argValue);
    if (std::holds_alternative<Top>(oldRange.getValue())) {
      return LatticeType::top();
    }
    assert(std::holds_alternative<PredicateRowNumRange>(oldRange.getValue()));
    auto value = std::get<PredicateRowNumRange>(oldRange.getValue());
    if (value.getLowerBound().iszero())
      return oldRange;
    return ValueType{std::in_place_type<PredicateRowNumRange>, oneBound(),
                     value.getUpperBound()};
  }

  static ValueType rowNumOneHandler(const ValueType& argValue) {
    auto oldRange = getRowNumRange(argValue);
    if (std::holds_alternative<Top>(oldRange.getValue())) {
      return LatticeType::top();
    }
    PredicateRowNumRange::RowNumWithParameter bound(
        false,
        std::vector(constants::Constants::slicingOracleSetting.maxTableIdx, 0));
    return ValueType{std::in_place_type<PredicateRowNumRange>, bound, bound};
  }

  static ValueType rowNumLeftJoinHandler(const ValueType& argValue1,
                                         const ValueType& argValue2) {
    auto oldRange1 = getRowNumRange(argValue1);
    auto oldRange2 = getRowNumRange(argValue2);
    if (std::holds_alternative<Top>(oldRange1.getValue()) ||
        std::holds_alternative<Top>(oldRange2.getValue())) {
      return LatticeType::top();
    }
    auto value1 = std::get<PredicateRowNumRange>(oldRange1.getValue());
    auto value2 = std::get<PredicateRowNumRange>(oldRange2.getValue());
    return ValueType(std::in_place_type<PredicateRowNumRange>, zeroBound(),
                     value1.getUpperBound() * value2.getUpperBound());
  }

  static ValueType rowNumInnerJoinHandler(const ValueType& argValue1,
                                          const ValueType& argValue2) {

    auto oldRange1 = getRowNumRange(argValue1);
    auto oldRange2 = getRowNumRange(argValue2);
    if (std::holds_alternative<Top>(oldRange1.getValue()) ||
        std::holds_alternative<Top>(oldRange2.getValue())) {
      return LatticeType::top();
    }
    auto value1 = std::get<PredicateRowNumRange>(oldRange1.getValue());
    auto value2 = std::get<PredicateRowNumRange>(oldRange2.getValue());
    return ValueType(std::in_place_type<PredicateRowNumRange>, zeroBound(),
                     value1.getUpperBound() * value2.getUpperBound());
  }

  static PredicateRowNumRange::RowNumWithParameter zeroBound() { return {}; }

  static PredicateRowNumRange::RowNumWithParameter oneBound() {
    return {
        false,
        std::vector(constants::Constants::slicingOracleSetting.maxTableIdx, 0)};
  }

  static std::pair<ValueType, std::vector<int>> getUnorderedValue(
      const ValueType& unorderedValue) {
    auto hashValue = getColumnHashValue(unorderedValue);
    std::vector<int> order;
    order.resize(hashValue.size());
    for (int i = 0; i < hashValue.size(); ++i) {
      order[i] = i;
    }
    return std::make_pair(unorderedValue, order);
  }

  static ValueType getRowNumRange(const ValueType& predicate) {
    if (std::holds_alternative<ValueType::ConjunctionType>(
            predicate.getValue())) {
      auto conjunction =
          std::get<ValueType::ConjunctionType>(predicate.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateRowNumRange>(predicate)) {
          return ValueType{std::get<PredicateRowNumRange>(predicate)};
        }
      }
    }
    if (std::holds_alternative<PredicateRowNumRange>(predicate.getValue())) {
      return ValueType{std::get<PredicateRowNumRange>(predicate.getValue())};
    }
    return LatticeType::top();
  }

  static std::vector<int> getColumnHashValue(const ValueType& predicate) {
    std::vector<ValueType> valueArgs;
    int numOfCols = -1;
    if (std::holds_alternative<ValueType::ConjunctionType>(
            predicate.getValue())) {
      auto conjunction =
          std::get<ValueType::ConjunctionType>(predicate.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        valueArgs.emplace_back(common::variant_cast(predicate));
        if (std::holds_alternative<PredicateNumOfColumns>(predicate)) {
          numOfCols =
              std::get<PredicateNumOfColumns>(predicate).getNumOfColumn();
        }
      }
    } else if (std::holds_alternative<PredicateNumOfColumns>(
                   predicate.getValue())) {
      valueArgs.push_back(predicate);
      numOfCols = std::get<PredicateNumOfColumns>(predicate.getValue())
                      .getNumOfColumn();
    } else {
      return {};
    }
    if (numOfCols == -1) {
      //if it's a conjunction but no numOfColumns, then we still return empty hashValue
      return {};
    }

    std::vector<int> hashValue;
    hashValue.resize(numOfCols);
    for (const auto& predicate : valueArgs) {
      if (std::holds_alternative<PredicateSubsetOfInput>(
              predicate.getValue())) {
        auto value = std::get<PredicateSubsetOfInput>(predicate.getValue());
        auto inputTableIdx = value.getInputTableIdx();
        auto idxInCur = value.getIdxInCur();
        hashValue[idxInCur] += inputTableIdx + 1;
      }
      if (std::holds_alternative<PredicateColType>(predicate.getValue())) {
        auto value = std::get<PredicateColType>(predicate.getValue());
        auto colIdx = value.getColIdx();
        auto colType = value.getColType();
        hashValue[colIdx] += (colType + 1) * 100;
      }
    }
    return hashValue;
  }

  static int getInputTableIdxFromHashValue(const std::vector<int>& hashValue,
                                           int i) {
    return hashValue[i] % 100 - 1;
  }

  static int getColTypeFromHashValue(const std::vector<int>& hashValue, int i) {
    return hashValue[i] / 100 - 1;
  }

  static ValueType removeRowNumRange(const ValueType& value) {
    using VariantType = ValueType::ConjunctionType::VariantType;
    std::vector<VariantType> result;
    if (std::holds_alternative<ValueType::ConjunctionType>(value.getValue())) {
      auto conjunction = std::get<ValueType::ConjunctionType>(value.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (!std::holds_alternative<PredicateRowNumRange>(predicate)) {
          result.push_back(predicate);
        }
      }
    } else if (!std::holds_alternative<PredicateRowNumRange>(
                   value.getValue())) {
      return value;
    }
    return ValueType{std::in_place_type<ValueType::ConjunctionType>, result};
  }

  static bool isValidIntervals(const std::vector<int>& thres,
                               const std::vector<ValueType>& argValues) {
    std::unordered_map<int, int> thres_to_idx;
    int tot = 0;
    for (auto x : thres)
      thres_to_idx[x] = tot++;
    for (const auto& argValue : argValues) {
      if (std::holds_alternative<PredicateSelectIntervalColumns>(
              argValue.getValue())) {
        auto p = std::get<PredicateSelectIntervalColumns>(argValue.getValue());
        if (thres_to_idx[p.getIntervalL()] !=
            thres_to_idx[p.getIntervalR() + 1] - 1)
          return false;
      } else if (std::holds_alternative<ValueType::ConjunctionType>(
                     argValue.getValue())) {
        auto c = std::get<ValueType::ConjunctionType>(argValue.getValue())
                     .getConjunction();
        for (const auto& v : c) {
          assert(std::holds_alternative<PredicateSelectIntervalColumns>(v));
          auto p = std::get<PredicateSelectIntervalColumns>(v);
          if (thres_to_idx[p.getIntervalL()] !=
              thres_to_idx[p.getIntervalR() + 1] - 1)
            return false;
        }
      } else
        assert(0);
    }
    return true;
  }

  static bool isValidIntervals(const std::vector<ValueType>& argValues,
                               int numOfPredicates) {
    std::set<int> thres;
    thres.insert(0);
    int maxThres = (numOfPredicates >= 4 ? 5 : (numOfPredicates == 3 ? 3 : 2));
    for (const auto& argValue : argValues) {
      if (std::holds_alternative<PredicateSelectIntervalColumns>(
              argValue.getValue())) {
        auto p = std::get<PredicateSelectIntervalColumns>(argValue.getValue());
        thres.insert(p.getIntervalL());
        thres.insert(p.getIntervalR() + 1);
        if (thres.size() > maxThres)
          return false;
      } else if (std::holds_alternative<ValueType::ConjunctionType>(
                     argValue.getValue())) {
        auto c = std::get<ValueType::ConjunctionType>(argValue.getValue())
                     .getConjunction();
        for (const auto& v : c) {
          assert(std::holds_alternative<PredicateSelectIntervalColumns>(v));
          auto p = std::get<PredicateSelectIntervalColumns>(v);
          thres.insert(p.getIntervalL());
          thres.insert(p.getIntervalR() + 1);
          if (thres.size() > maxThres)
            return false;
        }
      } else
        assert(0);
    }
    return isValidIntervals({thres.begin(), thres.end()}, argValues);
  }
};

}  // namespace foresighter::implementation::sql::semantics::transformers