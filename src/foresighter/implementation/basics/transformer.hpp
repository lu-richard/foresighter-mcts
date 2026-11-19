#pragma once
/**
 * @file transformer.hpp
 * @brief Abstract transformer interface for computing f-sharp: the abstract semantics of operators.
 *
 * Defines the Context class (binds symbols to values) and the Transformer base class.
 * Subclasses implement transform() to compute abstract values for each grammar operator,
 * used during offline FTA construction and online concrete evaluation.
 */
#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <vector>

namespace foresighter::implementation::basics {

template <class SymbolType, class ValueType>
class Context {
 public:
  void bind(SymbolType&& symbol, ValueType&& value) {
    map_.emplace(std::forward<SymbolType>(symbol),
                 std::forward<ValueType>(value));
  }

  void bind(const SymbolType& symbol, const ValueType& value) {
    map_.emplace(symbol, value);
  }

  const ValueType& get(const SymbolType& symbol) const {
    return map_.at(symbol);
  }

  bool contains(const SymbolType& symbol) const {
    return map_.contains(symbol);
  }

 private:
  emhash8hashmap::HashMap<SymbolType, ValueType> map_;
};

class Transformer {
 public:
  template <class SymbolType, class ValueType>
  ValueType transform(const SymbolType& symbol,
                      const std::vector<ValueType>& values,
                      const Context<SymbolType, ValueType>& context =
                          Context<SymbolType, ValueType>()) {
    throw common::NotImplementedException();
  }
};
}  // namespace foresighter::implementation::basics
