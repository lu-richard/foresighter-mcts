// usage: ./build_state_map_for_slicing
// --fta_input <path to fta input file>
// --slicing_oracle_input <path to slicing oracle input file>
// --oracle_setting_input <path to oracle setting input file>
// --grammar_input <path to grammar input file>
// --sql_setting_input <path to sql setting input file>
// --state_map_output <file to output state map>
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
  std::string grammarInputPath;
  std::string oracleSettingInputPath;
  std::string sqlSettingInputPath;
  std::string benchmarkListInputPath;
  std::string stateMapOutputPath;
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
      "oracle_setting_input", value<std::string>(&args.oracleSettingInputPath),
      "path to oracle setting input file")(
      "grammar_input", value<std::string>(&args.grammarInputPath),
      "path to grammar input file")(
      "sql_setting_input", value<std::string>(&args.sqlSettingInputPath),
      "path to sql setting input file")(
      "state_map_output", value<std::string>(&args.stateMapOutputPath),
      "file to output state map")("log_output",
                                  value<std::string>(&args.logOutputPath),
                                  "path to log output file");
  variables_map vm;
  store(parse_command_line(argc, argv, desc), vm);
  notify(vm);
  return args;
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

  auto start = std::chrono::high_resolution_clock::now();

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

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;
  SPDLOG_LOGGER_TRACE(logger, "|build-state-map-time-cost|{}",
                      duration.count());

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

}  // namespace

void buildSlicesUsingMultiThreading(auto& grammar, auto& slicingServer,
                                    auto& logger, auto& fta,
                                    auto& slicingOracle, auto& benchmarks,
                                    auto& args, const std::string& mode) {
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
  auto feature_storage = buildStateMap(fta, grammar, slicingOracle, logger);

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
                             &feature_storage, curIndex, i, chunkSize]() {
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
      auto allowed_combination =
          SlicingOracle::getAllowedCombinationBitVecFromVarControlledSourceSet(
              variableStates, (int)benchmark.getHints().size(),
              slicingOracle.config, logger);
      auto info_list = extractRelatedFinalStates(
          fta, benchmark, feature_storage, bitVec2Vector(allowed_combination));

      for (int cost = 1; cost < info_list.size(); ++cost) {
        auto& info = info_list[cost];
        std::vector<IDType> states(info.final_states.begin(),
                                   info.final_states.end());
        slicingServer.template buildSliceFromOutputs<
            foresighter::implementation::sql::semantics::transformers::
                ConcreteTransformer,
            foresighter::implementation::sql::semantics::abstractors::
                ConcreteToOfflineConstructionAbstractor>(
            benchmark, fta, slicingOracle, grammar, states, info.cost,
            loggerForBenchmark, info.time_cost);
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
  auto slicingOracle = loadSlicingOracle(args.slicingOracleInputPath, logger);
  loadSlicingOracleSetting(args.oracleSettingInputPath, logger);
  auto grammar = loadGrammar(args.grammarInputPath, logger);
  loadSQLSetting(args.sqlSettingInputPath, logger);
  auto feature_storage = buildStateMap(fta, grammar, slicingOracle, logger);

  //serialize state map using cereal
  saveStateMap(args.stateMapOutputPath, feature_storage, logger);
}
