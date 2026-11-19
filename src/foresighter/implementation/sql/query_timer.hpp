#pragma once

/**
 * @file query_timer.hpp
 * @brief Times synthesized SQL rewrites against a live PostgreSQL database so
 *        candidate solutions can be ranked by execution speed.
 *
 * The target database does NOT need to be pre-populated: each benchmark
 * carries its own input tables inline in its JSON file (see
 * BenchmarkReader). provisionSchema() creates a scratch schema named after
 * the benchmark, (re)creates those tables under their original names/columns
 * -- the same identifiers the synthesized SQL and the ground-truth "solution"
 * string already reference -- and pads each table's rows (by cyclic
 * repetition of the benchmark's own example rows, which preserves join/filter
 * selectivity instead of introducing unrelated synthetic keys) up to a target
 * volume so that execution-time differences between rewrites rise above
 * connection/parse/plan noise. The schema is dropped when the QueryTimer is
 * destroyed (or explicitly via dropSchema()).
 *
 * One QueryTimer owns one libpqxx connection and is NOT thread-safe:
 * construct one per worker thread (the online search runs benchmarks in a
 * thread pool, so one timer per synthesizeWithSlicing call is correct).
 */

#include <pqxx/pqxx>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace foresighter::implementation::sql {

class QueryTimer {
 public:
  // conninfo: standard libpq connection string, e.g.
  //   "host=localhost port=5432 dbname=postgres user=me password=..."
  //   Any reachable Postgres instance the caller can create/drop schemas in
  //   works -- nothing needs to be pre-loaded into it. Empty string disables
  //   timing (every call returns nullopt).
  // numRuns:  timed executions per query; the MEDIAN is reported.
  QueryTimer(const std::string& conninfo, int numRuns,
             std::shared_ptr<spdlog::logger> logger)
      : numRuns_(std::max(1, numRuns)), logger_(std::move(logger)) {
    if (conninfo.empty()) {
      SPDLOG_LOGGER_WARN(logger_,
                         "|query-timer-disabled| no conninfo; rewrites will "
                         "not be timed");
      return;
    }
    try {
      conn_ = std::make_unique<pqxx::connection>(conninfo);

      int statementTimeoutMs = 8000;
      if (const char* t =
              std::getenv("FORESIGHTER_TIMING_STATEMENT_TIMEOUT_MS")) {
        statementTimeoutMs = std::max(100, std::atoi(t));
      }
      // Session-level SET (not SET LOCAL): survives the commit below and
      // bounds every later query on this connection, including a candidate
      // rewrite that turns out to be a runaway join over the padded tables.
      pqxx::work init(*conn_);
      init.exec("SET statement_timeout = " +
               std::to_string(statementTimeoutMs));
      init.commit();
    } catch (const std::exception& e) {
      SPDLOG_LOGGER_ERROR(logger_, "|query-timer-connect-failed|{}", e.what());
      conn_.reset();
    }
  }

  ~QueryTimer() { dropSchema(); }

  QueryTimer(const QueryTimer&) = delete;
  QueryTimer& operator=(const QueryTimer&) = delete;

  bool isConnected() const { return conn_ && conn_->is_open(); }

  // Creates a fresh schema named after `benchmarkName` and fills it with
  // `benchmark`'s own input tables (real names/columns, so both the
  // synthesized rewrites and benchmark.getGroundTruth() resolve against it
  // unmodified), padding each table's rows up to a share of
  // `targetRowBudget` (split evenly across however many input tables the
  // benchmark has, never truncating below the table's own original row
  // count). No-op (returns false) if disabled or on any provisioning error --
  // callers should treat that the same as "timing unavailable", not fatal.
  template <class BenchmarkType>
  bool provisionSchema(const std::string& benchmarkName,
                       const BenchmarkType& benchmark, long targetRowBudget) {
    if (!isConnected())
      return false;
    dropSchema();
    schemaName_ = sanitizeSchemaName(benchmarkName);
    try {
      {
        // Unconditional (not gated on schemaProvisioned_, unlike the
        // dropSchema() call just above): a same-named schema can already
        // exist in the target database from a previous run of this binary
        // that was killed before its QueryTimer destructor ran (so *this*
        // object never got to set schemaProvisioned_ = true for it) --
        // "IF EXISTS" makes this a safe no-op otherwise.
        pqxx::work cleanupStale(*conn_);
        cleanupStale.exec("DROP SCHEMA IF EXISTS " + schemaName_ + " CASCADE");
        cleanupStale.commit();

        pqxx::work setup(*conn_);
        setup.exec("CREATE SCHEMA " + schemaName_);
        setup.exec("SET search_path TO " + schemaName_);
        setup.commit();
      }

      const size_t numTables = benchmark.getInputTableCount();
      const long perTableTarget =
          numTables > 0
              ? std::max<long>(1, targetRowBudget / (long)numTables)
              : targetRowBudget;

      for (size_t i = 0; i < numTables; ++i) {
        provisionOneTable(benchmark, i, perTableTarget);
      }
      schemaProvisioned_ = true;
      return true;
    } catch (const std::exception& e) {
      SPDLOG_LOGGER_ERROR(logger_, "|query-timer-provision-failed|{}|{}",
                          benchmarkName, e.what());
      schemaProvisioned_ = false;
      return false;
    }
  }

  void dropSchema() {
    if (!isConnected() || !schemaProvisioned_)
      return;
    try {
      pqxx::work cleanup(*conn_);
      cleanup.exec("DROP SCHEMA IF EXISTS " + schemaName_ + " CASCADE");
      cleanup.commit();
    } catch (const std::exception& e) {
      SPDLOG_LOGGER_ERROR(logger_, "|query-timer-drop-schema-failed|{}",
                          e.what());
    }
    schemaProvisioned_ = false;
  }

  // Runs `sql` once as warm-up, then numRuns_ timed runs, returning the median
  // wall-clock time in nanoseconds. Returns nullopt if the query cannot be run
  // at all (no connection / no provisioned schema / SQL error / timeout) so a
  // broken or runaway candidate is SKIPPED rather than winning the ranking.
  std::optional<long long> medianExecTimeNs(const std::string& sql) {
    if (!isConnected()) return std::nullopt;

    try {
      // Warm-up: populate buffer/plan cache so the first timed run isn't an
      // outlier. Result discarded, transaction rolled back (read-only).
      {
        pqxx::work warm(*conn_);
        (void)warm.exec(sql);
        warm.abort();
      }

      std::vector<long long> samples;
      samples.reserve(static_cast<size_t>(numRuns_));
      for (int i = 0; i < numRuns_; ++i) {
        pqxx::work txn(*conn_);
        const auto start = std::chrono::high_resolution_clock::now();
        // pqxx fully materializes the result set inside exec(), so this timing
        // window covers planning + execution + row transfer.
        pqxx::result r = txn.exec(sql);
        const auto end = std::chrono::high_resolution_clock::now();
        txn.abort();  // never commit; timing must not mutate the DB
        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count());
      }

      std::sort(samples.begin(), samples.end());
      const size_t n = samples.size();
      return (n % 2) ? samples[n / 2]
                     : (samples[n / 2 - 1] + samples[n / 2]) / 2;
    } catch (const std::exception& e) {
      SPDLOG_LOGGER_TRACE(logger_, "|query-timer-exec-failed|{}|{}", e.what(),
                          sql);
      return std::nullopt;
    }
  }

 private:
  template <class BenchmarkType>
  void provisionOneTable(const BenchmarkType& benchmark, size_t idx,
                         long targetRows) {
    const std::string& tableName = benchmark.getBenchmarkTableName(idx);
    const auto& colNames = benchmark.getBenchmarkTableColNames(idx);
    const auto& concreteTable = benchmark.getIOPairs()[0].getInput()[idx];
    const auto& colTypes = concreteTable.getColumnDataTypes();
    const auto& rows = concreteTable.getTable();

    pqxx::work txn(*conn_);

    std::string ddl = "CREATE TABLE " + quoteIdent(tableName) + " (";
    for (size_t c = 0; c < colNames.size(); ++c) {
      ddl += quoteIdent(colNames[c]) + " " + pgTypeFor(colTypes[c]);
      if (c + 1 < colNames.size())
        ddl += ", ";
    }
    ddl += ")";
    txn.exec(ddl);

    if (!rows.empty()) {
      std::string colList;
      for (size_t c = 0; c < colNames.size(); ++c) {
        colList += quoteIdent(colNames[c]);
        if (c + 1 < colNames.size())
          colList += ", ";
      }

      // Cyclic repetition of the benchmark's own rows (not synthesized
      // values): this preserves whatever join keys / filter selectivity the
      // original tiny example had, so a rewrite's join/filter strategy is
      // still what dominates its timing at scale, not arbitrary new data.
      const long rowsToWrite = std::max<long>((long)rows.size(), targetRows);
      constexpr size_t kBatchSize = 1000;
      const std::string insertPrefix =
          "INSERT INTO " + quoteIdent(tableName) + " (" + colList + ") VALUES ";
      std::string batch;
      size_t inBatch = 0;
      for (long r = 0; r < rowsToWrite; ++r) {
        const auto& row = rows[r % (long)rows.size()];
        if (inBatch > 0)
          batch += ", ";
        batch += "(";
        for (size_t c = 0; c < row.size(); ++c) {
          batch += row[c].toString();
          if (c + 1 < row.size())
            batch += ", ";
        }
        batch += ")";
        ++inBatch;
        if (inBatch == kBatchSize || r + 1 == rowsToWrite) {
          txn.exec(insertPrefix + batch);
          batch.clear();
          inBatch = 0;
        }
      }
    }
    txn.commit();
  }

  static std::string quoteIdent(const std::string& ident) {
    std::string out = "\"";
    for (char c : ident) {
      if (c == '"')
        out += "\"\"";
      else
        out += c;
    }
    out += "\"";
    return out;
  }

  template <class ColumnDataType>
  static std::string pgTypeFor(const ColumnDataType& t) {
    return t.toString() == "NUMERIC" ? "double precision" : "text";
  }

  static std::string sanitizeSchemaName(const std::string& benchmarkName) {
    std::string out = "bench_";
    for (char c : benchmarkName) {
      out += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    }
    return out;
  }

  std::unique_ptr<pqxx::connection> conn_;
  int numRuns_;
  std::shared_ptr<spdlog::logger> logger_;
  std::string schemaName_;
  bool schemaProvisioned_ = false;
};

}  // namespace foresighter::implementation::sql
