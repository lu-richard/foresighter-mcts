// Debug tool: dump a slice AFTA's states and transitions as readable text.
// Not part of the paper pipeline -- built for interactively inspecting what
// a per-benchmark slice actually looks like (states/transitions/final
// states), to compare against the CFTA baseline builds and the candidates
// MCTS explores.
//
// usage: ./dump_slice_fta
// --slicing_input <path to slicing folder, e.g. oracle_output_0_ten>
// --benchmark_name <e.g. spider.activity_1.0001>
// --cost <layer/cost index the slice was saved under, e.g. 1>
// --log_output <path to log file>

#include <boost/program_options.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/basics/slicing_server.hpp>
#include <foresighter/implementation/sql/types.hpp>
#include <iostream>

namespace {

struct Args {
  std::string slicingInputPath;
  std::string benchmarkName;
  int cost = 1;
  std::string logOutputPath;
};

Args parseArgs(int argc, char** argv) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()(
      "slicing_input", boost::program_options::value<std::string>(),
      "path to slicing folder")(
      "benchmark_name", boost::program_options::value<std::string>(),
      "benchmark name, e.g. spider.activity_1.0001")(
      "cost", boost::program_options::value<int>()->default_value(1),
      "cost/layer index the slice was saved under")(
      "log_output", boost::program_options::value<std::string>(),
      "path to log file");
  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);
  if (vm.count("slicing_input") == 0 || vm.count("benchmark_name") == 0 ||
      vm.count("log_output") == 0) {
    std::cout << desc << std::endl;
    exit(1);
  }
  Args args;
  args.slicingInputPath = vm["slicing_input"].as<std::string>();
  args.benchmarkName = vm["benchmark_name"].as<std::string>();
  args.cost = vm["cost"].as<int>();
  args.logOutputPath = vm["log_output"].as<std::string>();
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace foresighter::common;
  using namespace foresighter::implementation;

  Args args = parseArgs(argc, argv);
  auto logger = getConsoleAndFileSinkLogger(args.logOutputPath, "DumpSliceFTA");

  basics::SlicingServer<sql::OfflineFTA> server(args.slicingInputPath);
  auto slice = server.loadSlicing(args.benchmarkName, args.cost, logger);
  const auto& fta = slice.getFTA();

  std::cout << "=== Slice AFTA: " << args.benchmarkName
            << " (cost=" << args.cost << ") ===\n";
  std::cout << "States: " << fta.getStates().size() << "\n";
  std::cout << "Transitions: " << fta.getTransitions().size() << "\n";
  std::cout << "Final states: " << fta.getFinalStates().size() << "\n\n";

  std::cout << "--- States ---\n";
  for (auto stateID : fta.getStates()) {
    const auto& state = fta.getStateManager().read(stateID);
    bool isFinal = fta.isFinalState(stateID);
    std::cout << "  [" << stateID << "]" << (isFinal ? " (FINAL) " : " ")
              << state.toString() << "\n";
  }

  std::cout << "\n--- Transitions ---\n";
  for (auto transID : fta.getTransitions()) {
    const auto& trans = fta.getTransitionManager().read(transID);
    std::cout << "  [" << transID << "] " << trans.toString()
              << " head=" << trans.getHead() << "\n";
  }

  return 0;
}
