// usage: ./online_search_with_slicing
// --grammar_input <path to grammar file>
// --slicing_input <path to slicing folder>
// --benchmark_list_input <path to benchmark list file>
// --sql_setting_input <path to sql setting file>
// --online_search_setting_input <path to online search setting file>
// --log_output_folder <path to log output folder>
// --cegis_fuzz_dir <path to cegis fuzz directory>
// --use_mcts (optional flag, default off) -- use the top-down MCTS search
//     (top_down_mcts_final_fixedroot.hpp) instead of the default exhaustive
//     bottom-up search (synthesize_with_slicing.hpp) for every benchmark in
//     this run.

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options.hpp>
#include <filesystem>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/algorithms/synthesize_with_slicing.hpp>
#include <foresighter/implementation/algorithms/top_down_mcts_final_fixedroot.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/basics/grammar.hpp>
#include <foresighter/implementation/sql/benchmark_reader.hpp>
#include <foresighter/implementation/sql/types.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include "foresighter/common/exceptions.hpp"
#include "foresighter/implementation/basics/slicing_oracle.hpp"
#include "foresighter/implementation/basics/slicing_server.hpp"
#include "foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp"
#include "utils/entries_util.hpp"
#include <filesystem>

using namespace boost::program_options;
using namespace foresighter::common;
using namespace foresighter::implementation;

namespace {

struct CommandLineArguments {
  std::string ftaInputPath;
  std::string slicingInputPath;
  std::string logOutputPath;
  std::string grammarInputPath;
  std::string benchmarkListPath;
  std::string sqlSettingInputPath;
  std::string onlineSearchSettingInputPath;
  std::string cegisFuzzDir;
  bool useMCTS = false;
};

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

CommandLineArguments setUpCommandLineArguments(int argc, char** argv) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()("grammar_input",
                     boost::program_options::value<std::string>(),
                     "path to grammar file")(
      "slicing_input", boost::program_options::value<std::string>(),
      "path to slicing folder")("benchmark_list_input",
                                boost::program_options::value<std::string>(),
                                "path to benchmark list file")(
      "sql_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to sql setting file")(
      "online_search_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to sql setting file")("log_output_folder",
                                  boost::program_options::value<std::string>(),
                                  "path to log folder")(
      "cegis_fuzz_dir",
      boost::program_options::value<std::string>()->required(),
      "path to cegis fuzz directory")(
      "use_mcts",
      boost::program_options::bool_switch()->default_value(false),
      "use the top-down MCTS search instead of the default exhaustive "
      "bottom-up search");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  if (vm.count("slicing_input") == 0 || vm.count("log_output_folder") == 0 ||
      vm.count("grammar_input") == 0 || vm.count("benchmark_list_input") == 0 ||
      vm.count("sql_setting_input") == 0 || vm.count("cegis_fuzz_dir") == 0) {
    std::cout << desc << std::endl;
    std::cout << "all arguments are required" << std::endl;
    exit(1);
  }

  CommandLineArguments args;
  args.slicingInputPath = vm["slicing_input"].as<std::string>();
  args.logOutputPath = vm["log_output_folder"].as<std::string>();
  args.grammarInputPath = vm["grammar_input"].as<std::string>();
  args.benchmarkListPath = vm["benchmark_list_input"].as<std::string>();
  args.sqlSettingInputPath = vm["sql_setting_input"].as<std::string>();
  args.onlineSearchSettingInputPath =
      vm["online_search_setting_input"].as<std::string>();
  args.cegisFuzzDir = vm["cegis_fuzz_dir"].as<std::string>();
  args.useMCTS = vm["use_mcts"].as<bool>();
  return args;
}

std::string replaceNewlineWithSpace(const std::string& str) {
  std::string result = str;
  std::replace(result.begin(), result.end(), '\n', ' ');
  return result;
}

template <class FTAType>
void synthesizeBenchmark(const sql::SQLBenchmark& benchmark,
                         const sql::Grammar& grammar,
                         const basics::SlicingServer<FTAType>& slicingOracle,
                         const std::string& resultLogOutputPath, int curIndex,
                         int totalNum, const std::string& cegisFuzzDir,
                         bool useMCTS) {

  std::cout << "Start to synthesize for benchmark " << benchmark.getName()
            << " (" << curIndex << "/" << totalNum << ")" << std::endl;

  std::string loggerOutputPath =
      resultLogOutputPath + "/" + benchmark.getName() + ".b.log";
  auto logger = getFileSinkLogger(loggerOutputPath, benchmark.getName());
  CEGISRunner cegisRunner(cegisFuzzDir, benchmark.getName(),
                          (int)foresighter::implementation::sql::constants::
                              Constants::onlineSearchSetting.numCEGISExamples);

  SPDLOG_LOGGER_TRACE(logger, "|ground-truth|{}",
                      replaceNewlineWithSpace(benchmark.getGroundTruth()));

  if (useMCTS) {
    algorithms::fixedroot_variant::MCTSSynthesizerFixedRoot mctsSynthesizer(logger);
    auto bestRewrite = mctsSynthesizer.runMCTS<
        sql::semantics::transformers::ConcreteTransformer>(
        benchmark, grammar, slicingOracle,
        foresighter::implementation::sql::constants::Constants::
            onlineSearchSetting.timeoutInMs,
        cegisRunner);
    SPDLOG_LOGGER_TRACE(logger, "|best-rewrite-found|{}", !bestRewrite.empty());
    return;
  }

  algorithms::Synthesizer synthesizer(logger);

  auto numResults = synthesizer.synthesizeWithSlicing<
      sql::semantics::transformers::ConcreteTransformer>(
      benchmark, grammar, benchmark.getHints(), slicingOracle,
      foresighter::implementation::sql::constants::Constants::
          onlineSearchSetting.timeoutInMs,
      cegisRunner);

  SPDLOG_LOGGER_TRACE(logger, "|num-of-results|{}", numResults);
}

void setUpSQLiteMultiThreading() {
  int returnVal = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
  if (returnVal != SQLITE_OK) {
    throw std::runtime_error("Failed to set sqlite to multi-threading mode");
  }
}

void synthesizeUsingMultiTreading(auto& commandLineArguments, auto& benchmarks,
                                  auto& grammar, auto& slicingServer,
                                  auto& logger,
                                  const std::string& cegisFuzzDir,
                                  bool useMCTS) {

  //use boost thread pool to test benchmarks
  int numThreads = (int)foresighter::implementation::sql::constants::Constants::
                       onlineSearchSetting.numThreads;

  //if slurm setting detected, then use slurm setting
  if (getenv("SLURM_CPUS_PER_TASK") != nullptr) {
    numThreads = std::stoi(getenv("SLURM_CPUS_PER_TASK"));
  }
  boost::asio::thread_pool pool(numThreads);

  int curIndex = 1;
  for (auto& benchmark : benchmarks) {
    boost::asio::post(pool, [&benchmark, &grammar, &slicingServer,
                             logPath = commandLineArguments.logOutputPath,
                             curIndex, totalNum = benchmarks.size(), &logger,
                             cegisFuzzDir, useMCTS]() {
      try {
        synthesizeBenchmark(benchmark, grammar, slicingServer, logPath,
                            curIndex, (int)totalNum, cegisFuzzDir, useMCTS);
      } catch (foresighter::common::InvalidArgument& e) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to synthesize benchmark " +
                                        benchmark.getName() + ": " + e.what());
      }
    });
    curIndex++;
  }

  pool.join();
}
}  // anonymous namespace

int main(int argc, char** argv) {
  try {
    // Initialize SQLite for multi-threading
    setUpSQLiteMultiThreading();

    // Parse command line arguments
    CommandLineArguments args = setUpCommandLineArguments(argc, argv);

    // Set up logging
    auto logger = getConsoleAndFileSinkLogger(
        args.logOutputPath + "/basics.log", "OnlineSearch");

    // Load required data and settings
    auto benchmarks =
        getBenchmarksFromBenchmarkList(args.benchmarkListPath, logger);
    auto grammar = loadGrammar(args.grammarInputPath, logger);
    auto slicingServer = SlicingServer<sql::OfflineFTA>(args.slicingInputPath);

    // Load configuration settings
    loadSQLSetting(args.sqlSettingInputPath, logger);
    loadOnlineSearchSetting(args.onlineSearchSettingInputPath, logger);

    // Run synthesis
    synthesizeUsingMultiTreading(args, benchmarks, grammar, slicingServer,
                                 logger, args.cegisFuzzDir, args.useMCTS);

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
