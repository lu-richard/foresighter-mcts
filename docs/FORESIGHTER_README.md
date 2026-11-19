# Foresighter

Foresighter implements the presynthesis framework described in *"Presynthesis: Towards Scaling Up Program Synthesis with Finer-Grained Abstract Semantics."* The framework is domain-agnostic; this repository provides a full instantiation to SQL synthesis.

For artifact evaluation instructions (reproducing paper results, running experiments), see the [top-level README](../README.md).

## Building

```bash
bash scripts/install.sh        # installs dependencies via Conan, builds with CMake
```

Requires GCC 14+ (C++23), CMake 3.22+, and Python 3.9+ (for Conan). The build produces 8 executables in `build/`.

## Source Structure

```
src/foresighter/
├── implementation/
│   ├── basics/              # Domain-agnostic core (C++ templates)
│   │   ├── fta.hpp          #   FTA = (Q, F, Q_f, Δ)
│   │   ├── state.hpp        #   State = (symbol, abstract value, cost)
│   │   ├── transition.hpp   #   Transition = f(q1,...,qn) → q
│   │   ├── grammar.hpp      #   Grammar = (V, Σ, R, s0)
│   │   ├── production.hpp   #   Production = s → f(s1,...,sn)
│   │   ├── transformer.hpp  #   Abstract transformer interface ⟦f⟧♯
│   │   ├── abstractor.hpp   #   Abstraction function α
│   │   ├── lattice.hpp      #   Lattice operations (meet, join, ⊑)
│   │   ├── slicing_oracle.hpp   # Reachability oracle O
│   │   ├── slicing_server.hpp   # Per-benchmark slice cache
│   │   ├── slice.hpp            # Slice A_e (sub-FTA)
│   │   ├── benchmark.hpp       # Benchmark base class
│   │   ├── io_pair.hpp         # Input-output example
│   │   ├── entity_manager.hpp  # Persistent state/transition storage
│   │   └── ...                 # (symbol, term, types, value, etc.)
│   ├── algorithms/          # Core algorithms (templated)
│   │   ├── offline_fta_construct.hpp    # BuildOfflineFTA
│   │   ├── synthesize_with_slicing.hpp  # Slice + Search
│   │   ├── cfta_term_extraction.hpp     # Term extraction
│   │   ├── grammar_topo_sort.hpp        # Topological sort
│   │   └── unroll_grammar.hpp           # Grammar finitization
│   └── sql/                 # SQL instantiation (domain-specific)
│       ├── symbol.hpp       #   SQL symbol type (69 variants)
│       ├── symbol/          #   Individual symbols: NtQuery, OpSelect, ...
│       ├── grammar_generator.hpp    # Generates Grammar<Symbol> from settings
│       ├── benchmark_reader.hpp     # Parses benchmark JSON → SQLBenchmark
│       ├── cegis_runner.hpp         # CEGIS counter-example generation
│       ├── constants.hpp            # Settings structures
│       ├── concrete_value/          # ConcreteTable, error types
│       ├── semantics/transformers/  # Concrete + abstract transformers
│       ├── semantics/abstractors/   # Abstraction functions
│       ├── semantics/values/        # Value types for transformers
│       └── abstract_value/          # Predicate types
└── common/                  # Utilities (logging, hashing)

entries/                     # 8 executable entry points
├── fta_construct.cpp
├── slicing_oracle_construct.cpp
├── build_state_map_for_slicing.cpp
├── slicing_server_building.cpp
├── slicing_server_building_ablation.cpp
├── online_search_with_slicing.cpp
├── ablation_run.cpp
└── gather_slicing_size_info.cpp
```

The key architectural boundary: `basics/` and `algorithms/` contain no SQL-specific code. All domain-specific logic lives in `sql/`. To target a new domain, create a new directory parallel to `sql/` and implement the same interfaces (see [Reusability Guide](../README.md#4-reusability-guide)).

## Executables

All executables use Boost.ProgramOptions. Run any with `--help` for full argument descriptions.

### Core Pipeline

These run in sequence to perform presynthesis and synthesis:

| Executable | Purpose |
|-----------|---------|
| `fta_construct` | Build offline FTA A from grammar settings |
| `slicing_oracle_construct` | Build reachability oracle O from A |
| `build_state_map_for_slicing` | Pre-compute lookup table for fast slicing |
| `slicing_server_building` | Pre-compute and cache slices for all benchmarks |
| `online_search_with_slicing` | Online synthesis: load slice, search, CEGIS verify |

### Ablation / Experiment

| Executable | Purpose |
|-----------|---------|
| `ablation_run` | NoPresyn ablation: builds FTA on-the-fly per benchmark |
| `slicing_server_building_ablation` | NoOracle ablation: slicing without oracle O |
| `gather_slicing_size_info` | Report slice size statistics |

## Example: End-to-End Pipeline

```bash
# 1. Build FTA (k=4 predicates)
./build/fta_construct \
    --grammar_input grammarSetting.json \
    --sql_setting_input sql_setting.json \
    --slicing_oracle_setting_input oracle_setting.json \
    --num_of_predicates 4 \
    --fta_output fta_4.bin --grammar_output grammar_4.bin --log_output fta_4.log

# 2. Build oracle
./build/slicing_oracle_construct \
    --fta_input fta_4.bin --grammar_input grammar_4.bin \
    --slicing_oracle_setting_input oracle_setting.json \
    --slicing_oracle_output oracle_4.bin --log_output oracle_4.log

# 3. Build state map
./build/build_state_map_for_slicing \
    --fta_input fta_4.bin --slicing_oracle_input oracle_4.bin \
    --grammar_input grammar_4.bin \
    --oracle_setting_input oracle_setting.json --sql_setting_input sql_setting.json \
    --state_map_output state_map_4.bin --log_output state_map_4.log

# 4. Pre-compute slices
./build/slicing_server_building \
    --fta_input fta_4.bin --slicing_oracle_input oracle_4.bin \
    --grammar_input grammar_4.bin --state_map_input state_map_4.bin \
    --oracle_setting_input oracle_setting.json --sql_setting_input sql_setting.json \
    --benchmark_list_input benchmarks.txt \
    --slicing_server_output oracle_output_4/ --log_output slicing_4.log

# 5. Synthesize
./build/online_search_with_slicing \
    --grammar_input grammar_4.bin --slicing_input oracle_output_4/ \
    --benchmark_list_input benchmarks.txt \
    --sql_setting_input sql_setting.json --online_search_setting_input online_setting.json \
    --log_output_folder synthesis_log_4/ --cegis_fuzz_dir fuzz/
```

For automated execution with SLURM job management, use the scripts in `../scripts/` (see [top-level README](../README.md#34-step-by-step-pipeline-reference)).
