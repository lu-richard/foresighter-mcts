// usage: ./gather_slicing_size_info
// --slicing_input <path to slicing folder>
// --benchmark_list_input <path to benchmark list file>
// --log_output <path to log output file>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options.hpp>
#include <filesystem>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/algorithms/synthesize_with_slicing.hpp>
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

using namespace boost::program_options;
using namespace foresighter::common;
using namespace foresighter::implementation;

namespace {

struct CommandLineArguments {
  std::string slicingInputPath;
  std::string logOutputPath;
  std::string benchmarkListInputPath;
};

CommandLineArguments setUpCommandLineArguments(int argc, char** argv) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()("slicing_input",
                     boost::program_options::value<std::string>(),
                     "path to slicing folder")(
      "log_output", boost::program_options::value<std::string>(),
      "path to log output file")("benchmark_list_input",
                                 boost::program_options::value<std::string>(),
                                 "path to benchmark list file");
  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  if (vm.count("slicing_input") == 0 || vm.count("log_output") == 0 ||
      vm.count("benchmark_list_input") == 0) {
    std::cout << desc << std::endl;
    std::cout << "all arguments are required" << std::endl;
    exit(1);
  }

  CommandLineArguments args;
  args.slicingInputPath = vm["slicing_input"].as<std::string>();
  args.logOutputPath = vm["log_output"].as<std::string>();
  args.benchmarkListInputPath = vm["benchmark_list_input"].as<std::string>();
  return args;
}

using ProgNum = double;

ProgNum getProgramNum(const OfflineFTA& slice) {
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
  for (auto& state : slice.getFinalStates())
    result += collect(collect, state);
  return result;
}

void gatherSlicingSizeInfo(auto& benchmark, auto& logger, auto& slicingServer) {
  emhash8hashset::HashSet<IDType> statesUnion;
  emhash8hashset::HashSet<OfflineFTA::TransitionType> transitionsUnion;
  for (int i = 1; i <= 30; ++i) {
    auto slice = slicingServer.loadSlicing(benchmark.getName(), i, logger);
    for (auto state : slice.getFTA().getStates()) {
      statesUnion.insert(slice.getGlobalState(state));
    }
    for (auto transition : slice.getFTA().getTransitions()) {
      auto& transitionEntity =
          slice.getFTA().getTransitionManager().read(transition);
      IDType transitionHead = transitionEntity.getHead();
      std::vector<IDType> transitionTails = transitionEntity.getTails();
      IDType originalHead = slice.getGlobalState(transitionHead);
      std::vector<IDType> originalTails;
      for (auto tail : transitionTails) {
        originalTails.push_back(slice.getGlobalState(tail));
      }
      transitionsUnion.insert(OfflineFTA::TransitionType(
          originalHead, transitionEntity.getSymbol(), originalTails,
          transitionEntity.getCost(), transitionEntity.getOrder()));
    }
    SPDLOG_LOGGER_TRACE(logger, "|slicing-union-size|{}|{}|{}", i,
                        statesUnion.size(), transitionsUnion.size());
    SPDLOG_LOGGER_TRACE(logger, "|slicing-total-programs|{}|{}", i,
                        getProgramNum(slice.getFTA()));
  }
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
}  // anonymous namespace

void getSlicingUnionSizeForEachCost(auto& benchmarks, auto& logger,
                                    auto& args) {
  using namespace foresighter::common;

  auto slicingServer =
      basics::SlicingServer<sql::OfflineFTA>(args.slicingInputPath);

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
    boost::asio::post(pool, [&logger, &benchmarks, curIndex, i, chunkSize,
                             &args, &slicingServer]() {
      auto& benchmark = benchmarks[i];
      SPDLOG_LOGGER_TRACE(logger, "Processing benchmark: {}. {}/{}",
                          benchmark.getName(), curIndex, chunkSize);
      auto loggerForBenchmark = getConsoleAndFileSinkLogger(
          args.logOutputPath + ".folder" + "/" + benchmark.getName() + ".log",
          "slice_union");
      gatherSlicingSizeInfo(benchmark, loggerForBenchmark, slicingServer);
    });
  }

  pool.join();
}

int main(int argc, char** argv) {

  // Parse command line arguments
  CommandLineArguments args = setUpCommandLineArguments(argc, argv);

  // Set up logging
  auto logger =
      getConsoleAndFileSinkLogger(args.logOutputPath, "GatherSlicingSizeInfo");
  auto benchmarks =
      getBenchmarksFromBenchmarkList(args.benchmarkListInputPath, logger);
  getSlicingUnionSizeForEachCost(benchmarks, logger, args);

  return 0;
}
