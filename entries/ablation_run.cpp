// An entry point for the no-presynthesis ablation (RQ4)
// usage: ./ablation_run
// --grammar_input <path to grammar setting file>
// --sql_setting_input <path to sql setting file>
// --slicing_oracle_setting_input <path to slicing oracle setting file>
// --online_setting_input <path to online setting file>
// --log_output <path to overall log file>
// --output <path to output folder>
// --grammar_output <path to grammar output file>
// --benchmark_list <benchmark list file>
// --cegis_fuzz_dir <path to cegis fuzz directory>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/algorithms/offline_fta_construct.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/basics/slicing_server.hpp>
#include <foresighter/implementation/sql/grammar_generator.hpp>
#include <foresighter/implementation/utils/fta_statistics.hpp>
#include <iostream>
#include <string>
#include <utility>
#include "foresighter/implementation/algorithms/synthesize_with_slicing.hpp"
#include "foresighter/implementation/sql/benchmark_reader.hpp"
#include "foresighter/implementation/sql/cegis_runner.hpp"
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/semantics/abstractors/concrete_to_offline_construction_abstractor.hpp"
#include "foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp"
#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"
#include "foresighter/implementation/sql/semantics/values/concrete_value.hpp"
#include "utils/entries_util.hpp"

namespace {

void setUpCommandlineArguments(
    int argc, char** argv, std::string& grammarSettingPathIn,
    std::string& sqlSettingPathIn, std::string& onlineSettingPathIn,
    std::string& logPathOut, std::string& ftaPathOut,
    std::string& grammarOutputPath, std::string& slicingOracleSettingPathIn,
    std::string& benchmarkList, std::string& cegisFuzzDir) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()("grammar_input",
                     boost::program_options::value<std::string>()->required(),
                     "path to grammar setting file")(
      "sql_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to sql setting file")(
      "online_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to online setting file")(
      "slicing_oracle_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to slicing oracle setting file")(
      "log_output", boost::program_options::value<std::string>()->required(),
      "path to log file")(
      "output", boost::program_options::value<std::string>()->required(),
      "path to FTA output file")(
      "grammar_output",
      boost::program_options::value<std::string>()->required(),
      "path to grammar output file")(
      "benchmark_list",
      boost::program_options::value<std::string>()->required(),
      "benchmark list file")(
      "cegis_fuzz_dir",
      boost::program_options::value<std::string>()->required(),
      "path to cegis fuzz directory");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  //all the arguments are required
  if (vm.count("grammar_input") == 0 || vm.count("log_output") == 0 ||
      vm.count("output") == 0 || vm.count("grammar_output") == 0 ||
      vm.count("sql_setting_input") == 0 ||
      vm.count("online_setting_input") == 0 ||
      vm.count("slicing_oracle_setting_input") == 0 ||
      vm.count("benchmark_list") == 0 || vm.count("cegis_fuzz_dir") == 0) {
    std::cout << desc << std::endl;
    exit(1);
  }

  grammarSettingPathIn = vm["grammar_input"].as<std::string>();
  logPathOut = vm["log_output"].as<std::string>();
  ftaPathOut = vm["output"].as<std::string>();
  grammarOutputPath = vm["grammar_output"].as<std::string>();
  sqlSettingPathIn = vm["sql_setting_input"].as<std::string>();
  onlineSettingPathIn = vm["online_setting_input"].as<std::string>();
  slicingOracleSettingPathIn =
      vm["slicing_oracle_setting_input"].as<std::string>();
  benchmarkList = vm["benchmark_list"].as<std::string>();
  cegisFuzzDir = vm["cegis_fuzz_dir"].as<std::string>();
}

auto constructFTAAndLogTime(auto& grammar, auto& benchmark, auto& logger,
                            auto& h, auto& numOfPredicates) {
  SPDLOG_LOGGER_TRACE(logger, "FTA construction on cost = {}", h);
  auto start = std::chrono::high_resolution_clock::now();
  auto fta = foresighter::implementation::algorithms::constructFTA<
      foresighter::implementation::sql::semantics::transformers::
          OfflineConstructionTransformer>(
      grammar, std::make_optional<SQLBenchmark>(benchmark), logger, h,
      numOfPredicates);
  auto end = std::chrono::high_resolution_clock::now();
  SPDLOG_LOGGER_TRACE(
      logger, "|fta-construction-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  return fta;
}

}  // anonymous namespace

std::vector<foresighter::implementation::sql::SQLBenchmark>
getBenchmarksFromBenchmarkList(const std::string& benchmarkListPath,
                               const std::shared_ptr<spdlog::logger>& logger) {
  std::vector<std::string> benchmarkPaths;
  std::ifstream benchmarkSummaryFile(benchmarkListPath);
  std::string benchmarkPath;
  if (benchmarkSummaryFile.is_open()) {
    while (std::getline(benchmarkSummaryFile, benchmarkPath)) {
      benchmarkPaths.push_back(benchmarkPath);
    }
  } else {
    SPDLOG_LOGGER_ERROR(logger, "Failed to open benchmark summary file.");
  }

  //divide the workload based on current job id and total number of jobs
  //those info are get from slurm array settings

  int slurmTaskArrayId = 0;
  int slurmTaskArraySize = 1;
  if (getenv("SLURM_ARRAY_TASK_ID") != nullptr) {
    slurmTaskArrayId = std::stoi(getenv("SLURM_ARRAY_TASK_ID"));
  }
  if (getenv("SLURM_ARRAY_TASK_COUNT") != nullptr) {
    slurmTaskArraySize = std::stoi(getenv("SLURM_ARRAY_TASK_COUNT"));
  }

  int totalBenchmarkSize = (int)benchmarkPaths.size();

  int chunkSize =
      (totalBenchmarkSize + slurmTaskArraySize - 1) / slurmTaskArraySize;
  int startIndex = slurmTaskArrayId * chunkSize;
  int endIndex = std::min(startIndex + chunkSize, (int)totalBenchmarkSize);

  SPDLOG_LOGGER_TRACE(logger, "Processing benchmarks from {} to {} (total {})",
                      startIndex, endIndex, totalBenchmarkSize);

  std::vector<foresighter::implementation::sql::SQLBenchmark> benchmarks;

  for (int i = startIndex; i < endIndex; i++) {
    auto benchmarkPath = benchmarkPaths[i];
    try {
      SPDLOG_LOGGER_TRACE(logger, "Benchmark path: {}", benchmarkPath);
      auto benchmark = foresighter::implementation::sql::BenchmarkReader::read(
          benchmarkPath);
      SPDLOG_LOGGER_TRACE(logger, "Benchmark read finish.");
      SPDLOG_LOGGER_TRACE(logger, benchmark.getName());
      SPDLOG_LOGGER_TRACE(logger, benchmark.getIOPairs().size());
      benchmarks.push_back(benchmark);
    } catch (...) {
      SPDLOG_LOGGER_ERROR(logger, "Failed to read benchmark file: {}",
                          benchmarkPath);
    }
  }

  return benchmarks;
}

template <class FTAType, class BenchmarkType, class GrammarType>
std::pair<std::vector<IDType>, std::vector<IDType>> getSlicing(
    FTAType& fta, const BenchmarkType& benchmark, const GrammarType& grammar,
    int h, const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "Start getting slicing for cost = {}", h);
  auto outputStates =
      getOutputStates<foresighter::implementation::sql::semantics::
                          transformers::ConcreteTransformer,
                      foresighter::implementation::sql::semantics::abstractors::
                          ConcreteToOfflineConstructionAbstractor>(
          fta, benchmark, grammar, logger);
  SPDLOG_LOGGER_TRACE(logger, "Output states (any cost) size = {}",
                      outputStates.size());
  std::set<IDType> states, transitions;
  std::queue<IDType> q;
  for (auto& state : outputStates) {
    if (fta.getStateManager().read(state).getCost() == h) {
      SPDLOG_LOGGER_TRACE(
          logger, "Real output states with value = {}",
          fta.getStateManager().read(state).getValue().toString());
      states.insert(state);
      q.push(state);
      fta.markFinalState(state);
    }
  }
  while (!q.empty()) {
    auto state = q.front();
    q.pop();
    for (auto transition : fta.getTransitionsByHead(state)) {
      transitions.insert(transition);
      auto& transitionEntity = fta.getTransitionManager().read(transition);
      for (auto tail : transitionEntity.getTails()) {
        if (states.find(tail) == states.end()) {
          states.insert(tail);
          q.push(tail);
        }
      }
    }
  }
  SPDLOG_LOGGER_TRACE(logger,
                      "Got slice with (#states, #transitions) = ({}, {})",
                      states.size(), transitions.size());

  return {{states.begin(), states.end()},
          {transitions.begin(), transitions.end()}};
}

template <class FTAType>
Slice<FTAType> buildIdenticalMappingSlice(FTAType&& fta) {
  return Slice<FTAType>(std::forward<FTAType>(fta));
}

template <class FTAType, class BenchmarkType, class GrammarType>
int concretize(
    const std::pair<std::vector<IDType>, std::vector<IDType>>& slicing,
    FTAType&& fta, const BenchmarkType& benchmark, const GrammarType& grammar,
    int h, const std::shared_ptr<spdlog::logger>& logger, auto restTime,
    foresighter::implementation::sql::CEGISRunner& cegisRunner,
    auto& synthesizer, auto& interpreter, auto& abstractor) {

  SPDLOG_LOGGER_TRACE(logger, "|concretization-start|{}", h);

  auto slice = buildIdenticalMappingSlice(std::forward<FTAType>(fta));

  //some data structures needed for incremental concretization
  emhash8hashmap::HashMap<IDType, IDType> previouslyMappedCFTAState;
  foresighter::implementation::basics::FTA<
      typename GrammarType::SymbolType,
      foresighter::implementation::sql::semantics::values::ConcreteValue>
      cfta;
  emhash8hashmap::HashMap<IDType, emhash8hashset::HashSet<IDType>>
      mappingAbstractStateToConcreteStates;

  auto programSatisfyingIOExample =
      synthesizer.template findProgramSatisfyingIOExampleFromSlicing<
          foresighter::implementation::sql::semantics::abstractors::
              ConcreteToOfflineConstructionAbstractor::Lattice>(
          slicing, slice, benchmark, grammar, interpreter, h,
          benchmark.getHints(), restTime, cegisRunner,
          previouslyMappedCFTAState, cfta, mappingAbstractStateToConcreteStates,
          abstractor);
  //record interpreter call times
  SPDLOG_LOGGER_TRACE(logger, "|interpreter-calls-layer|{}|{}", h,
                      interpreter.getNumInterpreterCalls());
  SPDLOG_LOGGER_TRACE(logger, "|interpreter-queries-evaluated-layer|{}|{}", h,
                      interpreter.getNumQueriesEvaluated());
  SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-created-layer|{}|{}", h,
                      interpreter.getNumTablesCreated());
  SPDLOG_LOGGER_TRACE(logger, "|interpreter-tables-in-cache-layer|{}|{}", h,
                      interpreter.getNumTablesInCache());
  SPDLOG_LOGGER_TRACE(
      logger, "|interpreter-tables-not-created-due-to-cache-layer|{}|{}", h,
      interpreter.getNumTablesNotCreatedDueToCache());
  logger->flush();
  if (!programSatisfyingIOExample.empty()) {
    SPDLOG_LOGGER_TRACE(logger, "|program-found-num|{}|{}", h,
                        programSatisfyingIOExample.size());
  }
  return programSatisfyingIOExample.size();
}

using ProgNum = double;

ProgNum getProgramNumWithCertainCostAsFinalState(const OfflineFTA& slice,
                                                 int cost) {
  std::unordered_map<IDType, ProgNum> cache;
  auto& state_manager = slice.getStateManager();
  auto& trans_manager = slice.getTransitionManager();

  auto collect = [&](auto&& self, IDType index) -> ProgNum {
    auto it = cache.find(index);
    if (it != cache.end())
      return it->second;

    auto& state = state_manager.read(index);
    if (!state.getSymbol().isNonTerminal()) {
      return cache[index] = 1.0;
    }

    ProgNum result = 0.0;
    for (auto& trans : slice.getTransitionsByHead(index)) {
      ProgNum trans_num = 1.0;
      for (auto& sub_state : trans_manager.read(trans).getTails()) {
        trans_num *= self(self, sub_state);
      }
      result += trans_num;
    }
    return cache[index] = result;
  };

  ProgNum result = 0.0;
  for (auto& state : slice.getStates()) {
    if (state_manager.read(state).getCost() == cost and
        std::holds_alternative<foresighter::implementation::sql::NtQuery>(
            state_manager.read(state).getSymbol().getSymbol())) {
      result += collect(collect, state);
    }
  }
  return result;
}

void testSingleBenchmark(const auto& benchmark, const auto& grammar,
                         const auto maxDistance, const auto& outputPath,
                         const auto& cegisFuzzDir) {
  auto logger = foresighter::common::getConsoleAndFileSinkLogger(
      outputPath + "/" + benchmark.getName() + "/experiment.log",
      "ablation_experiment");
  CEGISRunner cegisRunner(cegisFuzzDir, benchmark.getName(),
                          foresighter::implementation::sql::constants::
                              Constants::onlineSearchSetting.numCEGISExamples);
  for (int numOfPredicates = 4; numOfPredicates >= 0; --numOfPredicates) {
    SPDLOG_LOGGER_TRACE(logger, "Number of predicates = {}:", numOfPredicates);
    long long restTime =
        (int)constants::Constants::onlineSearchSetting.timeoutInMs;
    bool solved = false;
    bool timeout = false;
    auto loggerForFTA = foresighter::common::getFileSinkLogger(
        outputPath + "/" + benchmark.getName() + "/" +
            std::to_string(numOfPredicates) + "/fta.log",
        "fta_construction");
    auto loggerForSearch = foresighter::common::getFileSinkLogger(
        outputPath + "/" + benchmark.getName() + "/" +
            std::to_string(numOfPredicates) + "/concretization.log",
        "concretization");
    auto loggerForSlicing = foresighter::common::getFileSinkLogger(
        outputPath + "/" + benchmark.getName() + "/" +
            std::to_string(numOfPredicates) + "/slicing.log",
        "get_slicing");

    foresighter::implementation::sql::semantics::transformers::
        ConcreteTransformer interpreter(benchmark);
    foresighter::implementation::algorithms::Synthesizer synthesizer(
        loggerForSearch);
    synthesizer.setSynthesisStart();
    auto abstractor = foresighter::implementation::sql::semantics::abstractors::
        ConcreteToOfflineConstructionAbstractor(benchmark);
    for (int curMaxDistance = 1; curMaxDistance <= maxDistance;
         ++curMaxDistance) {
      if (restTime <= 0) {
        SPDLOG_LOGGER_TRACE(logger, "Unsolved-Timeout");
        timeout = true;
        break;
      }
      SPDLOG_LOGGER_TRACE(logger, "Max distance = {}:", curMaxDistance);
      // step 1: FTA construction
      auto timeoutStart = std::chrono::high_resolution_clock::now();
      auto fta = constructFTAAndLogTime(grammar, benchmark, loggerForFTA,
                                        curMaxDistance, numOfPredicates);

      auto timeoutEnd = std::chrono::high_resolution_clock::now();
      auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         timeoutEnd - timeoutStart)
                         .count();
      SPDLOG_LOGGER_TRACE(logger, "Step 1: {}ms", time_ns / 1000000);

      SPDLOG_LOGGER_TRACE(logger, "|RQ4-nopresyn-bottom-up-size|{}|{}|{}",
                          curMaxDistance, fta.getStates().size(),
                          fta.getTransitions().size());
      SPDLOG_LOGGER_TRACE(
          logger, "|RQ4-bottom-up-program-size|{}|{}", curMaxDistance,
          getProgramNumWithCertainCostAsFinalState(fta, curMaxDistance));

      // step 2: get slicing (refinement)
      timeoutStart = std::chrono::high_resolution_clock::now();
      auto slice = getSlicing(fta, benchmark, grammar, (int)curMaxDistance,
                              loggerForSlicing);
      timeoutEnd = std::chrono::high_resolution_clock::now();
      time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    timeoutEnd - timeoutStart)
                    .count();
      SPDLOG_LOGGER_TRACE(logger, "Step 2: {}ms", time_ns / 1000000);
      restTime -= time_ns / 1000000;

      // step 3: concretization (commented out for RQ4 ablation - only need FTA + slicing times)
    }
    if (!solved && !timeout) {
      SPDLOG_LOGGER_TRACE(logger, "Unsolved-Finished");
    }
  }
}

int main(int argc, char** argv) {
  using namespace foresighter::implementation::sql;
  using namespace foresighter::implementation;
  std::string grammarPath;
  std::string logOutputPath;
  std::string outputPath;
  std::string grammarOutputPath;
  std::string sqlSettingPath;
  std::string onlineSettingPath;
  std::string slicingOracleSettingPath;
  std::string benchmarkList;
  std::string cegisFuzzDir;
  setUpCommandlineArguments(argc, argv, grammarPath, sqlSettingPath,
                            onlineSettingPath, logOutputPath, outputPath,
                            grammarOutputPath, slicingOracleSettingPath,
                            benchmarkList, cegisFuzzDir);

  auto logger = foresighter::common::getConsoleAndFileSinkLogger(
      logOutputPath, "ablation_run");
  loadSQLSetting(sqlSettingPath, logger);
  loadSlicingOracleSetting(slicingOracleSettingPath, logger);
  loadOnlineSearchSetting(onlineSettingPath, logger);
  auto grammar = loadSettingAndGenerateGrammar(grammarPath, logger);

  auto benchmarks = getBenchmarksFromBenchmarkList(benchmarkList, logger);
  auto totalBenchmarkSize = benchmarks.size();

  auto& curMaxDistance = foresighter::implementation::sql::constants::
                             Constants::slicingOracleSetting.maxExactDistance;
  auto maxDistance = curMaxDistance;

  //use boost thread pool to test benchmarks
  int numThreads = (int)foresighter::implementation::sql::constants::Constants::
                       onlineSearchSetting.numThreads;

  //if slurm setting detected, then use slurm setting
  if (getenv("SLURM_CPUS_PER_TASK") != nullptr) {
    numThreads = std::stoi(getenv("SLURM_CPUS_PER_TASK"));
  }

  boost::asio::thread_pool pool(numThreads);

  SPDLOG_LOGGER_TRACE(logger, "|num-threads|{}", numThreads);

  for (int i = 0; i < benchmarks.size(); ++i) {
    auto benchmark = benchmarks[i];
    SPDLOG_LOGGER_TRACE(logger, "Processing benchmark: {}. {}/{}",
                        benchmark.getName(), i + 1, benchmarks.size());
    boost::asio::post(
        pool, [benchmark, grammar, maxDistance, outputPath, cegisFuzzDir]() {
          testSingleBenchmark(benchmark, grammar, maxDistance, outputPath,
                              cegisFuzzDir);
        });
  }
  pool.join();
  SPDLOG_LOGGER_TRACE(logger, "All benchmarks processed.");
  return 0;
}
