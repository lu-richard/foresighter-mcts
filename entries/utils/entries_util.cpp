#include "entries_util.hpp"

#include <filesystem>
#include <foresighter/implementation/sql/constants.hpp>
#include <foresighter/implementation/sql/grammar_generator.hpp>

#include <cereal/archives/binary.hpp>
#include <variant>
#include "foresighter/implementation/sql/symbol/var_filter_const.hpp"
#include "foresighter/implementation/sql/symbol/var_table.hpp"

void saveFTA(const std::string& filepath,
             const foresighter::implementation::sql::OfflineFTA& fta,
             const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|fta-serialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  std::ofstream ofs(filepath, std::ios::binary);
  {
    cereal::BinaryOutputArchive archive(ofs);
    archive(fta);
  }
  ofs.close();
  auto end = std::chrono::high_resolution_clock::now();
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|fta-serialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print FTA file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|fta-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  //print FTA statistics
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-states|{}",
                      fta.getStates().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-transitions|{}",
                      fta.getTransitions().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-symbols|{}",
                      fta.getSymbols().size());
}

foresighter::implementation::sql::OfflineFTA loadFTA(
    const std::string& filepath,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|fta-deserialization-start|");
  foresighter::implementation::sql::OfflineFTA emptyFTA;
  auto start = std::chrono::high_resolution_clock::now();
  std::ifstream ifs(filepath, std::ios::binary);
  {
    cereal::BinaryInputArchive archive(ifs);
    archive(emptyFTA);
  }
  ifs.close();
  auto end = std::chrono::high_resolution_clock::now();
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|fta-deserialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print FTA file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|fta-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  //print FTA statistics
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-states|{}",
                      emptyFTA.getStates().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-transitions|{}",
                      emptyFTA.getTransitions().size());
  SPDLOG_LOGGER_TRACE(logger, "|fta-number-of-symbols|{}",
                      emptyFTA.getSymbols().size());
  return emptyFTA;
}

uint32_t getMaxTableIdFromGrammar(
    const foresighter::implementation::sql::Grammar& grammar) {
  uint32_t maxTableId = 0;
  for (const auto& symbol : grammar.getVariables()) {
    if (std::holds_alternative<foresighter::implementation::sql::VarTable>(
            symbol.getSymbol())) {
      maxTableId = std::max(
          maxTableId, std::get<foresighter::implementation::sql::VarTable>(
                          symbol.getSymbol())
                          .getIndex());
    }
  }
  return maxTableId + 1;
}

uint32_t getMaxSymbolicConstantNumFromGrammar(
    const foresighter::implementation::sql::Grammar& grammar) {
  uint32_t maxSymbolicConstantNum = 0;
  for (const auto& symbol : grammar.getVariables()) {
    if (std::holds_alternative<
            foresighter::implementation::sql::VarFilterConst>(
            symbol.getSymbol())) {
      maxSymbolicConstantNum =
          std::max(maxSymbolicConstantNum,
                   std::get<foresighter::implementation::sql::VarFilterConst>(
                       symbol.getSymbol())
                       .getIndex());
    }
  }
  return maxSymbolicConstantNum + 1;
}

void loadSlicingOracleSettingFromGrammar(
    const foresighter::implementation::sql::Grammar& grammar,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-setting-from-grammar-start|");
  foresighter::implementation::sql::constants::Constants::slicingOracleSetting
      .maxTableIdx = getMaxTableIdFromGrammar(grammar);
  foresighter::implementation::sql::constants::Constants::slicingOracleSetting
      .maxSymbolicConstantNum = getMaxSymbolicConstantNumFromGrammar(grammar);
}

foresighter::implementation::sql::Grammar loadGrammar(
    const std::string& filepath,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|grammar-deserialization-start|");
  foresighter::implementation::sql::Grammar emptyGrammar;
  auto start = std::chrono::high_resolution_clock::now();
  std::ifstream ifs(filepath, std::ios::binary);
  {
    cereal::BinaryInputArchive archive(ifs);
    archive(emptyGrammar);
  }
  ifs.close();
  auto end = std::chrono::high_resolution_clock::now();
  loadSlicingOracleSettingFromGrammar(emptyGrammar, logger);
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|grammar-deserialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print grammar file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|grammar-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  //print grammar statistics
  SPDLOG_LOGGER_TRACE(logger, "|grammar-string|{}", emptyGrammar.toString());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-symbols|{}",
                      emptyGrammar.getSymbols().size());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-productions|{}",
                      emptyGrammar.getProductions().size());
  return emptyGrammar;
}

void saveGrammar(const std::string& filepath,
                 const foresighter::implementation::sql::Grammar& grammar,
                 const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|grammar-serialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  std::ofstream ofs(filepath, std::ios::binary);
  {
    cereal::BinaryOutputArchive archive(ofs);
    archive(grammar);
  }
  ofs.close();
  auto end = std::chrono::high_resolution_clock::now();
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|grammar-serialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print grammar file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|grammar-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  //print grammar statistics
  // SPDLOG_LOGGER_TRACE(logger, "Grammar String:{}", grammar.toString());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-symbols|{}",
                      grammar.getSymbols().size());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-productions|{}",
                      grammar.getProductions().size());
}

foresighter::implementation::basics::SlicingOracle loadSlicingOracle(
    const std::string& filepath,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-deserialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  auto oracle =
      foresighter::implementation::basics::SlicingOracle::loadFromFile(filepath,
                                                                       logger);
  auto end = std::chrono::high_resolution_clock::now();
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|slicing-oracle-deserialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print slicing oracle file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  return oracle;
}

void saveSlicingOracle(
    const std::string& filepath,
    const foresighter::implementation::basics::SlicingOracle& oracle,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-serialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  std::ofstream ofs(filepath, std::ios::binary);
  {
    cereal::BinaryOutputArchive archive(ofs);
    archive(oracle);
  }
  ofs.close();
  auto end = std::chrono::high_resolution_clock::now();
  //print used time in ms
  SPDLOG_LOGGER_TRACE(
      logger, "|slicing-oracle-serialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  //print slicing oracle file size in MB
  SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
}

foresighter::implementation::sql::Grammar loadSettingAndGenerateGrammar(
    const std::string& grammarPath,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|grammar-setting-from-json-start|");
  auto grammarSetting =
      foresighter::implementation::sql::getGrammarSettingsFromJson(grammarPath);
  auto grammar = foresighter::implementation::sql::generateGrammarFromSettings(
      grammarSetting);
  SPDLOG_LOGGER_TRACE(logger, "|grammar-construction-finish|");
  SPDLOG_LOGGER_TRACE(logger, "|grammar-string|{}",
                      grammar.getTopoSortedString());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-symbols|{}",
                      grammar.getSymbols().size());
  SPDLOG_LOGGER_TRACE(logger, "|grammar-number-of-productions|{}",
                      grammar.getProductions().size());
  return grammar;
}

auto loadJSONSetting(const std::string& jsonFilePath,
                     const std::shared_ptr<spdlog::logger>& logger) {
  std::ifstream ifs(jsonFilePath);
  if (!ifs.is_open()) {
    SPDLOG_LOGGER_ERROR(logger, "Cannot open the json file at {}",
                        jsonFilePath);
    throw std::runtime_error("Cannot open the json file at " + jsonFilePath);
  }
  auto jsonObject = boost::json::parse(ifs);
  return jsonObject;
}

void loadSQLSetting(const std::string& sqlSettingPath,
                    const std::shared_ptr<spdlog::logger>& logger) {
  auto sqlSettingJson = loadJSONSetting(sqlSettingPath, logger);
  try {
    foresighter::implementation::sql::constants::Constants::sqlSetting
        .maxNumberOfColumnsInInput =
        sqlSettingJson.at("maxNumberOfColumnsInInput").as_int64();
    foresighter::implementation::sql::constants::Constants::sqlSetting
        .maxNumberOfColumnsIntermediate =
        sqlSettingJson.at("maxNumberOfColumnsIntermediate").as_int64();
    SPDLOG_LOGGER_TRACE(
        logger, "|sql-setting-max-number-of-columns-in-input|{}",
        foresighter::implementation::sql::constants::Constants::sqlSetting
            .maxNumberOfColumnsInInput);
    SPDLOG_LOGGER_TRACE(
        logger, "|sql-setting-max-number-of-columns-intermediate|{}",
        foresighter::implementation::sql::constants::Constants::sqlSetting
            .maxNumberOfColumnsIntermediate);
  } catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "Json file {} does not contain \"maxNumberOfColumnsInInput\" or "
        "\"maxNumberOfColumnsIntermediate\"",
        sqlSettingPath);
    throw std::runtime_error(
        "Json file " + sqlSettingPath +
        R"( does not contain "maxNumberOfColumnsInInput" or "maxNumberOfColumnsIntermediate")");
  }
}

void loadOnlineSearchSetting(const std::string& onlineSearchSettingPath,
                             const std::shared_ptr<spdlog::logger>& logger) {
  auto onlineSettingJson = loadJSONSetting(onlineSearchSettingPath, logger);

  try {
    foresighter::implementation::sql::constants::Constants::onlineSearchSetting
        .timeoutInMs = onlineSettingJson.at("timeoutInMs").as_int64();
    foresighter::implementation::sql::constants::Constants::onlineSearchSetting
        .numThreads = onlineSettingJson.at("numThreads").as_int64();
    foresighter::implementation::sql::constants::Constants::onlineSearchSetting
        .numCEGISExamples = onlineSettingJson.at("numCEGISExamples").as_int64();
    foresighter::implementation::sql::constants::Constants::onlineSearchSetting
        .topDownSketchMaxDepth =
        onlineSettingJson.at("topDownSketchMaxDepth").as_int64();
    foresighter::implementation::sql::constants::Constants::onlineSearchSetting
        .numCEGISExamples = onlineSettingJson.at("numCEGISExamples").as_int64();

    SPDLOG_LOGGER_TRACE(logger, "|online-search-setting-timeout-in-ms|{}",
                        foresighter::implementation::sql::constants::Constants::
                            onlineSearchSetting.timeoutInMs);
    SPDLOG_LOGGER_TRACE(logger, "|online-search-setting-num-threads|{}",
                        foresighter::implementation::sql::constants::Constants::
                            onlineSearchSetting.numThreads);
    SPDLOG_LOGGER_TRACE(logger, "|online-search-setting-num-cegis-examples|{}",
                        foresighter::implementation::sql::constants::Constants::
                            onlineSearchSetting.numCEGISExamples);
  } catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "Json file {} does not contain \"timeoutInMs\" or \"numThreads\" or "
        "\"numCEGISExamples\" or \"topDownSketchMaxDepth\"",
        onlineSearchSettingPath);
    throw std::runtime_error(
        "Json file " + onlineSearchSettingPath +
        R"( does not contain "timeoutInMs" or "numThreads")");
  }
}

void loadSlicingOracleSetting(const std::string& slicingOracleSettingPath,
                              const std::shared_ptr<spdlog::logger>& logger) {
  auto slicingOracleJson =
      loadJSONSetting(slicingOracleSettingPath, logger).as_object();

  try {
    foresighter::implementation::sql::constants::Constants::slicingOracleSetting
        .maxExactDistance = slicingOracleJson.at("MaxExactDistance").as_int64();
    SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-setting-max-exact-distance|{}",
                        foresighter::implementation::sql::constants::Constants::
                            slicingOracleSetting.maxExactDistance);
  } catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger,
                        "Json file {} does not contain \"MaxExactDistance\"",
                        slicingOracleSettingPath);
    throw std::runtime_error("Json file " + slicingOracleSettingPath +
                             " does not contain \"MaxExactDistance\"");
  }

  try {
    foresighter::implementation::sql::constants::Constants::slicingOracleSetting
        .maxTableIdx = slicingOracleJson.at("MaxTableIdx").as_int64();
    SPDLOG_LOGGER_TRACE(logger, "|slicing-oracle-setting-max-table-index|{}",
                        foresighter::implementation::sql::constants::Constants::
                            slicingOracleSetting.maxTableIdx);
  } catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger, "Json file {} does not contain \"MaxTableIdx\"",
                        slicingOracleSettingPath);
    throw std::runtime_error("Json file " + slicingOracleSettingPath +
                             " does not contain \"MaxTableIdx\"");
  }

  try {
    foresighter::implementation::sql::constants::Constants::slicingOracleSetting
        .maxSymbolicConstantNum =
        slicingOracleJson.at("MaxSymbolicConstantNum").as_int64();
    SPDLOG_LOGGER_TRACE(logger,
                        "|slicing-oracle-setting-max-symbolic-constant-num|{}",
                        foresighter::implementation::sql::constants::Constants::
                            slicingOracleSetting.maxSymbolicConstantNum);
  } catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(
        logger, "Json file {} does not contain \"MaxSymbolicConstantNum\"",
        slicingOracleSettingPath);
    throw std::runtime_error("Json file " + slicingOracleSettingPath +
                             " does not contain \"MaxSymbolicConstantNum\"");
  }
}

void saveStateMap(
    const std::string& filepath,
    const emhash8hashmap::HashMap<
        int, std::vector<foresighter::implementation::basics::IDType>>&
        stateMap,
    const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|state-map-serialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  std::ofstream ofs(filepath, std::ios::binary);
  {
    cereal::BinaryOutputArchive archive(ofs);
    archive(stateMap);
  }
  ofs.close();
  auto end = std::chrono::high_resolution_clock::now();
  SPDLOG_LOGGER_TRACE(
      logger, "|state-map-serialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  SPDLOG_LOGGER_TRACE(logger, "|state-map-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
}

emhash8hashmap::HashMap<
    int, std::vector<foresighter::implementation::basics::IDType>>
loadStateMap(const std::string& filepath,
             const std::shared_ptr<spdlog::logger>& logger) {
  SPDLOG_LOGGER_TRACE(logger, "|state-map-deserialization-start|");
  auto start = std::chrono::high_resolution_clock::now();
  emhash8hashmap::HashMap<
      int, std::vector<foresighter::implementation::basics::IDType>>
      stateMap;
  std::ifstream ifs(filepath, std::ios::binary);
  {
    cereal::BinaryInputArchive archive(ifs);
    archive(stateMap);
  }
  ifs.close();
  auto end = std::chrono::high_resolution_clock::now();
  SPDLOG_LOGGER_TRACE(
      logger, "|state-map-deserialization-time-ms|{}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());
  SPDLOG_LOGGER_TRACE(logger, "|state-map-file-size-mb|{}",
                      std::filesystem::file_size(filepath) / 1024 / 1024);
  return stateMap;
}
