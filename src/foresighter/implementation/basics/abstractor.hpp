#pragma once
/**
 * @file abstractor.hpp
 * @brief Abstraction function alpha: maps concrete values to abstract values.
 *
 * Template base class Abstractor<BeforeValueType, AfterValueType>. Subclasses
 * implement abstract() to lift concrete domain values into the abstract domain,
 * enabling the offline phase to reason over abstract semantics.
 */
#include <foresighter/common/exceptions.hpp>

namespace foresighter::implementation::basics {

template <class BeforeValueType, class AfterValueType>
class Abstractor {
 public:
  AfterValueType abstract(const BeforeValueType& value) {
    throw common::NotImplementedException();
  }
};
}  // namespace foresighter::implementation::basics
