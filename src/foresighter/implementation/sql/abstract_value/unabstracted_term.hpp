/**
* @file unabstracted_term.hpp
* @brief 
* @author Rui Dong
* @date 4/23/24
*/

#pragma once
#include <cereal/access.hpp>
#include <foresighter/implementation/basics/term.hpp>
#include <foresighter/implementation/sql/symbol.hpp>
#include "foresighter/implementation/basics/lattice.hpp"

namespace foresighter::implementation::sql {
class UnAbstractedTerm {
  using Term = foresighter::implementation::basics::Term<
      foresighter::implementation::sql::Symbol>;

 public:
  UnAbstractedTerm() = default;

  explicit UnAbstractedTerm(Term term) : term(std::move(term)) {}

  [[nodiscard]] const Term& getTerm() const { return term; }

  [[nodiscard]] std::string toString() const { return term.toString(); }

  bool operator==(const UnAbstractedTerm& other) const {
    return term == other.term;
  }

 private:
  Term term;
  template <typename U>
  friend class foresighter::implementation::basics::Lattice;
  friend class cereal::access;

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<UnAbstractedTerm>(other)) {
      return term == std::get<UnAbstractedTerm>(other).getTerm();
    }
    return false;
  }

  template <typename T>
  [[nodiscard]] bool impliedBy(const T& other) const noexcept {
    return false;
  }

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(term);
  }
};
}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::UnAbstractedTerm> {
  size_t operator()(const foresighter::implementation::sql::UnAbstractedTerm&
                        un_abstracted_term) const {
    return hash<foresighter::implementation::basics::Term<
        foresighter::implementation::sql::Symbol>>()(
        un_abstracted_term.getTerm());
  }
};
}  // namespace std
