// usage: ./slicing_server_building_ablation
// --fta_input <path to fta input file>
// --grammar_input <path to grammar input file>
// --state_map_input <path to state map input file>
// --oracle_setting_input <path to oracle setting input file>
// --benchmark_list_input <path to benchmark list file>
// --sql_setting_input <path to sql setting input file>
// --slicing_server_output <folder to output cached slices>
// --log_output <path to log output file>

#include <algorithm>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options.hpp>
#include <filesystem>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/basics/fta.hpp>
#include <foresighter/implementation/basics/slicing_oracle.hpp>
#include <foresighter/implementation/basics/slicing_server.hpp>
#include <foresighter/implementation/sql/benchmark_reader.hpp>
#include <foresighter/implementation/sql/semantics/abstractors/concrete_to_offline_construction_abstractor.hpp>
#include <foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp>

#include <foresighter/implementation/sql/types.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include "utils/entries_util.hpp"

using namespace boost::program_options;

namespace {

struct CommandLineArguments {
  std::string ftaInputPath;
  std::string slicingOracleInputPath;
  std::string stateMapInputPath;
  std::string grammarInputPath;
  std::string oracleSettingInputPath;
  std::string sqlSettingInputPath;
  std::string benchmarkListInputPath;
  std::string slicingServerOutputPath;
  std::string logOutputPath;
};

CommandLineArguments setUpCommandLineArguments(int argc, char** argv) {
  CommandLineArguments args;
  options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "fta_input", value<std::string>(&args.ftaInputPath),
      "path to fta input file")(
      "slicing_oracle_input", value<std::string>(&args.slicingOracleInputPath),
      "path to slicing oracle input file")(
      "state_map_input", value<std::string>(&args.stateMapInputPath),
      "path to state map input file")(
      "oracle_setting_input", value<std::string>(&args.oracleSettingInputPath),
      "path to oracle setting input file")(
      "grammar_input", value<std::string>(&args.grammarInputPath),
      "path to grammar input file")(
      "benchmark_list_input", value<std::string>(&args.benchmarkListInputPath),
      "path to benchmark list file")(
      "sql_setting_input", value<std::string>(&args.sqlSettingInputPath),
      "path to sql setting input file")(
      "slicing_server_output",
      value<std::string>(&args.slicingServerOutputPath),
      "folder to output cached slices")("log_output",
                                        value<std::string>(&args.logOutputPath),
                                        "path to log output file");
  variables_map vm;
  store(parse_command_line(argc, argv, desc), vm);
  notify(vm);
  return args;
}

std::vector<foresighter::implementation::sql::SQLBenchmark>
getBenchmarksFromBenchmarkList(const std::string& benchmarkListPath,
                               const std::shared_ptr<spdlog::logger>& logger) {
  std::vector<foresighter::implementation::sql::SQLBenchmark> benchmarks;
  std::ifstream benchmarkSummaryFile(benchmarkListPath);
  std::string benchmarkPath;
  if (benchmarkSummaryFile.is_open()) {
    while (std::getline(benchmarkSummaryFile, benchmarkPath)) {
      try {
        SPDLOG_LOGGER_TRACE(logger, "Benchmark path: {}", benchmarkPath);
        auto benchmark =
            foresighter::implementation::sql::BenchmarkReader::read(
                benchmarkPath);
        SPDLOG_LOGGER_TRACE(logger, "Benchmark read finish.");
        SPDLOG_LOGGER_TRACE(logger, benchmark.getName());
        SPDLOG_LOGGER_TRACE(logger, benchmark.getIOPairs().size());
        benchmarks.push_back(benchmark);
      } catch (...) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to read benchmark file: {}",
                            benchmarkPath);
        throw;
      }
    }
  } else {
    SPDLOG_LOGGER_ERROR(logger, "Failed to open benchmark summary file.");
  }
  return benchmarks;
}

namespace {
using namespace foresighter::implementation::sql::semantics::values;
using namespace foresighter::implementation::sql;
typedef OfflineConstructionValue Abstract;
typedef OfflineConstructionValue::ConjunctionType Conjunction;
typedef OfflineConstructionValue::ConjunctionType::VariantType Predicate;

typedef emhash8hashmap::HashMap<int, std::vector<IDType>> StateMap;

std::vector<unsigned int> bitVec2Vector(
    const SlicingOracle::CombinationBitVec& bitvec) {
  std::vector<uint32_t> combines;
  combines.resize(bitvec.bitmap.cardinality());
  bitvec.bitmap.toUint32Array(combines.data());
  return combines;
}

int mergePair(unsigned int a, int b) {
  return int(a) * 100 + b;
}

StateMap buildStateMap(const OfflineFTA& fta,
                       const foresighter::implementation::sql::Grammar& grammar,
                       const SlicingOracle& oracle,
                       std::shared_ptr<spdlog::logger>& logger) {
  auto& state_manager = fta.getStateManager();

  auto max_cost = foresighter::implementation::sql::constants::Constants::
                      slicingOracleSetting.maxExactDistance;
  StateMap result;

  long long total_state_count = 0, total_record_count = 0;
  auto& start_symbols = grammar.getStartSymbols();
  for (auto& symbol : start_symbols) {
    for (auto final_state : fta.getStatesBySymbol(symbol)) {
      auto& state = state_manager.read(final_state);
      if (state.getCost() > max_cost)
        continue;
      auto bitmap = oracle.getCombination(final_state);
      auto combines = bitVec2Vector(bitmap);

      for (auto combine : combines) {
        auto pos = mergePair(combine, state.getCost());
        result[pos].push_back(final_state);
        total_record_count += 1;
      }

      total_state_count += 1;

      if (total_state_count % 100 == 0) {
        SPDLOG_LOGGER_TRACE(logger, "processing {} {}", total_state_count,
                            total_record_count);
      }
    }
  }

  return result;
}

struct ExtractInfo {
  int cost;
  std::vector<IDType> final_states;
  double time_cost;
  ExtractInfo() = default;

  ExtractInfo(int _cost, std::vector<IDType>&& _final_states, double _time_cost)
      : cost(_cost), final_states(_final_states), time_cost(_time_cost) {}
};

std::vector<ExtractInfo> extractRelatedFinalStates(
    const OfflineFTA& fta, const SQLBenchmark& benchmark,
    const StateMap& storage, const std::vector<unsigned int>& combines) {
  // overall pre-processing
  auto start = std::chrono::high_resolution_clock::now();
  auto& manager = fta.getStateManager();
  auto get_time_cost = [&]() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
  };

  int max_cost = foresighter::implementation::sql::constants::Constants::
                     slicingOracleSetting.maxExactDistance;
  auto abstractor = foresighter::implementation::sql::semantics::abstractors::
      ConcreteToOfflineConstructionAbstractor(benchmark);

  auto& example = benchmark.getIOPairs()[0];
  auto& inp = example.getInput();
  auto& oup = example.getOutput();
  int oup_row_num = oup.getNumRows();
  std::vector<int> inp_row_nums;
  inp_row_nums.reserve(inp.size());
  for (auto& inp_table : inp)
    inp_row_nums.push_back(inp_table.getNumRows());

  auto [abstract_oup, _] =
      semantics::transformers::OfflineConstructionTransformer::getOrderedValue(
          abstractor.abstract(ConcreteValue(oup)));

  auto init_time_cost = get_time_cost();

  std::vector<ExtractInfo> result(max_cost + 1);
  for (int i = 1; i <= max_cost; ++i) {
    start = std::chrono::high_resolution_clock::now();
    emhash8hashset::HashSet<IDType> final_states;

    for (auto comb : combines) {
      auto feature = mergePair(comb, i);
      auto it = storage.find(feature);
      if (it == storage.end())
        continue;
      for (auto& state_id : it->second)
        final_states.insert(state_id);
    }

    std::vector<IDType> valid_states;
    for (auto index : final_states) {
      auto val = manager.read(index).getValue();
      auto mapping = impliesUnderCanonicalOrderAndProjectionBool(
          abstract_oup, abstractor.removeRowNumRange(val));
      if (mapping &&
          abstractor.impliesRowNumRange(inp_row_nums, oup_row_num, val))
        valid_states.push_back(index);
    }
    std::sort(valid_states.begin(), valid_states.end());

    auto time_cost = get_time_cost();
    if (i == 1)
      time_cost += init_time_cost;
    result[i] = ExtractInfo(i, std::move(valid_states), time_cost);
  }
  return result;
}
}  // namespace

void buildSlicesUsingMultiThreading(auto& grammar, auto& slicingServer,
                                    auto& logger, auto& fta,
                                    auto& slicingOracle, auto& benchmarks,
                                    auto& args, auto& featureStorage) {
  using namespace foresighter::common;

  //get cpusPerTask using slurm's environment variables. If not set, use 1
  int cpusPerTask = 1;
  if (getenv("SLURM_CPUS_PER_TASK") != nullptr) {
    cpusPerTask = std::stoi(getenv("SLURM_CPUS_PER_TASK"));
  }

  boost::asio::thread_pool pool(cpusPerTask);
  int curIndex = 0;
  auto totalBenchmarkSize = benchmarks.size();
  int slurmTaskArrayId = 0;
  int slurmTaskArraySize = 1;
  if (getenv("SLURM_ARRAY_TASK_ID") != nullptr) {
    slurmTaskArrayId = std::stoi(getenv("SLURM_ARRAY_TASK_ID"));
  }
  if (getenv("SLURM_ARRAY_TASK_COUNT") != nullptr) {
    slurmTaskArraySize = std::stoi(getenv("SLURM_ARRAY_TASK_COUNT"));
  }

  //get the start and en index. for each worker, it will process the benchmarks in the following manner:
  //e.g. if there are [0-6] tasks, and there are 4 workers, then
  //worker 0 will process [0,1]
  //worker 1 will process [2,3]
  //worker 2 will process [4,5]
  //worker 3 will process [6]
  int chunkSize =
      (totalBenchmarkSize + slurmTaskArraySize - 1) / slurmTaskArraySize;
  int startIndex = slurmTaskArrayId * chunkSize;
  int endIndex = std::min(startIndex + chunkSize, (int)totalBenchmarkSize);

  SPDLOG_LOGGER_TRACE(logger, "Processing benchmarks from {} to {}", startIndex,
                      endIndex);

  for (int i = startIndex; i < endIndex; i++) {
    curIndex++;
    SQLBenchmark& benchmark = benchmarks[i];
    auto max_input = foresighter::implementation::sql::constants::Constants::
                         slicingOracleSetting.maxTableIdx;
    if (max_input < benchmark.getInputTableCount())
      continue;

    boost::asio::post(pool, [&grammar, &slicingServer, &logger, &fta,
                             &slicingOracle, &args, &benchmarks,
                             &featureStorage, curIndex, i, chunkSize]() {
      auto& benchmark = benchmarks[i];
      SPDLOG_LOGGER_TRACE(logger, "Processing benchmark: {}. {}/{}",
                          benchmark.getName(), curIndex, chunkSize);
      auto loggerForBenchmark = getConsoleAndFileSinkLogger(
          args.slicingServerOutputPath + "/" + benchmark.getName() + ".log",
          "slicing_server_building");

      foresighter::implementation::basics::VariableControlledSourceSet
          variableStates = getVariableStates<
              foresighter::implementation::sql::semantics::transformers::
                  ConcreteTransformer,
              foresighter::implementation::sql::semantics::abstractors::
                  ConcreteToOfflineConstructionAbstractor>(fta, benchmark,
                                                           grammar);
      auto slicingOracleConfig =
          SlicingOracle::buildLeafNodesConfigFromFTA(fta, logger);
      auto allowed_combination =
          SlicingOracle::getAllowedCombinationBitVecFromVarControlledSourceSet(
              variableStates, (int)benchmark.getHints().size(),
              slicingOracleConfig, logger);
      auto info_list = extractRelatedFinalStates(
          fta, benchmark, featureStorage, bitVec2Vector(allowed_combination));

      for (int cost = 1; cost < info_list.size(); ++cost) {
        auto& info = info_list[cost];
        std::vector<IDType> states(info.final_states.begin(),
                                   info.final_states.end());
        slicingServer.template buildSliceFromOutputs<
            foresighter::implementation::sql::semantics::transformers::
                ConcreteTransformer,
            foresighter::implementation::sql::semantics::abstractors::
                ConcreteToOfflineConstructionAbstractor>(
            benchmark, fta, slicingOracle, grammar, states, allowed_combination,
            info.cost, loggerForBenchmark, info.time_cost);
      }
    });
  }

  pool.join();
}

}  // namespace

using namespace foresighter::common;
using namespace foresighter::implementation;

int main(int argc, char** argv) {
  // auto args = setUpCommandLineArguments(argc, argv);

  auto args = setUpCommandLineArguments(argc, argv);

  auto logger = getConsoleAndFileSinkLogger(args.logOutputPath,
                                            "slicing_server_building");
  auto fta = loadFTA(args.ftaInputPath, logger);
  auto slicingOracle = SlicingOracleAblationDirectlyFromFTA();
  loadSlicingOracleSetting(args.oracleSettingInputPath, logger);
  auto benchmarks =
      getBenchmarksFromBenchmarkList(args.benchmarkListInputPath, logger);
  auto grammar = loadGrammar(args.grammarInputPath, logger);
  loadSQLSetting(args.sqlSettingInputPath, logger);
  basics::SlicingServer<decltype(fta)> slicingServer(
      args.slicingServerOutputPath);

  auto featureStorage = loadStateMap(args.stateMapInputPath, logger);
  buildSlicesUsingMultiThreading(grammar, slicingServer, logger, fta,
                                 slicingOracle, benchmarks, args,
                                 featureStorage);
}
