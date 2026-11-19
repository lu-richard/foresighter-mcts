/**
* @file cegis_runner.hpp
* @brief a cegis runner that will be used to run cegis against solution
* @author Rui Dong
* @date 6/24/25
*/
#pragma once
#include <sqlite3.h>
#include <filesystem>
#include <foresighter/common/exceptions.hpp>
#include <string>
#include <vector>
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp"

namespace foresighter::implementation::sql {
class CEGISRunner {

 public:
  struct CEGISRunnerResult {
    int status;  // -1: success, -2: failed, other: the example id that failed
    std::string info;
  };

  CEGISRunner(const std::string& cegisDatabaseFolder,
              const std::string& benchmarkName, int numCEGISExamples) {
    loadBenchmarkSQLiteFilesFromFolder(cegisDatabaseFolder, benchmarkName,
                                       numCEGISExamples);
  }

  [[nodiscard]] CEGISRunnerResult run(const std::string& query) {
    using namespace foresighter::implementation::sql::semantics::transformers;
    try {
      for (int i = 0; i < _exampleDBs.size(); i++) {

        auto start = std::chrono::high_resolution_clock::now();
        sqlite3* inputDB = _exampleDBs[i];
        ConcreteTable* outputTable = _exampleOutputTables[i].get();
        //run the query on the inputDB
        ConcreteTable resultTable =
            sql_operations::executeSQLAndGetTable(query, inputDB);
        auto end = std::chrono::high_resolution_clock::now();
        _totalInterpreterTimeInNs +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();
        _totalInterpreterCalls++;
        //compare the resultTable with the outputTable
        if (resultTable != *outputTable) {
          return CEGISRunnerResult{i, ""};
        }
      }
      return CEGISRunnerResult{-1, ""};
    } catch (const std::exception& e) {
      return CEGISRunnerResult{-2,
                               "CEGISRunner failed: " + std::string(e.what())};
    }
    return CEGISRunnerResult{-3, "CEGISRunner failed: no database loaded"};
  }

  [[nodiscard]] size_t getTotalInterpreterTimeInNs() const {
    return _totalInterpreterTimeInNs;
  }

  [[nodiscard]] size_t getTotalInterpreterCalls() const {
    return _totalInterpreterCalls;
  }

 private:
  struct BenchmarkSQLiteFiles {
    std::vector<std::string> inputDBs;
    std::string outputDB;
  };

  struct BenchmarkName {
    std::string source;
    std::string group;
    std::string id;
  };

  [[nodiscard]] BenchmarkName parseBenchmarkName(
      const std::string& benchmarkName) {
    BenchmarkName benchmarkNameStruct;
    //benchmark is of the form: <benchmark_name>.<group_id>.<example_id>
    //we need to parse the benchmark name into source, group, and id
    size_t first_dot = benchmarkName.find(".");
    size_t second_dot = benchmarkName.find(".", first_dot + 1);
    benchmarkNameStruct.source = benchmarkName.substr(0, first_dot);
    benchmarkNameStruct.group =
        benchmarkName.substr(first_dot + 1, second_dot - first_dot - 1);
    benchmarkNameStruct.id = benchmarkName.substr(second_dot + 1);
    return benchmarkNameStruct;
  }

  [[nodiscard]] std::vector<std::string> getExampleDBsInFolder(
      const std::string& exampleDBFolder, int numCEGISExamples) {
    std::vector<std::string> exampleDBFiles;
    for (int i = 0; i < numCEGISExamples; i++) {
      //if exists, then add to the exampleDBFiles
      std::string exampleDBFile =
          exampleDBFolder + "/fuzz_" + std::to_string(i) + "_input.sqlite";
      if (std::filesystem::exists(exampleDBFile)) {
        exampleDBFiles.push_back(exampleDBFile);
      } else {
        throw foresighter::common::InvalidArgument(
            "Example DB file does not exist: " + exampleDBFile);
      }
    }
    return exampleDBFiles;
  }

  void loadBenchmarkSQLiteFilesFromFolder(const std::string& benchmarkFolder,
                                          const std::string& benchmarkName,
                                          int numCEGISExamples) {

    if (benchmarkFolder.empty() || benchmarkName.empty()) {
      throw foresighter::common::InvalidArgument(
          "Benchmark folder and name cannot be empty");
    }
    //see if benchmark folder exists
    if (!std::filesystem::exists(benchmarkFolder)) {
      throw foresighter::common::InvalidArgument(
          "Benchmark folder does not exist: " + benchmarkFolder);
    }

    BenchmarkName benchmarkNameStruct = parseBenchmarkName(benchmarkName);

    // the cegis input files are of the form:
    // <benchmarkFolder>/benchmark.source/benchmark.group/benchmark.id/fuzz_[fuzz_example_id].sqlite
    // we need to load all the fuzz_[fuzz_example_id].sqlite files into memory
    // and store them in _exampleDBs
    std::string exampleDBFolder =
        benchmarkFolder + "/" + benchmarkNameStruct.source + "/" +
        benchmarkNameStruct.group + "/" + benchmarkNameStruct.id;
    std::vector<std::string> exampleDBFiles =
        getExampleDBsInFolder(exampleDBFolder, numCEGISExamples);
    for (const auto& exampleDBFile : exampleDBFiles) {
      loadExampleDB(exampleDBFile);
    }
  }

  void loadExampleDB(const std::string& inputSQLiteFile) {
    sqlite3* inMemoryDB = nullptr;
    sqlite3* onDiskDB = nullptr;

    if (sqlite3_open(":memory:", &inMemoryDB) != SQLITE_OK) {
      throw foresighter::common::InvalidArgument(
          "Failed (load to memory failed) to initialize CEGIS input in "
          "memory: " +
          inputSQLiteFile);
    }
    if (sqlite3_open(inputSQLiteFile.c_str(), &onDiskDB) != SQLITE_OK) {
      throw foresighter::common::InvalidArgument(
          "Failed (load to memory failed) to load CEGIS input on disk: " +
          inputSQLiteFile);
    }

    sqlite3_backup* backup =
        sqlite3_backup_init(inMemoryDB, "main", onDiskDB, "main");
    if (backup == nullptr) {
      throw foresighter::common::InvalidArgument(
          "Failed (load to memory failed) to transfer CEGIS input from on disk "
          "to in memory: " +
          inputSQLiteFile);
    }

    // back up the onDiskDB to the inMemoryDB
    int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) {
      throw foresighter::common::InvalidArgument(
          "Failed (status not OK) to backup CEGIS input: " + inputSQLiteFile +
          ". Reason: " + std::string(sqlite3_errmsg(inMemoryDB)) + "|" +
          std::string(sqlite3_errmsg(onDiskDB)) +
          ", error code: " + std::to_string(rc));
    }

    _exampleDBs.push_back(inMemoryDB);
    sqlite3_close(onDiskDB);

    //load the output table from the exampleDB
    using namespace foresighter::implementation::sql::semantics::transformers;
    ConcreteTable outputTable = sql_operations::executeSQLAndGetTable(
        "SELECT * FROM __output", inMemoryDB);
    _exampleOutputTables.push_back(
        std::make_unique<ConcreteTable>(outputTable));
  };

  std::vector<sqlite3*> _exampleDBs{};
  std::vector<std::unique_ptr<ConcreteTable>> _exampleOutputTables{};
  size_t _totalInterpreterTimeInNs = 0;
  size_t _totalInterpreterCalls = 0;
};

}  // namespace foresighter::implementation::sql
