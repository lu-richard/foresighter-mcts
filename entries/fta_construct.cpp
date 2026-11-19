// An entry point for fta construction
// usage: ./fta_construct
// --grammar_input <path to grammar setting file>
// --sql_setting_input <path to sql setting file>
// --slicing_oracle_setting_input <path to slicing oracle setting file>
// --num_of_predicates <number of predicates>
// --log_output <path to log file>
// --fta_output <path to FTA output file>
// --grammar_output <path to grammar output file>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <foresighter/common/logger.hpp>
#include <foresighter/implementation/algorithms/offline_fta_construct.hpp>
#include <foresighter/implementation/basics/entity_manager.hpp>
#include <foresighter/implementation/sql/grammar_generator.hpp>
#include <foresighter/implementation/utils/fta_statistics.hpp>
#include <iostream>
#include <optional>
#include <string>
#include "foresighter/implementation/sql/constants.hpp"
#include "foresighter/implementation/sql/semantics/transformers/offline_construction_transformer.hpp"
#include "utils/entries_util.hpp"

namespace {

void setUpCommandlineArguments(int argc, char** argv,
                               std::string& grammarSettingPathIn,
                               std::string& sqlSettingPathIn,
                               std::string& logPathOut, std::string& ftaPathOut,
                               std::string& grammarOutputPath,
                               std::string& slicingOracleSettingPathIn,
                               int& numOfPredicates) {
  boost::program_options::options_description desc("Allowed options");
  desc.add_options()("grammar_input",
                     boost::program_options::value<std::string>()->required(),
                     "path to grammar setting file")(
      "sql_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to sql setting file")(
      "slicing_oracle_setting_input",
      boost::program_options::value<std::string>()->required(),
      "path to slicing oracle setting file")(
      "log_output", boost::program_options::value<std::string>()->required(),
      "path to log file")(
      "fta_output", boost::program_options::value<std::string>()->required(),
      "path to FTA output file")(
      "grammar_output",
      boost::program_options::value<std::string>()->required(),
      "path to grammar output file")(
      "num_of_predicates", boost::program_options::value<int>()->required(),
      "number of predicates");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  //all the arguments are required
  if (vm.count("grammar_input") == 0 || vm.count("log_output") == 0 ||
      vm.count("fta_output") == 0 || vm.count("grammar_output") == 0 ||
      vm.count("sql_setting_input") == 0 ||
      vm.count("slicing_oracle_setting_input") == 0 ||
      vm.count("num_of_predicates") == 0) {
    std::cout << desc << std::endl;
    exit(1);
  }

  grammarSettingPathIn = vm["grammar_input"].as<std::string>();
  logPathOut = vm["log_output"].as<std::string>();
  ftaPathOut = vm["fta_output"].as<std::string>();
  grammarOutputPath = vm["grammar_output"].as<std::string>();
  sqlSettingPathIn = vm["sql_setting_input"].as<std::string>();
  slicingOracleSettingPathIn =
      vm["slicing_oracle_setting_input"].as<std::string>();
  numOfPredicates = vm["num_of_predicates"].as<int>();
}

auto constructFTAAndLogTime(auto& grammar, auto& logger, int numOfPredicates) {
  auto start = std::chrono::high_resolution_clock::now();
  auto fta = foresighter::implementation::algorithms::constructFTA<
      foresighter::implementation::sql::semantics::transformers::
          OfflineConstructionTransformer>(
      grammar, std::nullopt, logger,
      constants::Constants::slicingOracleSetting.maxExactDistance,
      numOfPredicates);
  auto end = std::chrono::high_resolution_clock::now();
  SPDLOG_LOGGER_TRACE(
      logger, "|fta-construction-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  return fta;
}
}  // anonymous namespace

int main(int argc, char** argv) {
  using namespace foresighter::implementation::sql;
  using namespace foresighter::implementation;
  std::string grammarPath;
  std::string logOutputPath;
  std::string ftaOutputPath;
  std::string grammarOutputPath;
  std::string sqlSettingPath;
  std::string slicingOracleSettingPath;
  int numOfPredicates;
  setUpCommandlineArguments(argc, argv, grammarPath, sqlSettingPath,
                            logOutputPath, ftaOutputPath, grammarOutputPath,
                            slicingOracleSettingPath, numOfPredicates);

  auto logger = foresighter::common::getConsoleAndFileSinkLogger(
      logOutputPath, "FTAConstruct");
  loadSQLSetting(sqlSettingPath, logger);
  loadSlicingOracleSetting(slicingOracleSettingPath, logger);
  auto grammar = loadSettingAndGenerateGrammar(grammarPath, logger);

  auto fta = constructFTAAndLogTime(grammar, logger, numOfPredicates);

  //save FTA to file
  saveFTA(ftaOutputPath, fta, logger);
  saveGrammar(grammarOutputPath, grammar, logger);

  return 0;
}
