#pragma once
#include <foresighter/implementation/basics/term.hpp>
#include <foresighter/implementation/basics/transformer.hpp>
#include <foresighter/implementation/sql/abstract_value/bottom.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_num_of_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_ordered_tuple.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_select_nth_column.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_subset_of_input.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_total_n_columns.hpp>
#include <foresighter/implementation/sql/abstract_value/predicate_used_nth_column.hpp>

#include <foresighter/common/exceptions.hpp>
#include <foresighter/implementation/sql/symbol.hpp>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/common/variant.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_select_interval_columns.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_superset_of_output.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"
#include "foresighter/implementation/sql/semantics/values/online_inverse_semantics_value.hpp"
#include "foresighter/implementation/sql/types.hpp"

namespace foresighter::implementation::sql::semantics::transformers {
using namespace foresighter::implementation::basics;

class OnlineInverseTransformer {
 public:
  using SymbolType = foresighter::implementation::sql::Symbol;
  using ArgValueType = values::OfflineConstructionValue;
  using ValueType = values::OnlineInverseSemanticsValue;
  using OperatorType = SymbolType;
  using LatticeType = Lattice<ValueType>;

  std::vector<std::vector<ValueType>> inverseTransform(
      const ValueType&
          strengthenedTopDownValue,   // this is the value of the output
      const ArgValueType& headValue,  // this is the value of the head
      const std::vector<ArgValueType>&
          argValues,           // this is the values of the arguments
      const OperatorType& op,  // this is the operator that we are transforming
      //for those arguments that we don't know, we use std::optional to represent them.
      const std::vector<int>& reorderMapping) const {

    //case by case for each operator
    return std::visit(
        common::overloaded{
            [&](const OpProjectionDistinct&) {
              return handleProjectionDistinct(op, strengthenedTopDownValue,
                                              headValue, argValues,
                                              reorderMapping);
            },
            [&](const OpGroupByOrderByCountLimit&) {
              return handleGroupByOrderByCountLimit(
                  op, strengthenedTopDownValue, headValue, argValues,
                  reorderMapping);
            },
            [&](const OpSelect&) {
              return handleSelect(op, strengthenedTopDownValue, headValue,
                                  argValues, reorderMapping);
            },
            [&](const OpOrderByColLimit&) {
              return handleOrderByColLimit(op, strengthenedTopDownValue,
                                           headValue, argValues,
                                           reorderMapping);
            },
            [&](const OpGroupWithoutKey&) {
              return handleGroupWithoutKey(op, strengthenedTopDownValue,
                                           headValue, argValues,
                                           reorderMapping);
            },
            [&](const OpGroupWithKey&) {
              return handleGroupWithKey(op, strengthenedTopDownValue, headValue,
                                        argValues, reorderMapping);
            },
            [&](const OpInnerJoin&) {
              return handleInnerJoin(op, strengthenedTopDownValue, headValue,
                                     argValues, reorderMapping);
            },
            [&](const OpLeftJoin&) {
              return handleLeftJoin(op, strengthenedTopDownValue, headValue,
                                    argValues, reorderMapping);
            },
            [&](const auto&) {
              //default case: return topfor all arguments
              return std::vector<std::vector<ValueType>>{
                  std::vector<ValueType>(argValues.size(), LatticeType::top())};
            }},
        op.getSymbol());
  };

 private:
  [[nodiscard]] int getIndexBeforeReorder(
      int idx, const std::vector<int>& reorderMapping) {
    for (int i = 0; i < reorderMapping.size(); i++) {
      if (reorderMapping[i] == idx) {
        return i;
      }
    }
    throw common::InvalidArgument("Index not found in reorderMapping");
  }

  /**
  * @brief Get the List Indices From Ordered Tuple Of Select Interval Columns object
  * 
  * @param op //the operator of the list that will be needed to construct the list.
  * @param listToBeExtracted //the list to be extracted. It should be an ordered tuple of SelectIntervalColumns.
  * @param idxStart // the start index of the output column indices
  * @param reorderMapping // the reorder mappings of {old indicies -> new indices} for the output columns. When doing the inverse transformation, we need to use this to map the output column indices to the input column indices.
  * @return std::vector<std::pair<std::vector<int>, ValueType>> 
  * it's a vector of pairs, each pair contains a vector of int (representing the selected columns), and a ValueType (the strengthened top down value)
  */

  std::vector<std::pair<std::vector<int>, ValueType>>
  getListIndicesFromOrderedTupleOfSelectIntervalColumns(
      const OperatorType& op, const ArgValueType& listToBeExtracted) const {

    std::vector<PredicateOrderedTuple::VariantType> values;
    if (std::holds_alternative<PredicateOrderedTuple>(
            listToBeExtracted.getValue())) {
      values = std::get<PredicateOrderedTuple>(listToBeExtracted.getValue())
                   .getValues();
    } else {
      values = std::vector<PredicateOrderedTuple::VariantType>{
          foresighter::common::variant_cast(listToBeExtracted.getValue())};
    }
    size_t listLength = values.size();
    std::vector<std::vector<PredicateOrderedTuple::VariantType>> newListItems(
        listLength);
    std::vector<std::vector<int>> selectedIndices(listLength);
    for (size_t i = 0; i < listLength; i++) {
      auto& item = values[i];
      assert(std::holds_alternative<PredicateSelectIntervalColumns>(item));
      auto& selectIntervalColumns =
          std::get<PredicateSelectIntervalColumns>(item);
      auto startIdx = selectIntervalColumns.getIntervalL();
      auto endIdx = selectIntervalColumns.getIntervalR();
      auto aggType = selectIntervalColumns.getAggType();
      newListItems[i].emplace_back(
          std::in_place_type<PredicateSelectIntervalColumns>, startIdx, endIdx,
          aggType);
      //if the aggType is max or min, then we add the column index to the selectedIndices because it can propogate
      //otherwise, we push "-1" (which means there's no matched column in the input)
      if (aggType != PredicateSelectIntervalColumns::COUNTType() &&
          aggType != PredicateSelectIntervalColumns::SUMAVGType()) {
        selectedIndices[i].push_back(1);
      } else {
        selectedIndices[i].push_back(-1);
      }
    }

    //do cartesian product for both, and build the return result
    std::vector<std::pair<std::vector<int>, ValueType>> result;

    common::CartesianProductIterator iterForSelectedIndices(selectedIndices);
    common::CartesianProductIterator iterForNewTopDownValue(newListItems);
    while (iterForSelectedIndices.hasNext() &&
           iterForNewTopDownValue.hasNext()) {
      auto selectedIndices = iterForSelectedIndices.next();
      auto newTopDownValue =
          PredicateOrderedTuple(iterForNewTopDownValue.next());
      result.emplace_back(selectedIndices, newTopDownValue);
    }
    return result;
  };

  [[nodiscard]] bool getSupersetOfOutputBitVec(
      const ValueType& strengthenedTopDownValue) const {

    if (std::holds_alternative<ValueType::ConjunctionType>(
            strengthenedTopDownValue.getValue())) {
      const auto& conjunction = std::get<ValueType::ConjunctionType>(
          strengthenedTopDownValue.getValue());

      for (const auto& item : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateSupersetOfOutput>(item)) {
          return true;
        }
      }
    } else if (std::holds_alternative<PredicateSupersetOfOutput>(
                   strengthenedTopDownValue.getValue())) {
      return true;
    }
    return false;
  }

  [[nodiscard]] bool everyColumnIsFromOneTable(
      const std::vector<int>& listFromIndices) const {
    for (const auto& item : listFromIndices) {
      if (item == -1) {
        return false;
      }
    }
    return true;
  }

  //ProjectDistinct(Q, List)
  std::vector<std::vector<ValueType>> handleProjectionDistinct(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    //first, read the SupersetOfOutputBitVec
    bool superSetOfOutputBitVec =
        getSupersetOfOutputBitVec(strengthenedTopDownValue);

    if (!superSetOfOutputBitVec) {
      return {std::vector<ValueType>(argValues.size(), LatticeType::top())};
    }

    auto selectedIndices =
        getListIndicesFromOrderedTupleOfSelectIntervalColumns(op, argValues[1]);

    std::vector<std::vector<ValueType>> result;
    for (const auto& [selectedIndices, value] : selectedIndices) {
      if (!everyColumnIsFromOneTable(selectedIndices)) {
        result.push_back(
            std::vector<ValueType>{ValueType{ValueType::TopType()}, value});
      } else {
        result.push_back(std::vector<ValueType>{
            ValueType{std::in_place_type<PredicateSupersetOfOutput>}, value});
      }
    }

    return result;
  }

  std::vector<std::vector<ValueType>> handleGroupByOrderByCountLimit(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    return handleProjectionDistinct(op, strengthenedTopDownValue, headValue,
                                    argValues, reorderMapping);
  }

  std::vector<std::vector<ValueType>> handleSelect(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    //first, read the SupersetOfOutputBitVec
    bool superSetOfOutputBitVec =
        getSupersetOfOutputBitVec(strengthenedTopDownValue);
    if (!superSetOfOutputBitVec) {
      return {std::vector<ValueType>(argValues.size(), LatticeType::top())};
    }

    std::vector<std::vector<ValueType>> result = {
        std::vector<ValueType>(argValues.size(), LatticeType::top())};
    if (superSetOfOutputBitVec) {
      result[0][0] = ValueType{std::in_place_type<PredicateSupersetOfOutput>};
    }
    return result;
  }

  std::vector<std::vector<ValueType>> handleOrderByColLimit(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    return handleProjectionDistinct(op, strengthenedTopDownValue, headValue,
                                    argValues, reorderMapping);
  }

  // GroupWithoutKey(Q, L_a)
  std::vector<std::vector<ValueType>> handleGroupWithoutKey(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    return handleProjectionDistinct(op, strengthenedTopDownValue, headValue,
                                    argValues, reorderMapping);
  }

  // GroupWithKey(Q, L_g, L_a)
  std::vector<std::vector<ValueType>> handleGroupWithKey(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    //first, read the SupersetOfOutputBitVec
    bool superSetOfOutputBitVec =
        getSupersetOfOutputBitVec(strengthenedTopDownValue);
    if (!superSetOfOutputBitVec) {
      return {std::vector<ValueType>(argValues.size(), LatticeType::top())};
    }
    //then, read list info from L_g
    std::vector<std::pair<std::vector<int>, ValueType>>
        listIndicesForGroupByList =
            getListIndicesFromOrderedTupleOfSelectIntervalColumns(op,
                                                                  argValues[1]);
    //get the group by list length, by getting the size of the first pair's first element
    int groupByListLength = listIndicesForGroupByList[0].first.size();
    //then, read list info from L_a
    std::vector<std::pair<std::vector<int>, ValueType>> listIndicesForAggList =
        getListIndicesFromOrderedTupleOfSelectIntervalColumns(op, argValues[2]);

    //combine them together and get the result
    std::vector<std::vector<ValueType>> result;
    for (const auto& item : listIndicesForGroupByList) {
      for (const auto& item2 : listIndicesForAggList) {
        std::vector<int> selectedIndices;
        selectedIndices.reserve(item.first.size() + item2.first.size());
        selectedIndices.insert(selectedIndices.end(), item.first.begin(),
                               item.first.end());
        selectedIndices.insert(selectedIndices.end(), item2.first.begin(),
                               item2.first.end());
        if (!everyColumnIsFromOneTable(selectedIndices)) {
          result.push_back(std::vector<ValueType>{
              ValueType{ValueType::TopType()}, item.second, item2.second});
        } else {
          result.push_back(std::vector<ValueType>{
              ValueType{std::in_place_type<PredicateSupersetOfOutput>},
              item.second, item2.second});
        }
      }
    }
    return result;
  }

  int getColNumFromAbstractValue(const ArgValueType& argValue) const {
    if (std::holds_alternative<PredicateNumOfColumns>(argValue.getValue())) {
      return std::get<PredicateNumOfColumns>(argValue.getValue())
          .getNumOfColumn();
    } else if (std::holds_alternative<ArgValueType::ConjunctionType>(
                   argValue.getValue())) {
      const auto& conjunction =
          std::get<ArgValueType::ConjunctionType>(argValue.getValue());
      for (const auto& item : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateNumOfColumns>(item)) {
          return std::get<PredicateNumOfColumns>(item).getNumOfColumn();
        }
      }
    }
    throw common::InvalidArgument(
        "Can't get column number from abstract value: 447");
  }

  //InnerJoin(Q1, Q2, join predicate)
  std::vector<std::vector<ValueType>> handleInnerJoin(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    //first, read the SupersetOfOutputBitVec
    bool superSetOfOutputBitVec =
        getSupersetOfOutputBitVec(strengthenedTopDownValue);
    if (!superSetOfOutputBitVec) {
      return {std::vector<ValueType>(argValues.size(), LatticeType::top())};
    }

    std::vector<std::vector<ValueType>> result = {
        std::vector<ValueType>(argValues.size(), LatticeType::top()),
        std::vector<ValueType>(argValues.size(), LatticeType::top())};
    if (superSetOfOutputBitVec) {
      result[0][0] = ValueType{std::in_place_type<PredicateSupersetOfOutput>};
      result[1][1] = ValueType{std::in_place_type<PredicateSupersetOfOutput>};
    }
    return result;
  }

  //LeftJoin(Q1, Q2, join predicate)
  std::vector<std::vector<ValueType>> handleLeftJoin(
      const OperatorType& op, const ValueType& strengthenedTopDownValue,
      const ArgValueType& headValue, const std::vector<ArgValueType>& argValues,
      const std::vector<int>& reorderMapping) const {
    return handleInnerJoin(op, strengthenedTopDownValue, headValue, argValues,
                           reorderMapping);
  }
};
};  // namespace foresighter::implementation::sql::semantics::transformers