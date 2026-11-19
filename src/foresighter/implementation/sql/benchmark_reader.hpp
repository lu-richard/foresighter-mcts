/**
 * @file benchmark_reader.hpp
 * @brief Parses benchmark JSON files into SQLBenchmark objects.
 *
 * Reads input/output tables, ground-truth SQL queries, and filter-constant hints
 * from JSON benchmark files. Produces Benchmark<IOPair> instances for synthesis.
 * @author Rui Dong
 * @date 4/21/24
 */

#pragma once

#include <boost/json.hpp>
#include <foresighter/implementation/basics/benchmark.hpp>
#include <foresighter/implementation/basics/io_pair.hpp>
#include <foresighter/implementation/sql/types.hpp>
#include <fstream>
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"

namespace foresighter::implementation::sql {

std::string static normalizeString(const std::string& str) {
  //replace all "'" to "''" in the string
  std::string result;
  for (auto& c : str) {
    if (c == '\'') {
      result += '\'';
    }
    result += c;
  }
  return result;
}

class SQLBenchmark
    : public foresighter::implementation::basics::Benchmark<IOPairType> {
 public:
  SQLBenchmark(
      const IOPairType& ioPair, const std::string& benchmarkName,
      const std::string& solution, const std::vector<std::string>& hints,
      const std::vector<std::string>& benchmarkTablesSQL,
      const std::vector<std::string>& benchmarkTablesName,
      const std::vector<std::vector<std::string>>& benchmarkTablesColNames)
      : foresighter::implementation::basics::Benchmark<IOPairType>(
            {ioPair}, benchmarkName, solution, hints),
        _benchmarkTablesSQL(benchmarkTablesSQL),
        _benchmarkTablesName(benchmarkTablesName),
        _benchmarkTablesColNames(benchmarkTablesColNames) {}

  [[nodiscard]] const std::string& getBenchmarkTableSQL(size_t index) const {
    return _benchmarkTablesSQL[index];
  }

  [[nodiscard]] size_t getInputTableCount() const {
    return _benchmarkTablesSQL.size();
  }

  [[nodiscard]] const std::string& getBenchmarkTableName(size_t index) const {
    return _benchmarkTablesName[index];
  }

  [[nodiscard]] const std::vector<std::string>& getBenchmarkTableColNames(
      size_t index) const {
    return _benchmarkTablesColNames[index];
  }

 private:
  std::vector<std::string> _benchmarkTablesSQL;
  std::vector<std::string> _benchmarkTablesName;
  std::vector<std::vector<std::string>> _benchmarkTablesColNames;
};

using BenchmarkType = SQLBenchmark;

class BenchmarkReader {
 public:
  static BenchmarkType read(const std::string& path) {
    std::vector<ConcreteTable> input;
    std::vector<std::string> benchmarkTablesSQL;
    std::vector<std::string> benchmarkTablesName;
    std::vector<std::vector<std::string>> benchmarkTablesColNames;

    //Path is a json file that contains a benchmark.
    std::ifstream ifs(path);
    boost::json::value v = boost::json::parse(ifs);
    std::string benchmarkSource = std::string(v.at("source").as_string());
    std::string benchmarkName =
        benchmarkSource + "." + std::string(v.at("group").as_string());
    benchmarkName += "." + std::string(v.at("id").as_string());
    //input table
    for (const auto& inputTable : v.at("inputs").as_array()) {
      InputTable inputTableObj = readInputTableFromJsonEntry(inputTable);
      input.push_back(inputTableObj.getConcreteTable());
      benchmarkTablesSQL.push_back(readTableSQLFromJsonEntryWithRemapping(
          inputTable, inputTableObj.getColumnCanonicalOrderMapping()));
      benchmarkTablesName.push_back(readTableName(inputTable));
      benchmarkTablesColNames.push_back(readTableColNamesWithRemapping(
          inputTable, inputTableObj.getColumnCanonicalOrderMapping()));
    }
    //output table
    ConcreteTable output = readTableFromJsonEntry(v.at("output"));

    //read solution
    std::string solution = "N/A";
    if (v.as_object().if_contains("solution")) {
      solution = v.at("solution").as_string().c_str();
    }

    //read hints
    std::vector<std::string> hints;

    if (v.as_object().if_contains("hints")) {
      for (const auto& hint : v.at("hints").as_array()) {
        if (hint.at("value").is_string()) {
          hints.emplace_back(hint.at("value").as_string().c_str());
        } else if (hint.at("value").is_number()) {
          hints.emplace_back(std::to_string(hint.at("value").as_int64()));
        } else if (hint.at("value").is_null()) {
          continue;
        } else {
          throw std::runtime_error("Invalid hint type");
        }
      }
    }

    //assemble tables
    IOPairType ioPair(input, output);
    return BenchmarkType({ioPair}, benchmarkName, solution, hints,
                         benchmarkTablesSQL, benchmarkTablesName,
                         benchmarkTablesColNames);
  }

 private:
  static InputTable readInputTableFromJsonEntry(
      const boost::json::value& entry) {
    std::vector<ConcreteTable::ColumnDataType> colDataTypes;
    for (const auto& colName : entry.at("col_names").as_array()) {
      colDataTypes.emplace_back(colName.at("type").as_string().c_str());
    }
    return InputTable(readTableFromJsonEntry(entry).getTable(), colDataTypes);
  }

  static ConcreteTable readTableFromJsonEntry(const boost::json::value& entry) {
    std::vector<ConcreteTable::ColumnDataType> colDataTypes;
    for (const auto& colName : entry.at("col_names").as_array()) {
      colDataTypes.emplace_back(colName.at("type").as_string().c_str());
    }
    std::vector<std::vector<ConcreteTable::CellValue>> t;
    for (const auto& row : entry.at("table_content").as_array()) {
      std::vector<ConcreteTable::CellValue> r;
      for (int i = 0; i < row.as_array().size(); i++) {
        if (colDataTypes[i] == ConcreteTable::ColumnDataType::Type::NUMERIC) {
          r.emplace_back(row.as_array()[i].as_string().c_str(), true);
        } else {
          if (strcmp(row.as_array()[i].as_string().c_str(), "NULL") == 0) {
            // "NULL" is treated as nullptr when reading the benchmarks
            r.emplace_back(nullptr);
          } else {
            r.emplace_back(row.as_array()[i].as_string().c_str(), false);
          }
        }
      }
      t.push_back(r);
    }
    return ConcreteTable(t, colDataTypes);
  }

  static std::string readTableName(const boost::json::value& entry) {
    return entry.at("table_name").as_string().c_str();
  }

  static std::vector<std::string> readTableColNames(
      const boost::json::value& entry) {
    std::vector<std::string> colNames;
    for (const auto& colName : entry.at("col_names").as_array()) {
      colNames.emplace_back(colName.at("name").as_string().c_str());
    }
    return colNames;
  }

  static std::vector<std::string> readTableColNamesWithRemapping(
      const boost::json::value& entry,
      const std::vector<int>& columnCanonicalOrderMapping) {
    std::vector<std::string> colNames = readTableColNames(entry);
    std::vector<std::string> remappedColNames;
    for (const auto& colName : columnCanonicalOrderMapping) {
      remappedColNames.emplace_back(colNames[colName]);
    }
    return remappedColNames;
  }

  static std::string readTableSQLFromJsonEntry(
      const boost::json::value& entry) {
    std::vector<std::string> colNames;
    for (const auto& colName : entry.at("col_names").as_array()) {
      colNames.emplace_back(colName.at("name").as_string().c_str());
    }
    std::string tableName = entry.at("table_name").as_string().c_str();

    std::string sql = "SELECT";
    for (auto i = 0; i < colNames.size(); i++) {
      sql +=
          " \"" + normalizeString(colNames[i]) + "\" AS c" + std::to_string(i);
      if (i != colNames.size() - 1) {
        sql += ", ";
      }
    }
    sql += " FROM \"" + normalizeString(tableName) + "\"";
    return sql;
  }

  static std::string readTableSQLFromJsonEntryWithRemapping(
      const boost::json::value& entry,
      const std::vector<int>& columnCanonicalOrderMapping) {
    //wrap a remapping layer around the readTableSQLFromJsonEntry
    std::string sql = readTableSQLFromJsonEntry(entry);

    std::string remappedSQL = "SELECT";
    for (auto i = 0; i < columnCanonicalOrderMapping.size(); i++) {
      remappedSQL += " c" + std::to_string(columnCanonicalOrderMapping[i]) +
                     " AS c" + std::to_string(i);
      if (i != columnCanonicalOrderMapping.size() - 1) {
        remappedSQL += ", ";
      }
    }
    remappedSQL += " FROM (" + sql + ") AS T";
    return remappedSQL;
  }
};
}  // namespace foresighter::implementation::sql
