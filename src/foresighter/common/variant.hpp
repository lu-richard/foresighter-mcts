/**
* @file variant.hpp
* @brief 
* @author Rui Dong
* @date 4/22/24
*/

#pragma once

#include <stdexcept>
#include <type_traits>
#include <variant>

namespace foresighter::common {
template <class... Args>
struct variant_cast_proxy {
  std::variant<Args...> v;

  template <class... ToArgs>
  operator std::variant<ToArgs...>() const {
    return std::visit(
        [](auto&& arg) -> std::variant<ToArgs...> {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::disjunction_v<std::is_same<ToArgs, T>...>) {
            return {std::forward<decltype(arg)>(arg)};
          } else {
            throw std::bad_variant_access();
          }
        },
        v);
  }
};

template <class... Args>
auto variant_cast(const std::variant<Args...>& v)
    -> variant_cast_proxy<Args...> {
  return {v};
}

template <typename... Ts>  // (7)
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace foresighter::common