#pragma once
/**
 * @file top_down_mcts.hpp
 * @brief Online synthesis: top-down Monte-Carlo Tree Search alternative to
 * synthesize_with_slicing.hpp.
 *
 * synthesize_with_slicing.hpp does an exhaustive, cost-bounded, bottom-up
 * concretization of every slice and keeps searching even after a correct
 * program is found, purely so that it can also report the fastest verified
 * rewrite. That is complete but wasteful: it spends the same effort on
 * clearly-unpromising regions of the AFTA as it does on promising ones.
 *
 * This file instead treats "which abstract transition to expand next" as a
 * bandit problem and drives the search with UCT: starting from the merged
 * AFTA's final (output-consistent) states, repeatedly select the
 * most-promising already-visited transition, expand one new transition at
 * the frontier, randomly play out the rest of the subtree down to leaves,
 * concretize just that one sampled derivation, verify it via CEGIS, and
 * backpropagate a reward based on measured execution-time speedup. Over many
 * iterations this concentrates search effort on the regions of the grammar
 * that actually tend to produce fast, correct rewrites.
 */
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <random>

#include <foresighter/implementation/sql/query_timer.hpp>

#include <foresighter/implementation/sql/symbol/var_table.hpp>
#include <foresighter/implementation/sql/types.hpp>

#include <foresighter/implementation/sql/semantics/transformers/concrete_transformer.hpp>

#include <foresighter/common/hash.hpp>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/common/exceptions.hpp"
#include "foresighter/implementation/basics/fta.hpp"
#include "foresighter/implementation/sql/abstract_value/unabstracted_term.hpp"
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/semantics/values/concrete_value.hpp"
#include "foresighter/implementation/sql/symbol.hpp"
#include "foresighter/implementation/sql/symbol/var_filter_const.hpp"

#include <foresighter/implementation/basics/slice.hpp>

#include <foresighter/implementation/algorithms/cfta_term_extraction.hpp>
#include <foresighter/implementation/sql/cegis_runner.hpp>
#include <foresighter/implementation/sql/constants.hpp>
#include <foresighter/implementation/sql/sql_rewrite_utils.hpp>

namespace {
using IDType = foresighter::implementation::basics::IDType;
using foresighter::implementation::sql::wrapSQLWithFinalProjection;
}  // namespace

namespace foresighter::implementation::algorithms {
namespace fixedroot_variant {

// Visit statistics for one abstract transition, scoped to a particular
// "path context" (see pathHash below). Transition/State objects returned by
// EntityManager::read() are always const (they are hash-consed and shared),
// so these stats cannot live on the transition itself -- they are tracked
// externally, keyed by transition ID.
struct MCTSStats {
  double totalReward = 0.0;
  unsigned long long numVisits = 0;
};

// One concrete realization of an abstract position: the CFTA state it
// evaluates to, paired with the term (candidate SQL syntax) that produced
// it. A single abstract position can have several of these pooled together
// (see hashToConcreteCandidates below), since the slice and the CFTA are not
// one-to-one -- one abstract skeleton can be filled in with more than one
// concrete derivation.
struct ConcreteCandidate {
  IDType state;
  sql::Term term;
};

// Thrown to unwind out of a single MCTS iteration -- specifically out of
// the recursive parameter-enumeration in concretize() -- when the overall
// search timeout has been exceeded mid-enumeration. Without this, a single
// iteration whose parameter space happens to be combinatorially large (e.g.
// several independent column/constant choices multiplying together) could
// run far past the intended timeout with nothing to stop it, since
// areResourcesRemaining() is otherwise only checked between iterations of
// the main driver loop, never inside one iteration's own work.
struct MCTSTimeoutSignal {};

class MCTSSynthesizerFixedRoot {
public:
    using IDType = foresighter::implementation::basics::IDType;
    using WeightType = foresighter::implementation::basics::WeightType;

    MCTSSynthesizerFixedRoot() = delete;

    explicit MCTSSynthesizerFixedRoot(std::shared_ptr<spdlog::logger> logger)
    : logger(std::move(logger)) {}

    //forbid copy and move
    MCTSSynthesizerFixedRoot(const MCTSSynthesizerFixedRoot&) = delete;
    MCTSSynthesizerFixedRoot(MCTSSynthesizerFixedRoot&&) = delete;
    MCTSSynthesizerFixedRoot& operator=(const MCTSSynthesizerFixedRoot&) = delete;
    MCTSSynthesizerFixedRoot& operator=(MCTSSynthesizerFixedRoot&&) = delete;

    // Merges every per-cost-level slice (h = 1..maxExactDistance) for this
    // benchmark into one AFTA, re-keying states/transitions onto shared
    // "global" IDs so a state reachable from multiple cost levels is only
    // represented once. MCTS then runs a single search over this merged AFTA
    // instead of iterative-deepening level by level.
    template <class FTAType>
    foresighter::implementation::basics::Slice<FTAType> mergeAllCostSlices(
        const std::string& benchmarkName, const auto& slicingServer,
        std::shared_ptr<spdlog::logger>& logger) {
        using IDType = foresighter::implementation::basics::IDType;

        FTAType mergedFta;
        emhash8hashmap::HashMap<IDType, IDType> globalToMergedLocal;
        emhash8hashmap::HashMap<IDType, IDType> mergedLocalToGlobal;

        for (int h = 1;
            h <= foresighter::implementation::sql::constants::Constants::
                        slicingOracleSetting.maxExactDistance;
            ++h) {
            auto slice = slicingServer.loadSlicing(benchmarkName, h, logger);
            auto& sfta = slice.getFTA();

            // States first, so every transition's head/tails already have a merged id.
            for (auto localState : sfta.getStates()) {
            auto globalState = slice.getGlobalState(localState);
            if (globalToMergedLocal.find(globalState) != globalToMergedLocal.end())
                continue;  // already pulled in by an earlier h
            auto& se = sfta.getStateManager().read(localState);
            auto newId = mergedFta.getStateManager().create(
                se.getSymbol(), se.getValue(), se.getCost());
            mergedFta.addState(newId);
            globalToMergedLocal[globalState] = newId;
            mergedLocalToGlobal[newId] = globalState;
            }
            for (auto localFinal : sfta.getFinalStates()) {
            mergedFta.markFinalState(
                globalToMergedLocal[slice.getGlobalState(localFinal)]);
            }

            // Transitions: re-key to merged ids; create() dedupes on
            // (head, tails, symbol) via Transition::operator==, so a transition
            // that reappears in a later h's slice just collapses into the same entry.
            for (auto localTransition : sfta.getTransitions()) {
            auto& te = sfta.getTransitionManager().read(localTransition);
            auto newHead = globalToMergedLocal[slice.getGlobalState(te.getHead())];
            std::vector<IDType> newTails;
            newTails.reserve(te.getTails().size());
            for (auto t : te.getTails())
                newTails.push_back(globalToMergedLocal[slice.getGlobalState(t)]);
            auto newTID = mergedFta.getTransitionManager().create(
                newHead, te.getSymbol(), newTails, te.getCost(), te.getOrder());
            mergedFta.addTransition(newTID);
            }
        }

        return foresighter::implementation::basics::Slice<FTAType>(
            std::move(mergedFta), std::move(mergedLocalToGlobal));
    }

    // Runs top-down MCTS over the merged AFTA for `benchmark` until
    // `timeoutInMs` elapses, returning the fastest verified rewrite found
    // (or an empty string if none was found).
    template <class ConcreteSemanticsType, class BenchmarkType, class GrammarType>
    std::string runMCTS(
        const BenchmarkType& benchmark, const GrammarType& grammar,
        const auto& slicingServer, long timeoutInMs,
        foresighter::implementation::sql::CEGISRunner& cegisRunner) {
        using SymbolType = typename GrammarType::SymbolType;
        using TermType = sql::Term;
        using ConcreteValueType =
            foresighter::implementation::sql::semantics::values::ConcreteValue;
        using ConcreteFTAType = basics::FTA<SymbolType, ConcreteValueType>;

        auto searchStart = std::chrono::high_resolution_clock::now();
        auto areResourcesRemaining = [&]() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::high_resolution_clock::now() - searchStart)
                       .count() < timeoutInMs;
        };

        // Checking the clock on every single combination inside the
        // recursive parameter enumeration would itself add measurable
        // overhead at the scale of hundreds of thousands of combos per
        // iteration, so only check every kTimeoutCheckInterval combos --
        // frequent enough to bound how far a single iteration can overrun,
        // cheap enough not to slow down the common case.
        constexpr long long kTimeoutCheckInterval = 2000;
        long long combosSinceTimeoutCheck = 0;
        auto checkTimeoutOrThrow = [&]() {
            if (++combosSinceTimeoutCheck >= kTimeoutCheckInterval) {
                combosSinceTimeoutCheck = 0;
                if (!areResourcesRemaining()) {
                    throw MCTSTimeoutSignal{};
                }
            }
        };

        // cartesianProduct() pre-allocates a vector sized to the *total*
        // combination count before we ever get a chance to check the clock
        // inside its loop -- so if that count is huge (independent
        // parameter positions multiplying together), the allocation itself
        // can hang or exhaust memory before checkTimeoutOrThrow() is ever
        // reached. Guard against that up front instead.
        constexpr long long kMaxCombosPerPosition = 200000;
        auto guardComboCount =
            [&](const std::vector<std::vector<ConcreteCandidate>>& candidatesPerTail) {
                long long total = 1;
                for (auto& c : candidatesPerTail) {
                    total *= (long long)c.size();
                    if (total > kMaxCombosPerPosition) {
                        throw MCTSTimeoutSignal{};
                    }
                }
            };

        ConcreteSemanticsType interpreter(benchmark);

        // Per-call Mersenne Twister engine (not global rand()/srand()):
        // FORESIGHTER_MCTS_SEED gives a reproducible sequence for controlled
        // A/B comparisons; unset falls back to std::random_device for a
        // genuinely non-deterministic default. Being a local variable here
        // (rather than process-global state) also makes this safe under
        // concurrent benchmarks -- online_search_with_slicing runs
        // benchmarks across a boost::asio::thread_pool, and global
        // rand()/srand() would let concurrently-running searches clobber
        // each other's seed/sequence.
        std::mt19937 rng = [&] {
            if (const char* seedEnv = std::getenv("FORESIGHTER_MCTS_SEED")) {
                return std::mt19937((unsigned)std::atoi(seedEnv));
            }
            std::random_device rd;
            return std::mt19937(rd());
        }();

        const char* pgConnEnv = std::getenv("FORESIGHTER_PG_CONNINFO");
        int timingRuns = 5;
        if (const char* r = std::getenv("FORESIGHTER_TIMING_RUNS"))
            timingRuns = std::max(1, std::atoi(r));
        long timingRowBudget = 20000;
        if (const char* rb = std::getenv("FORESIGHTER_TIMING_ROW_BUDGET"))
            timingRowBudget = std::max(1L, std::atol(rb));
        foresighter::implementation::sql::QueryTimer queryTimer(
            pgConnEnv ? std::string(pgConnEnv) : std::string(), timingRuns, logger);
        queryTimer.provisionSchema(benchmark.getName(), benchmark, timingRowBudget);

        long long origTimeNs = -1;
        if (auto o = queryTimer.medianExecTimeNs(benchmark.getGroundTruth())) {
            origTimeNs = *o;
        }

        // ! AFTA SLICE ACROSS ALL COST LEVELS
        auto slice = mergeAllCostSlices<sql::OfflineFTA>(benchmark.getName(),
                                                          slicingServer, logger);
        auto& fta = slice.getFTA();

        std::vector<IDType> targetStates = fta.getFinalStates();
        if (targetStates.empty()) {
            queryTimer.dropSchema();
            return "";
        }

        ConcreteFTAType cfta;
        auto& aftaStateManager = fta.getStateManager();
        auto& aftaTransitionManager = fta.getTransitionManager();
        auto& cftaStateManager = cfta.getStateManager();
        auto& cftaTransitionManager = cfta.getTransitionManager();

        FTATermExtractor<TermType, ConcreteFTAType> termExtractor(cfta, logger);

        // MCTS visit statistics, external to the (immutable, hash-consed)
        // AFTA transitions: transitionID -> pathHash -> stats. pathHash
        // identifies *which* partial derivation led to this transition (see
        // select() below), so the same shared AFTA transition reached via two
        // different parents accumulates independent statistics.
        emhash8hashmap::HashMap<IDType, emhash8hashmap::HashMap<size_t, MCTSStats>>
            transitionStats;
        // Same idea, but for the root-level choice among candidate target
        // states (there is no incoming transition to key the choice on).
        emhash8hashmap::HashMap<IDType, MCTSStats> rootStats;
        unsigned long long totalRootVisits = 0;

        // Every concrete alternative ever built at a given abstract
        // position, pooled across MCTS iterations and never cleared. Keyed
        // by a Merkle-style structural hash: a leaf's hash is derived from
        // its symbol, an interior transition's hash is derived from its own
        // symbol/order plus each tail's hash. Two structurally-identical
        // derivations always collide onto the same entry (so the expensive
        // interpreter.transform() call is only ever run once for them,
        // regardless of which iteration's playout got there first); two
        // different derivations that both produce a value for what is
        // nominally "the same" abstract position get separate, coexisting
        // entries instead of overwriting one another. concretize() below
        // only ever looks a position up via a hash it just computed while
        // walking the *current* iteration's own backtrackPath -- it never
        // consults fta.getTransitionsByHead -- so this pool being shared
        // across iterations can never let one iteration's extraction pull in
        // a derivation that belongs to a different, unrelated iteration's
        // skeleton.
        emhash8hashmap::HashMap<size_t, std::vector<ConcreteCandidate>>
            hashToConcreteCandidates;
        
        emhash8hashmap::HashMap<size_t, double> rootHashToReward;

        double bestReward = -1.0;
        std::string bestRewrite;
        long long fastestRewriteExecTimeNs = -1;

        // DEBUG instrumentation
        long long dbgIterations = 0;
        long long dbgExpandCalls = 0;
        long long dbgRootCandidatesSeen = 0;
        long long dbgConsistentTableSeen = 0;
        long long dbgCegisAttempts = 0;
        long long dbgCegisPassed = 0;
        long long dbgHashComputations = 0;
        long long dbgEmptyComboSets = 0;
        long long dbgComboAttempts = 0;
        long long dbgComboErrorOrBottom = 0;
        long long dbgComboSuccess = 0;
        emhash8hashmap::HashMap<size_t, std::pair<long long, long long>>
            dbgSymbolOutcomes;  // symbolIndex -> (attempts, errorOrBottom)
        emhash8hashmap::HashMap<size_t, long long> dbgEmptySetSymbol;  // symbolIndex -> count of empty-combo-set occurrences

        constexpr double kExplorationConstant = 1.414;

        // ---- parameter-like state enumeration (no MCTS/iteration dependency) ----
        //
        // A state is "parameter-like" if every admissible way to fill it
        // terminates immediately -- e.g. NtColumnIndex (one of a fixed,
        // small number of OpIdentity(ConstColumnIndex(i)) choices),
        // NtFilterConstantIndex, or the base-table selector NtQuery -- as
        // opposed to a genuinely structural state like NtQuery_182, whose
        // alternatives (LeftJoin / Distinct / OrderByLimit / ...) are
        // different program shapes that each open up further substructure.
        // Parameter-like positions are cheap and bounded (a handful of
        // columns/constants/tables), so they're enumerated exhaustively,
        // once, independent of any specific MCTS iteration or skeleton --
        // unlike structural positions, which stay driven by select()/
        // playout()'s UCT-guided sampling (the whole point of using MCTS
        // instead of exhaustively enumerating everything, as
        // synthesize_with_slicing.hpp already does).
        auto isParameterLike = [&](IDType state) {
            auto& transitions = fta.getTransitionsByHead(state);
            if (transitions.empty()) {
                return true;  // genuine leaf
            }
            for (IDType t : transitions) {
                if (sql::semantics::transformers::sql_operations::
                        isOperatorGeneratingTables(
                            aftaTransitionManager.read(t).getSymbol())) {
                    return false;
                }
            }
            return true;
        };

        // Builds the one concrete realization of a genuine leaf state
        // (constant or variable) -- a pure function of the fixed benchmark,
        // with no dependency on which MCTS iteration or skeleton is asking.
        auto buildLeafCandidate = [&](IDType abstrTail) -> ConcreteCandidate {
            auto& stateEntity = aftaStateManager.read(abstrTail);
            IDType concreteStateID;
            TermType term;
            if (stateEntity.getSymbol().isConstant()) {
                sql::semantics::values::ConcreteValue::VariantType value =
                    sql::UnAbstractedTerm((sql::Term)(stateEntity.getSymbol()));
                concreteStateID = cftaStateManager.create(
                    stateEntity.getSymbol(),
                    sql::semantics::values::ConcreteValue(value), stateEntity.getCost());
                cfta.addState(concreteStateID);
                term = (sql::Term)(stateEntity.getSymbol());
            } else if (std::holds_alternative<sql::VarTable>(
                           stateEntity.getSymbol().getSymbol())) {
                auto varSymbolIdx =
                    std::get<sql::VarTable>(stateEntity.getSymbol().getSymbol())
                        .getIndex();
                concreteStateID = cftaStateManager.create(
                    stateEntity.getSymbol(),
                    sql::semantics::values::ConcreteValue(
                        benchmark.getIOPairs()[0].getInput()[varSymbolIdx]),
                    stateEntity.getCost());
                cfta.addState(concreteStateID);
                term = TermType(stateEntity.getSymbol(), 0);
            } else if (std::holds_alternative<sql::VarFilterConst>(
                           stateEntity.getSymbol().getSymbol())) {
                auto filterConstIdx = std::get<sql::VarFilterConst>(
                                          stateEntity.getSymbol().getSymbol())
                                          .getIndex();
                auto leafTerm =
                    sql::Term((sql::Symbol)sql::VarFilterConst(filterConstIdx));
                concreteStateID = cftaStateManager.create(
                    stateEntity.getSymbol(),
                    (sql::semantics::values::ConcreteValue)sql::UnAbstractedTerm(
                        leafTerm),
                    stateEntity.getCost());
                cfta.addState(concreteStateID);
                term = leafTerm;
            } else {
                throw foresighter::common::InvalidArgument(
                    "buildLeafCandidate: leaf state " + std::to_string(abstrTail) +
                    " has unrecognized symbol variant index " +
                    std::to_string(stateEntity.getSymbol().getSymbol().index()));
            }
            return {concreteStateID, term};
        };

        // Persistent, MCTS-independent cache: every concrete filling of a
        // parameter-like state, computed exhaustively once and reused by
        // every iteration forever (unlike hashToConcreteCandidates below,
        // which is scoped to specific MCTS-sampled structural choices).
        emhash8hashmap::HashMap<IDType, std::vector<ConcreteCandidate>> stateEnumeration;

        std::function<std::vector<ConcreteCandidate>(IDType)> enumerateAll =
            [&](IDType state) -> std::vector<ConcreteCandidate> {
            if (auto it = stateEnumeration.find(state); it != stateEnumeration.end()) {
                return it->second;
            }
            // Placeholder to fail fast rather than infinitely recurse if the
            // grammar ever has a cycle among parameter-like states.
            stateEnumeration[state] = {};

            auto& transitions = fta.getTransitionsByHead(state);
            if (transitions.empty()) {
                std::vector<ConcreteCandidate> result = {buildLeafCandidate(state)};
                stateEnumeration[state] = result;
                return result;
            }

            std::vector<ConcreteCandidate> allResults;
            for (IDType t : transitions) {
                auto& transitionEntity = aftaTransitionManager.read(t);
                if (sql::semantics::transformers::sql_operations::
                        isOperatorGeneratingTables(transitionEntity.getSymbol())) {
                    // A parameter-like state, by definition, has no
                    // table-generating incoming transitions -- but guard
                    // against it anyway rather than silently including one.
                    continue;
                }
                auto& tails = transitionEntity.getTails();
                std::vector<std::vector<ConcreteCandidate>> candidatesPerTail;
                candidatesPerTail.reserve(tails.size());
                for (IDType tail : tails) {
                    if (!isParameterLike(tail)) {
                        throw foresighter::common::InvalidArgument(
                            "enumerateAll: parameter-like state " +
                            std::to_string(state) + " has a non-parameter-like "
                            "tail " + std::to_string(tail));
                    }
                    candidatesPerTail.push_back(enumerateAll(tail));
                }
                guardComboCount(candidatesPerTail);
                auto combos = foresighter::common::cartesianProduct(candidatesPerTail);
                auto& headStateEntity = aftaStateManager.read(transitionEntity.getHead());
                for (auto& combo : combos) {
                    checkTimeoutOrThrow();
                    std::vector<sql::semantics::values::ConcreteValue> argConcreteValues;
                    argConcreteValues.reserve(combo.size());
                    for (auto& c : combo) {
                        argConcreteValues.push_back(
                            cftaStateManager.read(c.state).getValue());
                    }
                    auto returnValueAfterTransform = interpreter.transform(
                        transitionEntity.getSymbol(), argConcreteValues,
                        transitionEntity.getOrder());
                    if (std::holds_alternative<sql::Error>(
                            returnValueAfterTransform.getValue()) ||
                        std::holds_alternative<sql::Bottom>(
                            returnValueAfterTransform.getValue())) {
                        continue;
                    }
                    std::vector<IDType> argConcreteStates;
                    std::vector<TermType> childTerms;
                    argConcreteStates.reserve(combo.size());
                    childTerms.reserve(combo.size());
                    for (auto& c : combo) {
                        argConcreteStates.push_back(c.state);
                        childTerms.push_back(c.term);
                    }
                    IDType concreteStateID = cftaStateManager.create(
                        headStateEntity.getSymbol(), returnValueAfterTransform,
                        headStateEntity.getCost());
                    cfta.addState(concreteStateID);
                    IDType concreteTransitionID = cftaTransitionManager.create(
                        concreteStateID, transitionEntity.getSymbol(), argConcreteStates,
                        transitionEntity.getCost(), transitionEntity.getOrder());
                    cfta.addTransition(concreteTransitionID);
                    TermType term = termExtractor.wrapInnerTermWithProjection(
                        TermType(transitionEntity.getSymbol(), childTerms,
                                 transitionEntity.getCost()),
                        transitionEntity.getOrder());
                    allResults.push_back({concreteStateID, term});
                }
            }
            stateEnumeration[state] = allResults;
            return allResults;
        };

        // ---- helpers -------------------------------------------------

        auto checkIfFullyExpanded = [&](IDType abstrNode, size_t pathHash,
                                         std::vector<std::pair<IDType, size_t>>&
                                             unvisitedBorder) {
            auto& transitions = fta.getTransitionsByHead(abstrNode);
            if (transitions.empty()) {
                // leaf: nothing to expand, handled directly during concretize()
                return false;
            }
            bool isFullyExpanded = true;
            for (IDType abstrTransition : transitions) {
                unsigned long long numVisits = 0;
                if (auto it = transitionStats.find(abstrTransition);
                    it != transitionStats.end()) {
                    if (auto it2 = it->second.find(pathHash); it2 != it->second.end()) {
                        numVisits = it2->second.numVisits;
                    }
                }
                if (numVisits == 0) {
                    isFullyExpanded = false;
                    // abstrTransition's head is abstrNode by construction
                    // (it came from fta.getTransitionsByHead(abstrNode)), so
                    // there's no need to also store the node here --
                    // expandAndSimulate() below recovers it by reading the
                    // transition's own head.
                    unvisitedBorder.push_back({abstrTransition, pathHash});
                }
            }
            return isFullyExpanded;
        };

        // The parent's visit count is the node's own visit count in this
        // pathHash context: the sum of its outgoing transitions' visits under
        // the same pathHash (every iteration that passes through the node
        // backpropagates to exactly one of them). Reading it off the parent
        // transition's stats instead would need the *parent's* pathHash, not
        // this node's -- looking it up under this node's pathHash finds an
        // entry that's never backpropagated to, i.e. 0 visits, making
        // log(0) = -inf and every UCT score NaN.
        auto calcUCT = [&](double parentVisits, IDType childAbstrTransition,
                            size_t pathHash) {
            auto& childStats = transitionStats[childAbstrTransition][pathHash];

            // It's guaranteed that childStats.numVisits > 0 because calcUCT() is
            // only called on fully-expanded nodes
            return childStats.totalReward / (double)childStats.numVisits +
                   kExplorationConstant *
                       std::sqrt(std::log(parentVisits) / (double)childStats.numVisits);
        };

        auto bestUCT = [&](IDType abstrNode, size_t pathHash) {
            auto& transitions = fta.getTransitionsByHead(abstrNode);
            double parentVisits = 0;
            for (IDType abstrTransition : transitions) {
                parentVisits += (double)transitionStats[abstrTransition][pathHash].numVisits;
            }

            double maxUCT = -std::numeric_limits<double>::infinity();
            IDType selected = transitions.front();
            for (IDType abstrTransition : transitions) {
                double uct = calcUCT(parentVisits, abstrTransition, pathHash);
                if (uct > maxUCT) {
                    maxUCT = uct;
                    selected = abstrTransition;
                }
            }
            return selected;
        };

        // Walks down from `rootState` selecting, at every already-fully-
        // expanded node, the child transition with the best UCT score, until
        // it reaches the search frontier (a leaf, or a node with at least one
        // never-tried transition). `path` collects the transitions chosen
        // along the way (paired with the pathHash context they were chosen
        // under, for backpropagation); `unvisitedBorder` collects every
        // untried (transition, node, pathHash) at the frontier;
        // `notFullyExpandedNodes` collects every frontier node (leaves and
        // partially-expanded interior nodes alike).
        auto select = [&](IDType rootState,
                           std::vector<std::pair<IDType, size_t>>& path,
                           emhash8hashmap::HashMap<IDType, IDType>& headToTransition,
                           std::vector<std::pair<IDType, size_t>>& unvisitedBorder,
                           std::vector<IDType>& notFullyExpandedNodes,
                           emhash8hashset::HashSet<IDType>& seenNodeSet) {
            // (node, parentTransition-or-sentinel, pathHash-so-far)
            std::deque<std::tuple<IDType, IDType, size_t>> nodeQueue;
            constexpr IDType kNoParentTransition = (IDType)-1;
            nodeQueue.push_back({rootState, kNoParentTransition, (size_t)0});
            seenNodeSet.insert(rootState);

            while (!nodeQueue.empty()) {
                auto [node, parentTransition, pathHash] = nodeQueue.front();
                nodeQueue.pop_front();

                if (checkIfFullyExpanded(node, pathHash, unvisitedBorder)) {
                    IDType selectedAbstrTransition =
                        bestUCT(node, pathHash);
                    size_t nextPathHash = pathHash;
                    foresighter::common::hashCombine(nextPathHash, selectedAbstrTransition);

                    for (IDType abstrTail :
                         aftaTransitionManager.read(selectedAbstrTransition).getTails()) {
                        if (seenNodeSet.find(abstrTail) == seenNodeSet.end()) {
                            nodeQueue.push_back(
                                {abstrTail, selectedAbstrTransition, nextPathHash});
                            seenNodeSet.insert(abstrTail);
                        }
                    }

                    path.push_back({selectedAbstrTransition, pathHash});
                    headToTransition[node] = selectedAbstrTransition;
                } else {
                    notFullyExpandedNodes.push_back(node);
                }
            }
        };

        // Randomly extends `backtrackPath` from `tails` down to leaves,
        // used both to finish playing out a freshly-expanded subtree and to
        // fill in the other not-yet-explored siblings encountered by select().
        auto playout = [&](emhash8hashmap::HashMap<IDType, IDType>& headToTransition,
                            const std::vector<IDType>& tails,
                            emhash8hashset::HashSet<IDType>& seenNodeSet) {
            std::deque<IDType> tailQueue(tails.begin(), tails.end());

            while (!tailQueue.empty()) {
                IDType node = tailQueue.front();
                tailQueue.pop_front();

                auto& transitions = fta.getTransitionsByHead(node);
                if (transitions.empty()) {
                    continue;  // leaf, nothing to choose
                }

                IDType randomAbstrTransition =
                    transitions[std::uniform_int_distribution<size_t>(
                        0, transitions.size() - 1)(rng)];

                for (IDType abstrTail :
                     aftaTransitionManager.read(randomAbstrTransition).getTails()) {
                    if (seenNodeSet.find(abstrTail) == seenNodeSet.end()) {
                        tailQueue.push_back(abstrTail);
                        seenNodeSet.insert(abstrTail);
                    }
                }

                headToTransition[node] = randomAbstrTransition;
            }
        };

        // Concretizes every candidate reachable through this iteration's own
        // backtrackPath, bottom-up. backtrackPath is the reverse of a
        // topological order (deepest transitions were pushed last during
        // select()/playout()), so iterating it back-to-front guarantees every
        // tail has already been resolved by the time we process a
        // transition that depends on it.
        //
        // abstrTailToHash is local to this call (rebuilt fresh every
        // iteration, since a different iteration's backtrackPath can route
        // through the same abstract head via a different incoming
        // transition): it records which hashToConcreteCandidates bucket each
        // abstract state visited by *this* backtrackPath resolves to. Every
        // lookup below goes through this map or through the pool entry it
        // just pointed at -- never through fta.getTransitionsByHead -- so
        // the result is always faithful to backtrackPath specifically, even
        // though hashToConcreteCandidates itself is shared across all
        // iterations.
        auto concretize = [&](emhash8hashmap::HashMap<IDType, IDType>& headToTransition, IDType rootState)
            -> size_t {
            if (headToTransition.empty()) {
                return 0;
            }

            emhash8hashmap::HashMap<IDType, size_t> abstrTailToHash;

            // backtrackPath's array order reflects the order select()/
            // playout() *discovered* transitions, not a valid topological
            // (parent-before-child) order: select()'s BFS can mark a state
            // "seen" (as a tail of one frontier node's chosen transition)
            // well before that state's *own* transition is chosen, and
            // expandAndSimulate() resolves every frontier node via an
            // independent playout() call, appending to the same
            // backtrackPath in whatever order it iterates
            // notFullyExpandedNodes -- so one frontier node's chosen
            // transition can need, as a tail, a state whose own producing
            // transition sits *later* in the array. So resolve every
            // position by its actual head/tail dependencies via memoized
            // recursion, driven by this map, rather than assuming a single
            // reverse linear pass is already in dependency order.

            // Note: genuine leaves are parameter-like by definition
            // (isParameterLike returns true when a state has zero
            // admissible transitions), so they're always routed through
            // enumerateAll()/buildLeafCandidate() at the outer scope, never
            // through resolve() below.

            // Resolves abstrState's hash bucket, recursively resolving its
            // tails first if it's produced by a transition in
            // backtrackPath, or as a leaf otherwise -- memoized via
            // abstrTailToHash so shared tails (e.g. a state referenced
            // twice by the same transition, or by two different
            // transitions in this same backtrackPath) are only resolved
            // once.
            std::function<size_t(IDType)> resolve = [&](IDType abstrState) -> size_t {
                if (auto it = abstrTailToHash.find(abstrState);
                    it != abstrTailToHash.end()) {
                    return it->second;
                }

                auto headIt = headToTransition.find(abstrState);
                if (headIt == headToTransition.end()) {
                    // Every non-parameter-like (structural) state reachable
                    // from the root is visited by select()/playout() and
                    // given a headToTransition entry; parameter-like states
                    // (including all genuine leaves) never reach resolve()
                    // at all -- the tail-gathering loop below routes them
                    // through enumerateAll() instead. Reaching here means
                    // that invariant broke.
                    throw foresighter::common::InvalidArgument(
                        "resolve: structural state " + std::to_string(abstrState) +
                        " has no headToTransition entry");
                }

                auto& transitionEntity = aftaTransitionManager.read(headIt->second);
                auto& tails = transitionEntity.getTails();

                size_t hash = 0;
                foresighter::common::hashCombine(hash, transitionEntity.getSymbol());
                for (int orderIdx : transitionEntity.getOrder()) {
                    foresighter::common::hashCombine(hash, orderIdx);
                }

                std::vector<std::vector<ConcreteCandidate>> candidatesPerTail;
                candidatesPerTail.reserve(tails.size());
                for (IDType abstrTail : tails) {
                    if (isParameterLike(abstrTail)) {
                        // Column index / filter constant / input table
                        // choice, etc.: already fully enumerated,
                        // independent of this (or any) iteration -- use the
                        // whole set directly instead of going through
                        // headToTransition/resolve at all.
                        candidatesPerTail.push_back(enumerateAll(abstrTail));
                        foresighter::common::hashCombine(hash, abstrTail);
                    }
                    else {
                        size_t tailHash = resolve(abstrTail);
                        foresighter::common::hashCombine(hash, tailHash);
                        candidatesPerTail.push_back(hashToConcreteCandidates[tailHash]);
                    }
                }

                if (hashToConcreteCandidates.find(hash) ==
                    hashToConcreteCandidates.end()) {
                    dbgHashComputations++;
                    size_t dbgSymIdx = transitionEntity.getSymbol().getSymbol().index();
                    guardComboCount(candidatesPerTail);
                    auto combos =
                        foresighter::common::cartesianProduct(candidatesPerTail);
                    if (combos.empty() && !tails.empty()) {
                        dbgEmptyComboSets++;
                        dbgEmptySetSymbol[dbgSymIdx]++;
                    }
                    std::vector<ConcreteCandidate> results;
                    results.reserve(combos.size());

                    auto& headStateEntity =
                        aftaStateManager.read(transitionEntity.getHead());

                    for (auto& combo : combos) {
                        checkTimeoutOrThrow();
                        dbgComboAttempts++;
                        dbgSymbolOutcomes[dbgSymIdx].first++;
                        std::vector<sql::semantics::values::ConcreteValue>
                            argConcreteValues;
                        argConcreteValues.reserve(combo.size());
                        for (auto& c : combo) {
                            argConcreteValues.push_back(
                                cftaStateManager.read(c.state).getValue());
                        }
                        auto returnValueAfterTransform = interpreter.transform(
                            transitionEntity.getSymbol(), argConcreteValues,
                            transitionEntity.getOrder());
                        if (std::holds_alternative<sql::Error>(
                                returnValueAfterTransform.getValue()) ||
                            std::holds_alternative<sql::Bottom>(
                                returnValueAfterTransform.getValue())) {
                            dbgComboErrorOrBottom++;
                            dbgSymbolOutcomes[dbgSymIdx].second++;
                            // this combination doesn't type-check / evaluate;
                            // skip it, other combinations may still succeed.
                            continue;
                        }
                        dbgComboSuccess++;

                        std::vector<IDType> argConcreteStates;
                        argConcreteStates.reserve(combo.size());
                        std::vector<TermType> childTerms;
                        childTerms.reserve(combo.size());
                        for (auto& c : combo) {
                            argConcreteStates.push_back(c.state);
                            childTerms.push_back(c.term);
                        }

                        IDType concreteStateID = cftaStateManager.create(
                            headStateEntity.getSymbol(), returnValueAfterTransform,
                            headStateEntity.getCost());
                        cfta.addState(concreteStateID);
                        IDType concreteTransitionID = cftaTransitionManager.create(
                            concreteStateID, transitionEntity.getSymbol(),
                            argConcreteStates, transitionEntity.getCost(),
                            transitionEntity.getOrder());
                        cfta.addTransition(concreteTransitionID);

                        TermType term = termExtractor.wrapInnerTermWithProjection(
                            TermType(transitionEntity.getSymbol(), childTerms,
                                     transitionEntity.getCost()),
                            transitionEntity.getOrder());

                        results.push_back({concreteStateID, term});
                    }

                    hashToConcreteCandidates[hash] = results;
                }

                abstrTailToHash[abstrState] = hash;
                return hash;
            };

            // rootState is already a state id (the root state select()
            // started from), not a transition -- resolve it directly.
            size_t rootHash = resolve(rootState);
            return rootHash;
        };

        // Verifies every candidate in rootCandidates -- all of them reached
        // solely by walking this iteration's own backtrackPath, see
        // concretize() above -- against every IO example via CEGIS, times
        // the ones that pass, and returns the average simulation reward
        // across every verified (candidate, output-mapping) pair (0 if none
        // verified) -- this average is what gets backpropagated.
        auto extract = [&](size_t rootHash) {
            if (auto it = rootHashToReward.find(rootHash); it != rootHashToReward.end()) {
                return it->second;
            }

            std::vector<ConcreteCandidate>& rootCandidates = hashToConcreteCandidates[rootHash];
            double rewardSum = 0.0;
            int numVerified = 0;
            dbgRootCandidatesSeen += (long long)rootCandidates.size();

            for (auto& candidate : rootCandidates) {
                // Unlike concretize()'s combo loop (cheap in-process
                // SQLite transforms, checked only every
                // kTimeoutCheckInterval combos to avoid clock-call
                // overhead), each iteration here can run a CEGIS pass plus
                // several real round-trips to an external Postgres
                // instance for timing -- individually far more expensive
                // than a clock check, and with rootCandidates sometimes
                // numbering in the thousands (the same pooled hash bucket
                // that gives concretize() its multiplicity). Without a
                // check here, a single expandAndSimulate() call can run
                // for minutes past the configured timeout before this loop
                // ever gets a chance to unwind.
                if (!areResourcesRemaining()) {
                    throw MCTSTimeoutSignal{};
                }
                auto& concreteStateEntity = cftaStateManager.read(candidate.state);
                if (!std::holds_alternative<sql::ConcreteTable>(
                        concreteStateEntity.getValue().getValue())) {
                    continue;
                }
                auto consistentMappings =
                    std::get<sql::ConcreteTable>(concreteStateEntity.getValue().getValue())
                        .getConsistentMappingsUnderProjection(
                            benchmark.getIOPairs()[0].getOutput());
                if (consistentMappings.empty()) {
                    continue;
                }
                dbgConsistentTableSeen++;

                auto sql = interpreter.getSQLFromTerm(candidate.term);
                for (auto& mapping : consistentMappings) {
                    if (!areResourcesRemaining()) {
                        throw MCTSTimeoutSignal{};
                    }
                    auto sqlWrapped = wrapSQLWithFinalProjection(sql, mapping);

                    dbgCegisAttempts++;
                    auto cegisRunnerResult = cegisRunner.run(sqlWrapped);
                    if (cegisRunnerResult.status != -1) {
                        // fails verification against at least one example
                        continue;
                    }
                    dbgCegisPassed++;

                    // Mirrors synthesize_with_slicing.hpp's own
                    // |sql-solution-found| logging (status == -1 branch) so
                    // the two searches' "time to first fully-verified
                    // solution" can be compared directly from their logs on
                    // the same elapsed-time basis (both measured from their
                    // own search-start timestamp).
                    auto solutionFoundElapsedMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::high_resolution_clock::now() - searchStart)
                            .count();
                    SPDLOG_LOGGER_TRACE(logger, "|mcts-solution-found|{}|{}",
                                        solutionFoundElapsedMs, sqlWrapped);

                    // Always attempt to time a verified rewrite, exactly
                    // like synthesize_with_slicing.hpp does (its own
                    // |rewrite-timing-ns| call is unconditional, not gated
                    // on the ground truth having been timed successfully)
                    // -- medianExecTimeNs() itself is a cheap no-op
                    // (returns nullopt immediately) when the QueryTimer
                    // isn't connected, so this costs nothing when timing is
                    // disabled outright. Gating this on origTimeNs > 0, as
                    // before, meant a benchmark whose ground-truth query
                    // happened to fail/timeout during its own timing never
                    // got fastestRewriteExecTimeNs populated at all, even
                    // though a correct rewrite existed and could have been
                    // timed.
                    double reward = 1.0;
                    auto execNs = queryTimer.medianExecTimeNs(sqlWrapped);
                    if (execNs && *execNs > 0) {
                        if (fastestRewriteExecTimeNs < 0 ||
                            *execNs < fastestRewriteExecTimeNs) {
                            fastestRewriteExecTimeNs = *execNs;
                        }
                        if (origTimeNs > 0) {
                            reward = (double)origTimeNs / (double)(*execNs) - 1.0;
                        }
                    } else if (origTimeNs > 0) {
                        // timing was expected to work (ground truth timed
                        // fine) but this rewrite's own timing failed --
                        // treat as a worse-than-baseline result, not a free
                        // reward=1.0, matching the original gated behavior.
                        reward = 0.0;
                    }

                    rewardSum += reward;
                    numVerified++;
                    if (reward > bestReward) {
                        bestReward = reward;
                        bestRewrite = sqlWrapped;
                    }
                }
            }

            double finalReward = numVerified > 0 ? rewardSum / numVerified : 0.0;

            rootHashToReward[rootHash] = finalReward;

            return finalReward;
        };

        // Expands one untried transition at the search frontier (chosen
        // uniformly at random among unvisitedBorder by the caller), plays
        // out every other frontier node, concretizes the whole sampled
        // derivation, and returns its simulation reward.
        auto expandAndSimulate = [&](std::vector<std::pair<IDType, size_t>>& path,
                                      emhash8hashmap::HashMap<IDType, IDType>& headToTransition,
                                      IDType rootState,
                                      IDType abstrTransitionToExpand,
                                      size_t pathHashAtExpansion,
                                      const std::vector<IDType>& notFullyExpandedNodes,
                                      emhash8hashset::HashSet<IDType>& seenNodeSet) {
            IDType abstrNodeToExpand =
                aftaTransitionManager.read(abstrTransitionToExpand).getHead();
            for (IDType notFullyExpandedNode : notFullyExpandedNodes) {
                if (notFullyExpandedNode == abstrNodeToExpand) {
                    path.push_back({abstrTransitionToExpand, pathHashAtExpansion});
                    headToTransition[abstrNodeToExpand] = abstrTransitionToExpand;

                    auto& tails =
                        aftaTransitionManager.read(abstrTransitionToExpand).getTails();
                    for (IDType abstrTail : tails) {
                        seenNodeSet.insert(abstrTail);
                    }

                    playout(headToTransition, tails, seenNodeSet);
                } else {
                    std::vector<IDType> playoutTails = {notFullyExpandedNode};
                    playout(headToTransition, playoutTails, seenNodeSet);
                }
            }

            // Pass in rootState because abstrTailToHash is local to concretize()
            size_t rootHash = concretize(headToTransition, rootState);
            return extract(rootHash);
        };

        auto backpropagate = [&](const std::vector<std::pair<IDType, size_t>>& path,
                                  double reward) {
            for (auto& [transition, pathHash] : path) {
                auto& stats = transitionStats[transition][pathHash];
                stats.totalReward += reward;
                stats.numVisits++;
            }
        };

        // ---- MCTS driver loop -----------------------------------------

        while (areResourcesRemaining()) {
            dbgIterations++;

            // Pick a candidate output/root state each iteration via the same
            // UCT policy used deeper in the tree, treating the choice among
            // roots as children of a single virtual root node.
            double maxRootUCT = -1;
            IDType rootState = (IDType)-1;
            std::vector<IDType> unvisitedRoots;
            
            for (IDType candidate : targetStates) {
                auto& stats = rootStats[candidate];

                if (stats.numVisits == 0) {
                    unvisitedRoots.push_back(candidate);
                }

                if (unvisitedRoots.empty()) {
                    double parentVisits = totalRootVisits;
                    double uct = stats.totalReward / (double)stats.numVisits +
                          kExplorationConstant *
                              std::sqrt(std::log(parentVisits) / (double)stats.numVisits);
                    
                    if (uct > maxRootUCT) {
                        maxRootUCT = uct;
                        rootState = candidate;
                    }
                }
            }

            if (!unvisitedRoots.empty()) {
                rootState = unvisitedRoots[std::uniform_int_distribution<size_t>(
                            0, unvisitedRoots.size() - 1)(rng)];
            }

            std::vector<std::pair<IDType, size_t>> path;
            emhash8hashmap::HashMap<IDType, IDType> headToTransition;
            std::vector<std::pair<IDType, size_t>> unvisitedBorder;
            std::vector<IDType> notFullyExpandedNodes;
            emhash8hashset::HashSet<IDType> seenNodeSet;

            select(rootState, path, headToTransition, unvisitedBorder, notFullyExpandedNodes, seenNodeSet);

            double reward;
            try {
                if (unvisitedBorder.empty()) {
                    // Every transition select() walked through (in every
                    // pathHash context it walked through) already has at
                    // least one visit, so it reached genuine leaves via
                    // pure UCT with nothing left to randomly expand this
                    // iteration -- that does NOT mean every full
                    // root-to-leaf combination has been tried, since
                    // bestUCT's pick at any node can still shift as visit
                    // counts/rewards evolve. Play out whatever frontier
                    // nodes remain (genuine leaves only, at this point --
                    // a no-op) and concretize/extract the fully
                    // UCT-determined skeleton directly; rootHashToReward
                    // makes re-tracing an already-seen exact combination
                    // cost only a hash lookup, not a repeated CEGIS/timing
                    // pass.
                    // for (IDType notFullyExpandedNode : notFullyExpandedNodes) {
                    //     std::vector<IDType> playoutTails = {notFullyExpandedNode};
                    //     playout(headToTransition, playoutTails, seenNodeSet);
                    // }
                    size_t rootHash =
                        concretize(headToTransition, rootState);
                    reward = extract(rootHash);
                } else {
                    dbgExpandCalls++;
                    auto& [transitionToExpand, pathHashAtExpansion] =
                        unvisitedBorder[std::uniform_int_distribution<size_t>(
                            0, unvisitedBorder.size() - 1)(rng)];
                    reward = expandAndSimulate(path, headToTransition, rootState,
                                                transitionToExpand, pathHashAtExpansion,
                                                notFullyExpandedNodes, seenNodeSet);
                }
            } catch (const MCTSTimeoutSignal&) {
                // Either the overall search timeout was hit mid-iteration,
                // or a single position's parameter space was too large to
                // enumerate safely -- either way, this iteration produced
                // no usable result. Skip backpropagating anything for it
                // and let the next while-condition check decide whether to
                // keep going (there may still be time for other, cheaper
                // positions) or stop.
                continue;
            }

            backpropagate(path, reward);
            auto& rStats = rootStats[rootState];
            rStats.totalReward += reward;
            rStats.numVisits++;
            totalRootVisits++;
        }

        SPDLOG_LOGGER_TRACE(
            logger,
            "|DEBUG-mcts-stats|iterations={}|expandCalls={}|targetStates={}|"
            "rootCandidatesSeen={}|consistentTableSeen={}|cegisAttempts={}|"
            "cegisPassed={}|hashComputations={}|emptyComboSets={}|"
            "comboAttempts={}|comboErrorOrBottom={}|comboSuccess={}",
            dbgIterations, dbgExpandCalls, targetStates.size(),
            dbgRootCandidatesSeen, dbgConsistentTableSeen, dbgCegisAttempts,
            dbgCegisPassed, dbgHashComputations, dbgEmptyComboSets,
            dbgComboAttempts, dbgComboErrorOrBottom, dbgComboSuccess);
        for (auto& [symIdx, outcomes] : dbgSymbolOutcomes) {
            SPDLOG_LOGGER_TRACE(
                logger, "|DEBUG-symbol-outcome|symIdx={}|attempts={}|errorOrBottom={}",
                symIdx, outcomes.first, outcomes.second);
        }
        for (auto& [symIdx, count] : dbgEmptySetSymbol) {
            SPDLOG_LOGGER_TRACE(
                logger, "|DEBUG-empty-set-symbol|symIdx={}|count={}", symIdx, count);
        }

        // Mirrors synthesize_with_slicing.hpp's own end-of-benchmark stdout
        // summary so the two searches' best-found-rewrite quality can be
        // compared directly (same fields, same units).
        std::cout << "\nBenchmark: " << benchmark.getName()
                  << "\nOriginal Exec (ms, median): "
                  << (origTimeNs > 0 ? origTimeNs / 1e6 : -1.0)
                  << "\nBest Rewrite Exec (ms, median): "
                  << (fastestRewriteExecTimeNs > 0
                          ? fastestRewriteExecTimeNs / 1e6
                          : -1.0)
                  << "\nSpeedup (orig / best): "
                  << ((origTimeNs > 0 && fastestRewriteExecTimeNs > 0)
                          ? (double)origTimeNs / (double)fastestRewriteExecTimeNs
                          : -1.0)
                  << "\nBest Rewrite SQL: "
                  << (bestRewrite.empty() ? "<none found>" : bestRewrite)
                  << std::endl;

        queryTimer.dropSchema();
        return bestRewrite;
    }

private:
    std::shared_ptr<spdlog::logger> logger;
};

}  // namespace fixedroot_variant
}  // namespace foresighter::implementation::algorithms
