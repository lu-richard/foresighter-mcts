// An entry point for slicing oracle construction
// usage: ./slicing_oracle_construct
// --fta_input <path to fta input file>
// --grammar_input <path to grammar input file>
// --slicing_oracle_setting_input <path to slicing oracle setting file>
// --log_output <path to log file>
// --slicing_oracle_output <path to slicing oracle output file>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/sql/types.hpp>
#include <iostream>

#include <fstream>

#include <cereal/archives/binary.hpp>
#include "foresighter/implementation/basics/slicing_oracle.hpp"
#include "spdlog/sinks/stdout_sinks.h"

#include "utils/entries_util.hpp"

namespace {

void setUpCommandLineArguments(int argc, char** argv, std::string& ftaPathIn,
                               std::string& grammarPathIn,
                               std::string& slicingOracleSettingPathIn,
                               std::string& logPathOut,
                               std::string& slicingOracleOut) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()("fta_input", boost::program_options::value<std::string>(),
                     "path to fta input file")(
      "grammar_input", boost::program_options::value<std::string>(),
      "path to grammar input file")(
      "slicing_oracle_setting_input",
      boost::program_options::value<std::string>(),
      "path to slicing oracle setting file")(
      "log_output", boost::program_options::value<std::string>(),
      "path to log file")("slicing_oracle_output",
                          boost::program_options::value<std::string>(),
                          "path to slicing oracle output file");
  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  //all the arguments are required
  if (vm.count("fta_input") == 0 || vm.count("grammar_input") == 0 ||
      vm.count("slicing_oracle_setting_input") == 0 ||
      vm.count("log_output") == 0 || vm.count("slicing_oracle_output") == 0) {
    std::cout << desc << std::endl;
    std::cout << "all arguments are required" << std::endl;
    exit(1);
  }

  ftaPathIn = vm["fta_input"].as<std::string>();
  slicingOracleSettingPathIn =
      vm["slicing_oracle_setting_input"].as<std::string>();
  logPathOut = vm["log_output"].as<std::string>();
  slicingOracleOut = vm["slicing_oracle_output"].as<std::string>();
  grammarPathIn = vm["grammar_input"].as<std::string>();
}

auto buildSlicingOralce(auto& grammar, auto& fta, auto& logger) {

  SPDLOG_LOGGER_TRACE(logger, "Start to build slicing oracle");

  auto start = std::chrono::high_resolution_clock::now();
  auto slicingOralce =
      foresighter::implementation::basics::SlicingOracle::buildFromFTA(fta,
                                                                       logger);
  auto end = std::chrono::high_resolution_clock::now();

  SPDLOG_LOGGER_TRACE(
      logger, "|slicing-oracle-construct-time|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  return slicingOralce;
}

}  // anonymous namespace

int main(int argc, char** argv) {
  using namespace foresighter::common;
  using namespace foresighter::implementation;

  std::string ftaPathIn;
  std::string grammarPathIn;
  std::string slicingOracleSettingPathIn;
  std::string logPathOut;
  std::string slicingOracleOut;

  setUpCommandLineArguments(argc, argv, ftaPathIn, grammarPathIn,
                            slicingOracleSettingPathIn, logPathOut,
                            slicingOracleOut);

  auto logger =
      getConsoleAndFileSinkLogger(logPathOut, "SlicingOracleConstruct");

  sql::OfflineFTA fta = loadFTA(ftaPathIn, logger);
  sql::Grammar grammar = loadGrammar(grammarPathIn, logger);
  loadSlicingOracleSetting(slicingOracleSettingPathIn, logger);

  auto slicingOracle = buildSlicingOralce(grammar, fta, logger);
  saveSlicingOracle(slicingOracleOut, slicingOracle, logger);
}
