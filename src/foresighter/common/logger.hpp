/**
* @file logger.hpp
* @brief 
* @author Rui Dong
* @date 7/1/24
*/

#pragma once
//set logger active to trace
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace foresighter::common {
inline std::shared_ptr<spdlog::logger> getConsoleAndFileSinkLogger(
    const std::string& logOutputPath, const std::string& loggerName) {
  //set sink to be both console and file
  auto fileSink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>(logOutputPath);
  fileSink->set_level(spdlog::level::trace);
  fileSink->set_pattern("[%H:%M:%S:%F %z] [%^%l%$] [%s:%#] %v");
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
  consoleSink->set_level(spdlog::level::trace);
  consoleSink->set_pattern("[%H:%M:%S:%F %z] [%^%l%$] [%s:%#] %v");
  auto logger = std::make_shared<spdlog::logger>(
      loggerName, spdlog::sinks_init_list{fileSink, consoleSink});
  logger->set_level(spdlog::level::trace);
  return logger;
}

inline std::shared_ptr<spdlog::logger> getConsoleSinkLogger(
    const std::string& loggerName) {
  //set sink to be console
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
  consoleSink->set_level(spdlog::level::trace);
  consoleSink->set_pattern("[%H:%M:%S:%F %z] [%^%l%$] [%s:%#] %v");
  auto logger = std::make_shared<spdlog::logger>(
      loggerName, spdlog::sinks_init_list{consoleSink});
  logger->set_level(spdlog::level::trace);
  return logger;
}

inline std::shared_ptr<spdlog::logger> getFileSinkLogger(
    const std::string& logOutputPath, const std::string& loggerName) {
  //set sink to be file
  auto fileSink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>(logOutputPath);
  fileSink->set_level(spdlog::level::trace);
  fileSink->set_pattern("[%H:%M:%S:%F %z] [%^%l%$] [%s:%#] %v");
  auto logger = std::make_shared<spdlog::logger>(
      loggerName, spdlog::sinks_init_list{fileSink});
  logger->set_level(spdlog::level::trace);
  return logger;
}
}  // namespace foresighter::common
