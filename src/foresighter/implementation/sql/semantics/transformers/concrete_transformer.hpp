#pragma once
/**
 * @file concrete_transformer.hpp
 * @brief Concrete SQL execution via SQLite for online program evaluation.
 *
 * Implements the concrete transformer that executes SQL operator semantics
 * (select, join, project, group-by, etc.) on actual tables using an in-memory
 * SQLite database. Used during the Search phase to verify candidate programs.
 */

#include <sqlite3.h>

#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/variant.hpp>
#include <foresighter/implementation/basics/term.hpp>
#include <foresighter/implementation/basics/transformer.hpp>
#include <foresighter/implementation/basics/value.hpp>
#include <foresighter/implementation/sql/abstract_value/top.hpp>
#include <foresighter/implementation/sql/abstract_value/unabstracted_term.hpp>
#include <foresighter/implementation/sql/concrete_value/concrete_table.hpp>
#include <foresighter/implementation/sql/concrete_value/error.hpp>
#include <foresighter/implementation/sql/symbol.hpp>
#include <variant>
#include "foresighter/common/hash.hpp"
#include "foresighter/implementation/basics/types.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/semantics/values/concrete_value.hpp"
#include "foresighter/implementation/sql/symbol/op_filter_neq.hpp"
#include "foresighter/implementation/sql/symbol/op_group_with_key.hpp"
#include "foresighter/implementation/sql/symbol/op_identity.hpp"
#include "foresighter/implementation/sql/symbol/op_index_in_interval.hpp"
#include "foresighter/implementation/sql/symbol/op_projection_distinct.hpp"
#include "foresighter/implementation/sql/symbol/op_select.hpp"
#include "foresighter/implementation/sql/symbol/var_filter_const.hpp"
#include "foresighter/implementation/sql/symbol/var_table.hpp"

namespace foresighter::implementation::sql::semantics::transformers {

using namespace foresighter::implementation::basics;

namespace sql_operations {
using namespace foresighter::implementation;
using SymbolToStrAndColSize = std::pair<std::string, size_t>;

bool isOperatorGeneratingTables(const sql::Symbol& op) {
  auto& symbol = op.getSymbol();
  return std::holds_alternative<OpProjection>(symbol) ||
         std::holds_alternative<OpProjectionDistinct>(symbol) ||
         std::holds_alternative<OpGroupByOrderByCountLimit>(symbol) ||
         std::holds_alternative<OpOrderByColLimit>(symbol) ||
         std::holds_alternative<OpGroupWithoutKey>(symbol) ||
         std::holds_alternative<OpGroupWithKey>(symbol) ||
         std::holds_alternative<OpInnerJoin>(symbol) ||
         std::holds_alternative<OpLeftJoin>(symbol) ||
         std::holds_alternative<OpSelect>(symbol);
}

[[nodiscard]] std::string idxToTableName(IDType idx) {
  return "_T" + std::to_string(idx);
}

void executeSQL(const std::string& sql, sqlite3* db) {
  char* errMsg = nullptr;
  sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
  if (errMsg) {
    throw foresighter::common::InvalidArgument(
        "Failed to execute SQL: " + sql.substr(0, 100) +
        " reason: " + std::string(errMsg));
  }
}

int parseColumnDeclaredType(const char* declaredType) {
  //if null, then return -1
  if (declaredType == nullptr) {
    return -1;
  }
  //if numeric, then return SQLITE_FLOAT
  if (strcmp(declaredType, "NUMERIC") == 0 ||
      strcmp(declaredType, "REAL") == 0 ||
      strcmp(declaredType, "INTEGER") == 0 ||
      strcmp(declaredType, "INT") == 0 || strcmp(declaredType, "BIGINT") == 0 ||
      strcmp(declaredType, "TINYINT") == 0 ||
      strcmp(declaredType, "SMALLINT") == 0 ||
      strcmp(declaredType, "MEDIUMINT") == 0 ||
      strcmp(declaredType, "BIGINT") == 0 ||
      strcmp(declaredType, "TINYINT") == 0) {
    return SQLITE_FLOAT;
  } else {
    return SQLITE_TEXT;
  }
}

std::vector<int> getNullableDeclaredColTypes(sqlite3_stmt* stmt) {
  std::vector<int> colDataTypesNullable;
  int numCols = sqlite3_column_count(stmt);
  colDataTypesNullable.reserve(numCols);
  for (int i = 0; i < numCols; i++) {
    colDataTypesNullable.emplace_back(
        parseColumnDeclaredType(sqlite3_column_decltype(stmt, i)));
  }
  return colDataTypesNullable;
}

uint32_t buildNullColsBitMask(
    const std::vector<int>& declaredColTypesNullable) {
  uint32_t nullColsBitMask = 0;
  for (int i = 0; i < declaredColTypesNullable.size(); i++) {
    if (declaredColTypesNullable[i] == -1) {
      nullColsBitMask |= 1 << i;
    }
  }
  return nullColsBitMask;
}

void updateNullDeclaredColType(std::vector<int>& declaredColTypesNullable,
                               int colIdx, int colValueType) {
  if (colValueType == SQLITE_FLOAT || colValueType == SQLITE_INTEGER) {
    declaredColTypesNullable[colIdx] = SQLITE_FLOAT;
  } else if (colValueType == SQLITE_TEXT) {
    declaredColTypesNullable[colIdx] = SQLITE_TEXT;
  } else {
    assert(false);
  }
}

ConcreteTable parseTableFromSQLiteUsingDeclaredColTypes(
    sqlite3_stmt* stmt, std::vector<int>& declaredColTypesNullable) {
  uint32_t nullColsBitMask = buildNullColsBitMask(declaredColTypesNullable);
  std::vector<std::vector<ConcreteTable::CellValue>> table;
  auto numCols = sqlite3_column_count(stmt);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::vector<ConcreteTable::CellValue> row;
    for (int i = 0; i < numCols; i++) {
      int colValueType = sqlite3_column_type(stmt, i);
      if (nullColsBitMask & (1 << i) and colValueType != SQLITE_NULL) {
        updateNullDeclaredColType(declaredColTypesNullable, i, colValueType);
        nullColsBitMask &= ~(1 << i);
      }
      switch (colValueType) {
        case SQLITE_INTEGER:
          row.emplace_back(sqlite3_column_int(stmt, i));
          break;
        case SQLITE_FLOAT:
          row.emplace_back(sqlite3_column_double(stmt, i));
          break;
        case SQLITE_TEXT:
          row.emplace_back(
              reinterpret_cast<const char*>(sqlite3_column_text(stmt, i)),
              false);
          break;
        case SQLITE_NULL:
          row.emplace_back(nullptr);
          break;
        default:
          throw common::InvalidArgument("Unsupported column type: " +
                                        std::to_string(colValueType));
      }
    }
    table.emplace_back(row);
  }
  std::vector<ConcreteTable::ColumnDataType> colDataTypes;
  for (int& i : declaredColTypesNullable) {
    if (i == -1) {
      i = SQLITE_TEXT;
    }
    colDataTypes.emplace_back(i);
  }
  return ConcreteTable{table, colDataTypes};
}

[[nodiscard]] foresighter::implementation::sql::ConcreteTable
executeSQLAndGetTable(const std::string& sql, sqlite3* db) {
  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    throw foresighter::common::InvalidArgument(
        "Failed to prepare SQL: " + sql.substr(0, 100) +
        " reason: " + std::string(sqlite3_errmsg(db)));
  }

  auto declaredColTypesNullable = getNullableDeclaredColTypes(stmt);
  auto concreteTable =
      parseTableFromSQLiteUsingDeclaredColTypes(stmt, declaredColTypesNullable);
  sqlite3_finalize(stmt);
  return concreteTable;
};

std::string generateDropTableSQL(IDType idx, std::string* tableName = nullptr) {
  std::string sql = "DROP TABLE IF EXISTS ";
  if (tableName != nullptr) {
    sql += '"' + normalizeString(*tableName) + '"';
  } else {
    sql += idxToTableName(idx);
  }
  sql += ";";
  return sql;
}

std::string generateInsertTableSQLTableStructurePart(
    const ConcreteTable& table, IDType idx,
    const std::string* tableName = nullptr,
    const std::vector<std::string>* colNames = nullptr) {
  std::string sql = "CREATE TABLE ";
  if (tableName != nullptr) {
    sql += '"' + normalizeString(*tableName) + '"';
  } else {
    sql += idxToTableName(idx);
  }
  sql += " (";
  auto colDataTypes = table.getColumnDataTypes();
  for (int i = 0; i < colDataTypes.size(); ++i) {
    if (colNames != nullptr) {
      sql += '"' + normalizeString((*colNames)[i]) + '"' +
             std::string(colDataTypes[i].toString());
    } else {
      sql += "c" + std::to_string(i) + " " +
             std::string(colDataTypes[i].toString());
    }
    if (i != colDataTypes.size() - 1) {
      sql += ", ";
    }
  }
  sql += "); ";
  return sql;
}

std::string generateInsertTableSQLTableDataPart(
    const ConcreteTable& table, IDType idx,
    const std::string* tableName = nullptr,
    const std::vector<std::string>* colNames = nullptr) {
  std::string sql = "INSERT INTO ";
  if (tableName != nullptr) {
    sql += '"' + normalizeString(*tableName) + '"';
  } else {
    sql += idxToTableName(idx);
  }
  sql += " VALUES ";
  auto concreteTable = table.getTable();
  for (int i = 0; i < concreteTable.size(); i++) {
    sql += "(";
    for (int j = 0; j < concreteTable[i].size(); j++) {
      sql += concreteTable[i][j].toString();
      if (j != concreteTable[i].size() - 1) {
        sql += ", ";
      }
    }
    sql += ")";
    if (i != concreteTable.size() - 1) {
      sql += ", ";
    }
  }
  sql += ";";
  return sql;
}

std::string generateCreateTableSQL(
    const ConcreteTable& table, IDType idx,
    const std::string* tableName = nullptr,
    const std::vector<std::string>* colNames = nullptr) {
  std::string sql =
      generateInsertTableSQLTableStructurePart(table, idx, tableName, colNames);
  if (!table.getTable().empty()) {
    sql += generateInsertTableSQLTableDataPart(table, idx, tableName, colNames);
  }
  return sql;
}

void addVariablesToContext(
    const foresighter::implementation::sql::SQLBenchmark& benchmark,
    basics::Context<sql::Symbol, SymbolToStrAndColSize>& context) {
  for (size_t i = 0; i < benchmark.getInputTableCount(); ++i) {
    context.bind(sql::Symbol{VarTable(i)},
                 std::make_pair(
                     benchmark.getBenchmarkTableSQL(i),
                     benchmark.getIOPairs()[0].getInput()[i].getNumColumns()));
  }
}

//don't need this, for special optimization (we directly read from hint strings to avoid context lookup)
// void addHintsToContext(
//     const foresighter::implementation::sql::SQLBenchmark& benchmark,
//     basics::Context<sql::Symbol, SymbolToStrAndColSize>& context) {
//   for (int i = 0; i < benchmark.getHints().size(); ++i) {
//     context.bind(sql::Symbol{VarFilterConst(i)},
//                  std::make_pair(benchmark.getHints()[i], 0));
//   }
// }

// don't need this, for special optimization (we directly use c<idx> to refer to the column index)
// void addColIdxToContext(
//     const foresighter::implementation::sql::SQLBenchmark& benchmark,
//     basics::Context<sql::Symbol, SymbolToStrAndColSize>& context) {
//   for (int i = 0;
//        i < constants::Constants::sqlSetting.maxNumberOfColumnsInInput; ++i) {
//     context.bind(sql::Symbol{ConstColumnIndex(i)},
//                  std::make_pair("c" + std::to_string(i), 0));
//   }
// }

std::string generateJoinSelectList(size_t innerTableColSizeLeft,
                                   size_t innerTableColSizeRight) {
  std::string sql;
  size_t curIdx = 0;
  for (int i = 0; i < innerTableColSizeLeft; ++i, ++curIdx) {
    if (i == innerTableColSizeLeft - 1) {
      sql += "T1.c" + std::to_string(i) + " AS c" + std::to_string(curIdx);
    } else {
      sql +=
          "T1.c" + std::to_string(i) + " AS c" + std::to_string(curIdx) + ", ";
    }
  }
  sql += ", ";
  for (int i = 0; i < innerTableColSizeRight; ++i, ++curIdx) {
    if (i == innerTableColSizeRight - 1) {
      sql += "T2.c" + std::to_string(i) + " AS c" + std::to_string(curIdx);
    } else {
      sql +=
          "T2.c" + std::to_string(i) + " AS c" + std::to_string(curIdx) + ", ";
    }
  }
  return sql;
}

void insertTablesToDB(
    const foresighter::implementation::sql::SQLBenchmark& benchmark,
    sqlite3* db) {
  for (size_t i = 0; i < benchmark.getInputTableCount(); ++i) {
    std::string insertSQL =
        generateCreateTableSQL(benchmark.getIOPairs()[0].getInput()[i], i,
                               &benchmark.getBenchmarkTableName(i),
                               &benchmark.getBenchmarkTableColNames(i));
    executeSQL(insertSQL, db);
  }
}

basics::Context<sql::Symbol, SymbolToStrAndColSize>
getInitialContextFromBenchmark(
    const foresighter::implementation::sql::SQLBenchmark& benchmark,
    sqlite3* db) {

  insertTablesToDB(benchmark, db);
  basics::Context<sql::Symbol, SymbolToStrAndColSize> context;
  addVariablesToContext(benchmark, context);
  // addHintsToContext(benchmark, context);
  // addColIdxToContext(benchmark, context); // optimization: all columns are used as c<idx>. Don't need to look up the context
  return context;
}
}  // namespace sql_operations

class ConcreteTransformer : public Transformer {
 public:
  using SymbolType = sql::Symbol;
  using ValueType = values::ConcreteValue;

  ConcreteTransformer() = delete;
  ConcreteTransformer(const ConcreteTransformer&) = delete;
  ConcreteTransformer(ConcreteTransformer&&) = delete;
  ConcreteTransformer& operator=(const ConcreteTransformer&) = delete;
  ConcreteTransformer& operator=(ConcreteTransformer&&) = delete;

  ~ConcreteTransformer() { sqlite3_close(_db); }

  explicit ConcreteTransformer(
      const foresighter::implementation::sql::SQLBenchmark& benchmark) {
    auto start = std::chrono::high_resolution_clock::now();
    initializeDBConnection();
    _contextForBenchmark =
        sql_operations::getInitialContextFromBenchmark(benchmark, _db);
    _tableNamesInSQLite = TableNamesInSQLite(_db);
    setHintStrings(benchmark);
    auto end = std::chrono::high_resolution_clock::now();
    _interpreterStatistics.totalInterpreterTimeInNs +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
  }

  static bool isInnerJoinAndLeftTableHashLargerThanRightTableHash(
      const SymbolType& op, const std::vector<ValueType>& values) {
    if (std::holds_alternative<OpInnerJoin>(op.getSymbol())) {
      size_t leftTableHash = 0;
      size_t rightTableHash = 0;
      common::hashCombine(leftTableHash, values[0]);
      common::hashCombine(rightTableHash, values[1]);
      return leftTableHash > rightTableHash;
    }
    return false;
  }

  [[nodiscard]] std::string wrapProjectionWithOrder(
      const std::string& sql, const std::vector<int>& reordering) {
    bool ordered = true;
    for (int i = 0; i < reordering.size(); ++i) {
      if (reordering[i] != i) {
        ordered = false;
        break;
      }
    }
    if (ordered) {
      return sql;
    }
    std::string sqlWithOrder = "SELECT ";
    for (int i = 0; i < reordering.size(); ++i) {
      sqlWithOrder +=
          "c" + std::to_string(reordering[i]) + " AS c" + std::to_string(i);
      if (i != reordering.size() - 1) {
        sqlWithOrder += ", ";
      }
    }
    sqlWithOrder += " FROM (" + sql + ") AS T";
    return sqlWithOrder;
  }

  [[nodiscard]] ValueType transform(const SymbolType& op,
                                    const std::vector<ValueType>& values,
                                    const std::vector<int>& reordering) {
    _interpreterStatistics.numInterpreterCalls++;
    if (std::holds_alternative<OpIdentity>(op.getSymbol())) {
      if (std::holds_alternative<Bottom>(values[0].getValue())) {
        throw foresighter::common::InvalidArgument(
            "Bottom value is not supported in identity operator");
      }
      return values[0];
    }
    if (sql_operations::isOperatorGeneratingTables(op)) {
      _interpreterStatistics.numQueriesEvaluated++;
      //if it's inner join and the left table's hash is larger than the right table's hash, we return bottom
      //to break the symmetry of the two tables
      auto interpreter_start_time = std::chrono::high_resolution_clock::now();
      if (isInnerJoinAndLeftTableHashLargerThanRightTableHash(op, values)) {
        return (ValueType)Bottom();
      }
      try {
        auto compiledSQL =
            wrapProjectionWithOrder(compileSQL(op, values), reordering);
        auto result = sql_operations::executeSQLAndGetTable(compiledSQL, _db);
        auto returnedValue =
            ValueType{std::in_place_type<ConcreteTable>, std::move(result)};
        auto interpreter_end_time = std::chrono::high_resolution_clock::now();
        _interpreterStatistics.totalInterpreterTimeInNs +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                interpreter_end_time - interpreter_start_time)
                .count();
        return returnedValue;
      } catch (const std::exception& e) {
        auto interpreter_end_time = std::chrono::high_resolution_clock::now();
        _interpreterStatistics.totalInterpreterTimeInNs +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                interpreter_end_time - interpreter_start_time)
                .count();
        return ValueType{std::in_place_type<Error>, e.what()};
      }
    } else {
      return ValueType{std::in_place_type<UnAbstractedTerm>,
                       buildUnAbstractedTerm(op, values)};
    }
  }

  [[nodiscard]] std::string getSQLFromTerm(
      const basics::Term<sql::Symbol>& term) const {
    return _getSQLFromTerm(term, _contextForBenchmark);
  }

  struct InterpreterStatistics {
    size_t numInterpreterCalls = 0;
    size_t numQueriesEvaluated = 0;
    size_t totalInterpreterTimeInNs = 0;
  };

  [[nodiscard]] size_t getNumInterpreterCalls() const {
    return _interpreterStatistics.numInterpreterCalls;
  }

  [[nodiscard]] size_t getNumQueriesEvaluated() const {
    return _interpreterStatistics.numQueriesEvaluated;
  }

  [[nodiscard]] size_t getTotalInterpreterTimeInNs() const {
    return _interpreterStatistics.totalInterpreterTimeInNs;
  }

  [[nodiscard]] size_t getNumTablesCreated() const {
    return _tableNamesInSQLite.getNumTablesCreated();
  }

  [[nodiscard]] size_t getNumTablesInCache() const {
    return _tableNamesInSQLite.getNumTablesInCache();
  }

  [[nodiscard]] size_t getNumTablesNotCreatedDueToCache() const {
    return _tableNamesInSQLite.getNumTablesNotCreatedDueToCache();
  }

 private:
  std::string _getSQLFromTerm(
      const basics::Term<sql::Symbol>& term,
      const basics::Context<sql::Symbol, sql_operations::SymbolToStrAndColSize>&
          context,
      size_t* innerTableColSize = nullptr, size_t* generateStartIdx = nullptr,
      bool isInLeftJoin = false) const {
    auto& opSymbol = term.getSymbol().getSymbol();
    auto& children = term.getChildren();
    return std::visit(
        foresighter::common::overloaded{
            [&](const OpIdentity&) {
              assert(children.size() == 1);
              return _getSQLFromTerm(children[0], context, innerTableColSize,
                                     generateStartIdx, isInLeftJoin);
            },
            [&](const OpIndexInInterval&) {
              assert(children.size() == 1);
              return _getSQLFromTerm(children[0], context, innerTableColSize,
                                     generateStartIdx, isInLeftJoin);
            },
            [&](const VarFilterConst& symbol) -> std::string {
              return _hintStrings
                  [symbol
                       .getIndex()];  //here we don't need to normalize the string, since when read we directly normalize the string
            },
            [&](const VarTable& symbol) -> std::string {
              if (innerTableColSize != nullptr) {
                *innerTableColSize = context.get(sql::Symbol{symbol}).second;
              }
              return context.get(sql::Symbol{symbol}).first;
            },
            [&](const ConstColumnIndex& symbol) -> std::string {
              // optimization: instead of looking up the context, we can directly use the column index
              // return context.get(sql::Symbol{symbol}).first;
              return "c" + std::to_string(symbol.getIndex());
            },
            [&](const OpAvg&) -> std::string {
              assert(children.size() == 1);
              return "AVG(" + _getSQLFromTerm(children[0], context) + ")";
            },
            [&](const OpCount&) -> std::string {
              assert(children.size() == 1);
              return "COUNT(" + _getSQLFromTerm(children[0], context) + ")";
            },
            [&](const OpCountDistinct&) -> std::string {
              assert(children.size() == 1);
              return "COUNT(DISTINCT " + _getSQLFromTerm(children[0], context) +
                     ")";
            },
            [&](const OpFilterAnd&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) + " AND " +
                     _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterEq&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) + " = " +
                     _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterGeq&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) +
                     " >= " + _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterGt&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) + " > " +
                     _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterLeq&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) +
                     " <= " + _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterLt&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) + " < " +
                     _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterLike&) -> std::string {
              assert(children.size() == 2);
              auto dealWithLikeStr = [](const std::string& str) -> std::string {
                if (str[0] == '\'') {
                  return "\'%" + str.substr(1, str.size() - 2) + "%\'";
                } else {
                  return "'%" + str + "%'";
                }
              };
              //for like, we need to manually add % to the beginning and end of the second child, and \" to the second child
              return _getSQLFromTerm(children[0], context) + " LIKE " +
                     dealWithLikeStr(_getSQLFromTerm(children[1], context));
            },
            [&](const OpFilterNeq&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) +
                     " != " + _getSQLFromTerm(children[1], context);
            },
            [&](const OpFilterOr&) -> std::string {
              assert(children.size() == 2);
              return _getSQLFromTerm(children[0], context) + " OR " +
                     _getSQLFromTerm(children[1], context);
            },
            [&](const OpGroupWithKey&) -> std::string {
              assert(children.size() == 3);
              size_t startIdx = 0;
              auto innerStr = _getSQLFromTerm(children[0], context);
              auto groupKeysStrWithRename =
                  _getSQLFromTerm(children[1], context, nullptr, &startIdx);
              assert(startIdx > 0);
              auto groupKeysStr = _getSQLFromTerm(children[1], context);
              auto AggregationsStr =
                  _getSQLFromTerm(children[2], context, nullptr, &startIdx);
              assert(startIdx > 0);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = startIdx;
              }
              return "SELECT " + groupKeysStrWithRename + ", " +
                     AggregationsStr + " FROM (" + innerStr +
                     ") AS T GROUP BY " + groupKeysStr;
            },
            [&](const OpGroupWithoutKey&) -> std::string {
              assert(children.size() == 2);
              size_t startIdx = 0;
              auto innerStr = _getSQLFromTerm(children[0], context);
              auto aggregationsStr =
                  _getSQLFromTerm(children[1], context, nullptr, &startIdx);
              assert(startIdx > 0);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = startIdx;
              }
              return "SELECT " + aggregationsStr + " FROM (" + innerStr +
                     ") AS T ";
            },
            [&](const OpJoinAnd&) -> std::string {
              assert(children.size() == 2);
              auto leftStr = _getSQLFromTerm(children[0], context);
              auto rightStr = _getSQLFromTerm(children[1], context);
              return leftStr + " AND " + rightStr;
            },
            [&](const OpJoinEq&) -> std::string {
              assert(children.size() == 2);
              auto leftStr = _getSQLFromTerm(children[0], context);
              auto rightStr = _getSQLFromTerm(children[1], context);
              std::string sql = "T1." + leftStr + " = T2." + rightStr;
              if (isInLeftJoin) {
                sql += " WHERE T2." + rightStr + " IS NULL ";
              }
              return sql;
            },
            [&](const OpInnerJoin&) -> std::string {
              assert(children.size() == 3);
              size_t innerTableColSizeLeft = 0;
              size_t innerTableColSizeRight = 0;
              auto leftStr =
                  _getSQLFromTerm(children[0], context, &innerTableColSizeLeft);
              assert(innerTableColSizeLeft > 0);
              auto rightStr = _getSQLFromTerm(children[1], context,
                                              &innerTableColSizeRight);
              assert(innerTableColSizeRight > 0);
              auto joinCond = _getSQLFromTerm(children[2], context);
              if (innerTableColSize != nullptr) {
                *innerTableColSize =
                    innerTableColSizeLeft + innerTableColSizeRight;
              }
              return "SELECT " +
                     sql_operations::generateJoinSelectList(
                         innerTableColSizeLeft, innerTableColSizeRight) +
                     " FROM (" + leftStr + ") AS T1 INNER JOIN (" + rightStr +
                     ") AS T2 ON " + joinCond;
            },
            [&](const OpLeftJoin&) -> std::string {
              assert(children.size() == 3);
              size_t innerTableColSizeLeft = 0;
              size_t innerTableColSizeRight = 0;
              auto leftStr =
                  _getSQLFromTerm(children[0], context, &innerTableColSizeLeft);
              assert(innerTableColSizeLeft > 0);
              auto rightStr = _getSQLFromTerm(children[1], context,
                                              &innerTableColSizeRight);
              assert(innerTableColSizeRight > 0);
              auto joinCond =
                  _getSQLFromTerm(children[2], context, nullptr, nullptr, true);
              if (innerTableColSize != nullptr) {
                *innerTableColSize =
                    innerTableColSizeLeft + innerTableColSizeRight;
              }
              return "SELECT " +
                     sql_operations::generateJoinSelectList(
                         innerTableColSizeLeft, innerTableColSizeRight) +
                     " FROM (" + leftStr + ") AS T1 LEFT JOIN (" + rightStr +
                     ") AS T2 ON " + joinCond;
            },
            [&](const OpAggList&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpProjList&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpGroupByList&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpOrderByDesc&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpOrderByAsc&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpWindowList&) -> std::string {
              return generateListStr(children, context, generateStartIdx);
            },
            [&](const OpMax&) -> std::string {
              assert(children.size() == 1);
              return "MAX(" + _getSQLFromTerm(children[0], context) + ")";
            },
            [&](const OpMin&) -> std::string {
              assert(children.size() == 1);
              return "MIN(" + _getSQLFromTerm(children[0], context) + ")";
            },
            [&](const OpProjection&) -> std::string {
              assert(children.size() == 2);
              size_t startIdx = 0;
              auto innerStr = _getSQLFromTerm(children[0], context);
              auto projListStr =
                  _getSQLFromTerm(children[1], context, nullptr, &startIdx);
              assert(startIdx > 0);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = startIdx;
              }
              return "SELECT " + projListStr + " FROM (" + innerStr + ") AS T";
            },
            [&](const OpProjectionDistinct&) -> std::string {
              assert(children.size() == 2);
              size_t startIdx = 0;
              auto innerStr = _getSQLFromTerm(children[0], context);
              auto projListStr =
                  _getSQLFromTerm(children[1], context, nullptr, &startIdx);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = startIdx;
              }
              return "SELECT DISTINCT " + projListStr + " FROM (" + innerStr +
                     ") AS T";
            },
            [&](const OpGroupByOrderByCountLimit&) -> std::string {
              assert(children.size() == 2);
              size_t startIdx = 0;
              auto innerStr = _getSQLFromTerm(children[0], context);
              auto projListStr =
                  _getSQLFromTerm(children[1], context, nullptr, &startIdx);
              auto projListStrWithoutAs = _getSQLFromTerm(children[1], context);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = startIdx;
              }
              bool isDesc = std::holds_alternative<OpOrderByDesc>(
                  children[1].getSymbol().getSymbol());
              if (isDesc) {
                return "SELECT " + projListStr + " FROM (" + innerStr +
                       ") AS T GROUP BY " + projListStrWithoutAs +
                       " ORDER BY COUNT(*) DESC LIMIT 1";
              } else {
                return "SELECT " + projListStr + " FROM (" + innerStr +
                       ") AS T GROUP BY " + projListStrWithoutAs +
                       " ORDER BY COUNT(*) ASC LIMIT 1";
              }
            },
            [&](const OpOrderByColLimit&) -> std::string {
              assert(children.size() == 2);
              size_t selectedTableColSize = 0;
              auto innerStr =
                  _getSQLFromTerm(children[0], context, &selectedTableColSize);
              auto projListStrWithoutAs = _getSQLFromTerm(children[1], context);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = selectedTableColSize;
              }
              bool isDesc = std::holds_alternative<OpOrderByDesc>(
                  children[1].getSymbol().getSymbol());
              if (isDesc) {
                return "SELECT * FROM (" + innerStr + ") AS T ORDER BY " +
                       projListStrWithoutAs + " DESC LIMIT 1";
              } else {
                return "SELECT * FROM (" + innerStr + ") AS T ORDER BY " +
                       projListStrWithoutAs + " ASC LIMIT 1";
              }
            },
            [&](const OpSelect&) -> std::string {
              assert(children.size() == 2);
              size_t selectedTableColSize = 0;
              auto innerStr =
                  _getSQLFromTerm(children[0], context, &selectedTableColSize);
              auto predStr = _getSQLFromTerm(children[1], context);
              if (innerTableColSize != nullptr) {
                *innerTableColSize = selectedTableColSize;
              }
              return "SELECT * FROM (" + innerStr + ") AS T WHERE " + predStr;
            },
            [&](const OpSum&) -> std::string {
              assert(children.size() == 1);
              return "SUM(" + _getSQLFromTerm(children[0], context) + ")";
            },
            [&](const auto&) -> std::string {
              throw foresighter::common::InvalidArgument(
                  "Unsupported operation: " + term.toString());
            }},
        opSymbol);
  }

  std::string generateListStr(
      const std::vector<basics::Term<sql::Symbol>>& list,
      const basics::Context<sql::Symbol, sql_operations::SymbolToStrAndColSize>&
          context,
      size_t* generateStartIdx) const {
    std::string sql;
    if (generateStartIdx == nullptr) {
      for (int i = 0; i < list.size(); ++i) {
        if (i == list.size() - 1) {
          sql += _getSQLFromTerm(list[i], context) + " ";
        } else {
          sql += _getSQLFromTerm(list[i], context) + ", ";
        }
      }
    } else {
      for (int i = 0; i < list.size(); ++i, ++(*generateStartIdx)) {
        if (i == list.size() - 1) {
          sql += _getSQLFromTerm(list[i], context) + " AS c" +
                 std::to_string(*generateStartIdx);
        } else {
          sql += _getSQLFromTerm(list[i], context) + " AS c" +
                 std::to_string(*generateStartIdx) + ", ";
        }
      }
    }
    return sql;
  }

  void initializeDBConnection() {
    _db = nullptr;
    int rc = sqlite3_open(":memory:", &_db);
    if (rc != SQLITE_OK) {
      sqlite3_close(_db);
      throw common::InvalidArgument("Failed to open database: " +
                                    std::string(sqlite3_errmsg(_db)));
    }
  }

  class TableNamesInSQLite {
   public:
    TableNamesInSQLite() : _db(nullptr) {}

    explicit TableNamesInSQLite(sqlite3* db) noexcept : _db(db) {}

    [[nodiscard]] std::string getTableName(const ConcreteTable& table) {
      if (auto it = _cachedTableNames.find(table);
          it != _cachedTableNames.end()) {
        _numTablesNotCreatedDueToCache++;
        return sql_operations::idxToTableName(it->second);
      }
      auto newTableIdx = createTableInSQLite(table);
      _cachedTableNames[table] = newTableIdx;
      _numTablesCreated++;
      return sql_operations::idxToTableName(newTableIdx);
    }

    [[nodiscard]] unsigned long long getNumTablesCreated() const {
      return _numTablesCreated;
    }

    [[nodiscard]] unsigned long long getNumTablesInCache() const {
      return _cachedTableNames.size();
    }

    [[nodiscard]] unsigned long long getNumTablesNotCreatedDueToCache() const {
      return _numTablesNotCreatedDueToCache;
    }

   private:
    using IDType = foresighter::implementation::basics::IDType;

    sqlite3* _db;

    unsigned long long _numTablesCreated = 0;
    unsigned long long _numTablesNotCreatedDueToCache = 0;

    [[nodiscard]] IDType createTableInSQLite(const ConcreteTable& table) {
      auto freshID = _cachedTableNames.size();
      auto tableName = sql_operations::idxToTableName(freshID);
      std::string sql = sql_operations::generateCreateTableSQL(table, freshID);
      sql_operations::executeSQL(sql, _db);
      return freshID;
    }

    emhash8hashmap::HashMap<ConcreteTable, IDType> _cachedTableNames;
  };

  void setHintStrings(const SQLBenchmark& benchmark) {
    for (const auto& i : benchmark.getHints()) {
      double d;
      if (hasDoubleRepresentation(i, d)) {
        _hintStrings.push_back(i);
      } else {
        _hintStrings.push_back("'" + normalizeString(i) + "'");
      }
    }
  }

  [[nodiscard]] std::pair<
      basics::Context<sql::Symbol, sql_operations::SymbolToStrAndColSize>,
      basics::Term<SymbolType>>
  buildTemporaryContextAndTermForCompilation(
      const Symbol& op, const std::vector<ValueType>& values) {
    std::vector<basics::Term<SymbolType>> children;
    basics::Context<sql::Symbol, sql_operations::SymbolToStrAndColSize> context;
    unsigned int freshTableIdx = 0;
    children.reserve(values.size());
    for (auto& value : values) {
      if (std::holds_alternative<UnAbstractedTerm>(value.getValue())) {
        children.push_back(
            std::get<UnAbstractedTerm>(value.getValue()).getTerm());
      } else if (std::holds_alternative<ConcreteTable>(value.getValue())) {
        auto symbol = Symbol{VarTable(freshTableIdx++)};
        context.bind(
            symbol,
            std::make_pair(
                _tableNamesInSQLite.getTableName(
                    std::get<ConcreteTable>(value.getValue())),
                std::get<ConcreteTable>(value.getValue()).getNumColumns()));
        children.push_back(basics::Term<SymbolType>{symbol, {}});
      } else {
        throw foresighter::common::InvalidArgument("Unsupported value type: " +
                                                   value.toString());
      }
    }
    return std::make_pair(context, basics::Term<SymbolType>{op, children});
  }

  [[nodiscard]] std::string compileSQL(const Symbol& op,
                                       const std::vector<ValueType>& values) {
    auto temporaryContextAndTermForCompilation =
        buildTemporaryContextAndTermForCompilation(op, values);
    return _getSQLFromTerm(temporaryContextAndTermForCompilation.second,
                           temporaryContextAndTermForCompilation.first);
  }

  static UnAbstractedTerm buildUnAbstractedTerm(
      const Symbol& op, const std::vector<ValueType>& values) {
    std::vector<basics::Term<SymbolType>> children;
    children.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      // Every argument to a non-table-generating operator is expected to
      // still be symbolic (UnAbstractedTerm) at this point; fail with a
      // clear message rather than letting std::get throw an opaque
      // bad_variant_access if a caller ever passes a real ConcreteTable
      // here instead.
      if (!std::holds_alternative<UnAbstractedTerm>(values[i].getValue())) {
        throw foresighter::common::InvalidArgument(
            "buildUnAbstractedTerm: argument " + std::to_string(i) +
            " of operator (symbol index " +
            std::to_string(op.getSymbol().index()) +
            ") is not an UnAbstractedTerm (value variant index " +
            std::to_string(values[i].getValue().index()) + ")");
      }
      children.push_back(
          std::get<UnAbstractedTerm>(values[i].getValue()).getTerm());
    }
    return UnAbstractedTerm{basics::Term<SymbolType>{op, children}};
  }

  sqlite3* _db{};
  TableNamesInSQLite _tableNamesInSQLite;
  basics::Context<sql::Symbol, sql_operations::SymbolToStrAndColSize>
      _contextForBenchmark;
  std::vector<std::string>
      _hintStrings;  //a hack to optimize the context, so that we don't need to pass around this in the context.

  //statistics

  InterpreterStatistics _interpreterStatistics = {};
};

}  // namespace foresighter::implementation::sql::semantics::transformers