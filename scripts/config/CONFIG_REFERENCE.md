# Configuration Reference

This document describes all configuration files in `scripts/config/`. These are SQL-specific settings for the Foresighter instantiation described in the paper.

## `grammarSetting.json` -- DSL Grammar

Controls which SQL operators are included in the search space and how deep the grammar can expand.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MaxTableIdx` | 3 | Max number of input tables |
| `MaxColIdx` | 18 | Max column index |
| `MaxSymbolicConstantNum` | 2 | Max constants drawn from benchmark hints |
| `MaxFilterPredicateNum` | 4 | Max WHERE predicates (controls how deeply filter conditions can nest) |
| `MaxProjectionListLength` | 3 | Max columns in SELECT |
| `MaxAggListLength` | 2 | Max aggregation functions |
| `MaxWindowListLength` | 0 | Max window functions (0 = disabled) |
| `MaxGroupByListLength` | 2 | Max GROUP BY columns |
| `QExpansionLimit` | 9 | Max query expression depth |
| `JoinClauseExpansionLimit` | 2 | Max join clause expansions |
| `MaxProjectDistinctExpansionTimes` | 2 | Max DISTINCT projection expansions |
| `MaxProjectDistinctMaxExpansionTimes` | 1 | Max nested DISTINCT expansions |
| `MaxJoinPredicateExpansionTimes` | 3 | Max join predicate expansions |
| `MaxGroupByExpansionTimes` | 2 | Max GROUP BY expansions |
| `Enable*` flags | varies | Toggle individual SQL operators (Projection, Filter, GroupWithoutKey, GroupWithKey, Window, InnerJoin, LeftJoin, each aggregation type, OrderBy, window functions) |

## `oracle_setting.json` -- Oracle Construction

Parameters for building the reachability oracle O (Fig. 7 in the paper).

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MaxExactDistance` | 30 | Max cost depth explored in FTA A |
| `MaxTableIdx` | 3 | Max input tables considered by the oracle |
| `MaxSymbolicConstantNum` | 2 | Max symbolic constants in oracle constraints |

## `sql_setting.json` -- SQL Limits

SQL-specific constraints on table dimensions.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `maxNumberOfColumnsInInput` | 15 | Max columns per input table (benchmarks exceeding this are skipped) |
| `maxNumberOfColumnsIntermediate` | 30 | Max columns in intermediate tables during synthesis |

## `online_setting.json` -- Synthesis Runtime

Controls for the online synthesis phase (Search in Algorithm 1).

| Parameter | Default | Description |
|-----------|---------|-------------|
| `timeoutInMs` | 600000 | Per-benchmark timeout in milliseconds (600s = 10 min) |
| `numThreads` | 1 | Threads for parallel synthesis within a single benchmark |
| `numCEGISExamples` | 16 | Number of CEGIS counter-examples to generate |
