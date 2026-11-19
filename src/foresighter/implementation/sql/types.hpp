#pragma once
#include "foresighter/implementation/basics/fta.hpp"
#include "foresighter/implementation/basics/grammar.hpp"
#include "foresighter/implementation/basics/io_pair.hpp"
#include "foresighter/implementation/basics/production.hpp"
#include "foresighter/implementation/basics/symbol.hpp"
#include "foresighter/implementation/basics/term.hpp"
#include "foresighter/implementation/sql/concrete_value/concrete_table.hpp"
#include "foresighter/implementation/sql/semantics/values/offline_construction_value.hpp"
#include "symbol.hpp"

namespace foresighter::implementation::sql {
using Production = foresighter::implementation::basics::Production<Symbol>;
using Grammar = foresighter::implementation::basics::Grammar<Symbol>;
using Term = foresighter::implementation::basics::Term<Symbol>;
using OfflineFTA = foresighter::implementation::basics::FTA<
    Symbol, semantics::values::OfflineConstructionValue>;
using IOPairType =
    foresighter::implementation::basics::IOPair<std::vector<ConcreteTable>,
                                                ConcreteTable>;

}  // namespace foresighter::implementation::sql