#pragma once

#include <foresighter/implementation/basics/slicing_oracle.hpp>
#include <foresighter/implementation/sql/types.hpp>

/**
 * @brief save FTA to given filepath
 * @details This function will also print out the FTA statistics and saving time.
 * 
 * @param filepath the path to store FTA
 * @param fta the FTA object to be serialized
 */

void saveFTA(const std::string& filepath,
             const foresighter::implementation::sql::OfflineFTA& fta,
             const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief save grammar to given filepath
 * @details This function will also print out the grammar statistics and saving time.
 * 
 * @param filepath The file path to store the grammar
 * @param grammar the grammar object to be serialized
 */

void saveGrammar(const std::string& filepath,
                 const foresighter::implementation::sql::Grammar& grammar,
                 const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief save slicing oracle to given filepath
 * 
 * @param filepath 
 * @param oracle 
 */
void saveSlicingOracle(
    const std::string& filepath,
    const foresighter::implementation::basics::SlicingOracle& oracle,
    const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief load FTA from given filepath
 * @details This function will also print out the FTA statistics and loading time.
 * 
 * @param filepath the path that stores FTA
 * @param emptyFTA the FTA object to store the deserialized FTA
 * @return foresighter::implementation::sql::FTA 
 */

foresighter::implementation::sql::OfflineFTA loadFTA(
    const std::string& filepath, const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief load grammar from given filepath
 * @details This function will also print out the grammar statistics and loading time.
 * 
 * @param filepath the path that stores grammar
 * @param emptyGrammar the grammar object to store the deserialized grammar
 * @return foresighter::implementation::sql::Grammar 
 */

foresighter::implementation::sql::Grammar loadGrammar(
    const std::string& filepath, const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief load slicing oracle from given filepath
 * 
 * @param filepath path to load the oracle
 * @param oracle 
 */
foresighter::implementation::basics::SlicingOracle loadSlicingOracle(
    const std::string& filepath, const std::shared_ptr<spdlog::logger>& logger);

/**
 * @brief load setting from file and generate a grammar
 * 
 * @param grammarPath the path to load the setting
 * @return foresighter::implementation::sql::Grammar generated grammar
 */

foresighter::implementation::sql::Grammar loadSettingAndGenerateGrammar(
    const std::string& grammarPath,
    const std::shared_ptr<spdlog::logger>& logger);

void loadSQLSetting(const std::string& sqlSettingPath,
                    const std::shared_ptr<spdlog::logger>& logger);

void loadOnlineSearchSetting(const std::string& onlineSearchSettingPath,
                             const std::shared_ptr<spdlog::logger>& logger);

void loadSlicingOracleSetting(const std::string& slicingOracleSettingPath,
                              const std::shared_ptr<spdlog::logger>& logger);

void saveStateMap(
    const std::string& filepath,
    const emhash8hashmap::HashMap<
        int, std::vector<foresighter::implementation::basics::IDType>>&
        stateMap,
    const std::shared_ptr<spdlog::logger>& logger);

emhash8hashmap::HashMap<
    int, std::vector<foresighter::implementation::basics::IDType>>
loadStateMap(const std::string& filepath,
             const std::shared_ptr<spdlog::logger>& logger);
