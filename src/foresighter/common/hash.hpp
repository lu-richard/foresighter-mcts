#pragma once

#include <functional>

namespace foresighter::common {
inline void hashCombine([[maybe_unused]] size_t& Seed) {}

template <typename T, typename... RestType>
inline void hashCombine(size_t& Seed, const T& v, RestType... Rest) {
  std::hash<T> Hasher;
  Seed ^= Hasher(v) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
  hashCombine(Seed, Rest...);
}
}  // namespace foresighter::common