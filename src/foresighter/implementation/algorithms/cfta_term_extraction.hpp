/**
 * @file cfta_term_extraction.hpp
 * @author Rui Dong
 * @brief algorithms for term extraction once final states are reached
 * @date 2024-12-02
 *
 */
#pragma once
#include <spdlog/logger.h>
#include <foresighter/common/hashmap.hpp>
#include <foresighter/common/logger.hpp>
#include <memory>

#include <foresighter/common/hash.hpp>
#include <utility>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/implementation/basics/fta.hpp"
#include "foresighter/implementation/basics/term.hpp"
#include "foresighter/implementation/sql/abstract_value/unabstracted_term.hpp"
#include "foresighter/implementation/sql/symbol/const_column_index.hpp"
#include "foresighter/implementation/sql/symbol/op_proj_list.hpp"
#include "foresighter/implementation/sql/symbol/op_projection.hpp"

namespace foresighter::implementation::algorithms {

template <class TermType, class FTAType>
class FTATermExtractor {
  using IDType = foresighter::implementation::basics::IDType;
  using WeightType = foresighter::implementation::basics::WeightType;

 public:
  FTATermExtractor(const FTAType& fta, std::shared_ptr<spdlog::logger> logger)
      : fta(fta), logger(std::move(logger)) {}

  bool isOrderOrdered(const std::vector<int>& order) {
    for (int i = 0; i < order.size(); ++i) {
      if (order[i] != i) {
        return false;
      }
    }
    return true;
  }

  TermType getProjectionListTerm(const std::vector<int>& order) {
    std::vector<TermType> colIndices;
    colIndices.reserve(order.size());
    for (int i = 0; i < order.size(); ++i) {
      colIndices.push_back(TermType(
          typename TermType::SymbolType(sql::ConstColumnIndex(order[i])), 0));
    }
    return TermType(typename TermType::SymbolType(sql::OpProjList()),
                    colIndices, 0);
  }

  TermType wrapInnerTermWithProjection(const TermType& term,
                                       const std::vector<int>& order) {
    if (order.empty() || isOrderOrdered(order)) {
      return term;
    }
    return TermType(typename TermType::SymbolType(sql::OpProjection()),
                    {term, getProjectionListTerm(order)}, 0);
  }

  // Extracts all accepted terms rooted at `stateID`, but constrains the very
  // first (outermost) step of the extraction to go through `transitionID`
  // specifically, rather than trying every transition incoming to `stateID`.
  // This is used right after a new CFTA transition is built during
  // concretization: we already know which transition just produced this
  // state and only want terms derived via that specific transition, while
  // descendants (children states) are free to use any of their own incoming
  // transitions.
  std::vector<TermType>
  extractAcceptedTermsFromStateWithFirstTransitionEnforced(
      IDType stateID, IDType transitionID) {
    // since the CFTA is built bottom-up, every subtree has already been
    // visited by the time we reach this state, so it's safe to cache here.
    if (auto it = termExtractionCache.find({stateID, transitionID});
        it != termExtractionCache.end()) {
      return it->second;
    }

    //build the term from the transition
    auto& transitionEntity = fta.getTransitionManager().read(transitionID);
    auto& symbol = transitionEntity.getSymbol();
    auto& childrenStates = transitionEntity.getTails();

    std::vector<std::vector<TermType>> subTerms;
    for (auto& childState : childrenStates) {
      auto terms = extractAcceptedTermsFromState(childState);
      subTerms.push_back(terms);
    }

    //build a result by doing a cartesian product and apply the symbol
    foresighter::common::CartesianProductIterator cartesianIter(subTerms);

    std::vector<TermType> result;
    while (cartesianIter.hasNext()) {
      auto& combinedSubTerms = cartesianIter.next();
      auto newTerm = wrapInnerTermWithProjection(
          TermType(symbol, combinedSubTerms, transitionEntity.getCost()),
          transitionEntity.getOrder());

      result.push_back(newTerm);
    }

    //record in cache and return
    termExtractionCache[{stateID, transitionID}] = result;
    return result;
  }

  std::vector<TermType> extractAcceptedTermsFromState(IDType stateID) {
    //if it is an unabstracted term, we can directly extract the term
    auto& state = fta.getStateManager().read(stateID);
    if (std::holds_alternative<sql::UnAbstractedTerm>(
            state.getValue().getValue())) {
      return {std::get<sql::UnAbstractedTerm>(state.getValue().getValue())
                  .getTerm()};
    }

    // first, get all transitions incoming to the state
    auto& transitions = fta.getTransitionsByHead(stateID);

    //if this is a leaf with no incoming transitions, extract the bare symbol
    if (transitions.empty()) {
      return {TermType(state.getSymbol(), 0)};
    }

    //since programs extracted from different transitions are disjoint, there's no duplicate computation here
    std::vector<TermType> terms;
    for (auto& transition : transitions) {
      auto termsForThisTransition =
          extractAcceptedTermsFromStateWithFirstTransitionEnforced(stateID,
                                                                    transition);
      terms.insert(terms.end(), termsForThisTransition.begin(),
                   termsForThisTransition.end());
    }

    return terms;
  }


 private:
  struct StateIDTransitionIDPair {
    IDType stateID;
    IDType transitionID;

    bool operator==(const StateIDTransitionIDPair& other) const {
      return stateID == other.stateID && transitionID == other.transitionID;
    }
  };

  struct StateIDTransitionIDPairHash {
    std::size_t operator()(const StateIDTransitionIDPair& pair) const {
      size_t h = 0;
      foresighter::common::hashCombine(h, pair.stateID);
      foresighter::common::hashCombine(h, pair.transitionID);
      return h;
    }
  };

  using TermExtractionCache =
      emhash8hashmap::HashMap<StateIDTransitionIDPair, std::vector<TermType>,
                              StateIDTransitionIDPairHash>;
  std::shared_ptr<spdlog::logger>
      logger;          // this is for logging inside the class
  const FTAType& fta;  // the FTA to extract terms from
  TermExtractionCache termExtractionCache;
};

}  // namespace foresighter::implementation::algorithms
