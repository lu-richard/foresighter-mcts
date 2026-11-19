/**
* @file constants.hpp
* @brief 
* @author Rui Dong
* @date 4/27/24
*/

#pragma once

#include <string>

namespace foresighter::implementation::sql::constants {
struct SlicingOracleSetting {
  unsigned int maxExactDistance = 30;
  unsigned int maxTableIdx = 2;
  unsigned int maxSymbolicConstantNum = 2;
};

struct OnlineSearchSetting {
  unsigned int timeoutInMs = 300000;
  unsigned int numThreads = 5;
  unsigned int numCEGISExamples = 10;
  unsigned int topDownSketchMaxDepth = 2;
  std::string sketchJsonFile;
};

struct SQLSetting {
  unsigned int maxNumberOfColumnsInInput = 12;
  unsigned int maxNumberOfColumnsIntermediate = 24;
};

class Constants {
 public:
  static SlicingOracleSetting slicingOracleSetting;
  static OnlineSearchSetting onlineSearchSetting;
  static SQLSetting sqlSetting;
};
}  // namespace foresighter::implementation::sql::constants
