#pragma once
/**
 * @file concrete_to_offline_construction_abstractor.hpp
 * @brief Abstraction function alpha: concrete SQL table -> abstract predicates.
 *
 * Maps a ConcreteValue (actual table) to an OfflineConstructionValue (conjunction
 * of predicates such as column types, subset-of-input, and row-number ranges).
 * Used online to abstract IO example values for matching against FTA states.
 */
#include <foresighter/implementation/basics/abstractor.hpp>
#include <utility>
#include <variant>
#include "foresighter/common/exceptions.hpp"
#include "foresighter/common/variant.hpp"
#include "foresighter/implementation/basics/lattice.hpp"
#include "foresighter/implementation/basics/value.hpp"
#include "foresighter/implementation/sql/abstract_value/predicate_row_num_range.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/grammar_generator.hpp"
#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"
#include "foresighter/implementation/sql/semantics/values/concrete_value.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"

namespace foresighter::implementation::sql::semantics::abstractors {
using namespace foresighter::implementation::sql::semantics::values;

class ConcreteToOfflineConstructionAbstractor
    : public Abstractor<ConcreteValue, OfflineConstructionValue> {
 public:
  using Lattice =
      foresighter::implementation::basics::Lattice<OfflineConstructionValue>;
  using ColumnValueSet = emhash8hashset::HashSet<ConcreteTable::CellValue>;
  using TableColumnSets = std::vector<ColumnValueSet>;
  using AllTableSets = std::vector<TableColumnSets>;

  ConcreteToOfflineConstructionAbstractor() = delete;
  ConcreteToOfflineConstructionAbstractor(
      const ConcreteToOfflineConstructionAbstractor&) = delete;
  ConcreteToOfflineConstructionAbstractor& operator=(
      const ConcreteToOfflineConstructionAbstractor&) = delete;

  explicit ConcreteToOfflineConstructionAbstractor(
      const SQLBenchmark& benchmark)
      : _cachedColumnValueSets(buildColumnValueSets(benchmark)) {}

  [[nodiscard]] OfflineConstructionValue abstract(
      const ConcreteValue& value) const {
    const auto* concreteTable = std::get_if<ConcreteTable>(&value.getValue());
    if (!concreteTable) {
      throw common::InvalidArgument("ConcreteTable expected");
    }

    auto result =
        Lattice::meet(getAbstractPredicateForNumOfColPredicate(*concreteTable),
                      getAbstractPredicateForSubsetOfInput(*concreteTable));
    result = Lattice::meet(
        result, getAbstractPredicateForColTypePredicate(*concreteTable));

    return result;
  }

  [[nodiscard]] static bool impliesRowNumRange(
      const std::vector<int>& rowNumOfinput, int rowNumOfoutput,
      const OfflineConstructionValue& value) {
    auto _predicate = getConcreteRowNumRangeFromPredicate(value);
    if (std::holds_alternative<Top>(_predicate.getValue())) {
      return true;
    }
    auto predicate = std::get<PredicateRowNumRange>(_predicate.getValue());
    int lowerBound = predicate.getLowerBound().toConcrete(rowNumOfinput);
    int upperBound = predicate.getUpperBound().toConcrete(rowNumOfinput);
    return lowerBound <= rowNumOfoutput && rowNumOfoutput <= upperBound;
  }

  [[nodiscard]] static OfflineConstructionValue removeRowNumRange(
      const OfflineConstructionValue& value) {
    using VariantType = OfflineConstructionValue::ConjunctionType::VariantType;
    std::vector<VariantType> result;
    if (std::holds_alternative<OfflineConstructionValue::ConjunctionType>(
            value.getValue())) {
      auto conjunction =
          std::get<OfflineConstructionValue::ConjunctionType>(value.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (!std::holds_alternative<PredicateRowNumRange>(predicate)) {
          result.push_back(predicate);
        }
      }
    } else if (!std::holds_alternative<PredicateRowNumRange>(
                   value.getValue())) {
      return value;
    }
    return OfflineConstructionValue{
        std::in_place_type<OfflineConstructionValue::ConjunctionType>, result};
  }

 private:
  static AllTableSets buildColumnValueSets(const SQLBenchmark& benchmark) {
    const auto& inputTables = benchmark.getIOPairs()[0].getInput();
    AllTableSets result(inputTables.size());

    for (size_t tableIdx = 0; tableIdx < inputTables.size(); ++tableIdx) {
      const auto& table = inputTables[tableIdx];
      const size_t numCols = table.getNumColumns();
      result[tableIdx].resize(numCols);

      for (const auto& row : table.getTable()) {
        for (size_t col = 0; col < numCols; ++col) {
          result[tableIdx][col].insert(row[col]);
        }
      }
    }
    return result;
  }

  static OfflineConstructionValue getAbstractPredicateForNumOfColPredicate(
      const ConcreteTable& concrete) {
    const auto numCols = concrete.getNumColumns();
    return OfflineConstructionValue{std::in_place_type<PredicateNumOfColumns>,
                                    numCols};
  }

  [[nodiscard]] OfflineConstructionValue getAbstractPredicateForSubsetOfInput(
      const ConcreteTable& concrete) const {
    const auto colNumOfConcrete = concrete.getNumColumns();
    std::vector<std::vector<std::vector<bool>>> isSubsetOfInput(
        _cachedColumnValueSets.size());

    // Initialize subset flags
    for (size_t i = 0; i < _cachedColumnValueSets.size(); ++i) {
      isSubsetOfInput[i].resize(_cachedColumnValueSets[i].size());
      for (auto& cols : isSubsetOfInput[i]) {
        cols.resize(colNumOfConcrete, true);
      }
    }

    // Check subset relationships
    for (const auto& row : concrete.getTable()) {
      for (int concreteCol = 0; concreteCol < colNumOfConcrete; ++concreteCol) {
        const auto& value = row[concreteCol];

        for (size_t inputTable = 0; inputTable < _cachedColumnValueSets.size();
             ++inputTable) {
          for (size_t inputCol = 0;
               inputCol < _cachedColumnValueSets[inputTable].size();
               ++inputCol) {
            if (!isSubsetOfInput[inputTable][inputCol][concreteCol]) {
              continue;
            }
            if (!_cachedColumnValueSets[inputTable][inputCol].contains(value)) {
              isSubsetOfInput[inputTable][inputCol][concreteCol] = false;
            }
          }
        }
      }
    }

    // Build conjunction predicates
    std::vector<OfflineConstructionValue::ConjunctionType::VariantType>
        conjunctions;
    for (size_t inputTable = 0; inputTable < _cachedColumnValueSets.size();
         ++inputTable) {
      for (size_t inputCol = 0;
           inputCol < _cachedColumnValueSets[inputTable].size(); ++inputCol) {
        for (size_t concreteCol = 0; concreteCol < colNumOfConcrete;
             ++concreteCol) {
          if (isSubsetOfInput[inputTable][inputCol][concreteCol]) {
            conjunctions.emplace_back(PredicateSubsetOfInput{
                static_cast<int>(inputTable), static_cast<int>(concreteCol)});
          }
        }
      }
    }

    return OfflineConstructionValue{
        std::in_place_type<OfflineConstructionValue::ConjunctionType>,
        OfflineConstructionValue::ConjunctionType{conjunctions}};
  }

  [[nodiscard]] static OfflineConstructionValue
  getAbstractPredicateForColTypePredicate(const ConcreteTable& concrete) {
    auto result = Lattice::top();
    const auto& colInfos = concrete.getColumnDataTypes();
    //for each column, read its type and create a predicate
    for (int i = 0; i < colInfos.size(); ++i) {
      auto colType = colInfos[i].getType();
      result = Lattice::meet(
          result, OfflineConstructionValue{std::in_place_type<PredicateColType>,
                                           PredicateColType{i, colType}});
    }
    return result;
  }

  [[nodiscard]] static OfflineConstructionValue
  getConcreteRowNumRangeFromPredicate(
      const OfflineConstructionValue& predicate) {
    if (std::holds_alternative<OfflineConstructionValue::ConjunctionType>(
            predicate.getValue())) {
      auto conjunction = std::get<OfflineConstructionValue::ConjunctionType>(
          predicate.getValue());
      for (const auto& predicate : conjunction.getConjunction()) {
        if (std::holds_alternative<PredicateRowNumRange>(predicate)) {
          return OfflineConstructionValue{
              std::get<PredicateRowNumRange>(predicate)};
        }
      }
    }
    if (std::holds_alternative<PredicateRowNumRange>(predicate.getValue())) {
      return OfflineConstructionValue{
          std::get<PredicateRowNumRange>(predicate.getValue())};
    }
    return Lattice::top();
  }

  AllTableSets _cachedColumnValueSets;
};

}  // namespace foresighter::implementation::sql::semantics::abstractors