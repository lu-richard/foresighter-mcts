#pragma once
/**
 * @file sql_rewrite_utils.hpp
 * @brief Small SQL-string helpers shared by every online search strategy
 * (synthesize_with_slicing.hpp, top_down_mcts_final.hpp, ...) so a candidate
 * rewrite's columns can be permuted to match the benchmark's expected output
 * order.
 */
#include <algorithm>
#include <string>
#include <vector>

namespace foresighter::implementation::sql {

// Wraps `sql` with a final projection that reorders its columns according to
// `mapping` (mapping[i] is the index, in `sql`'s own column order, of the
// column that should appear as the i-th output column) -- i.e. produces
// "SELECT c<mapping[0]> AS c0, c<mapping[1]> AS c1, ... FROM ( sql ) AS T".
inline std::string wrapSQLWithFinalProjection(const std::string& sql,
                                              const std::vector<int>& mapping) {
  std::string result;
  result += "SELECT ";
  for (int i = 0; i < mapping.size(); ++i) {
    result += "c" + std::to_string(mapping[i]) + " AS c" + std::to_string(i);
    if (i < mapping.size() - 1) {
      result += ", ";
    }
  }
  result += " FROM ( ";
  result += sql;
  result += " ) AS T";
  return result;
}

}  // namespace foresighter::implementation::sql
