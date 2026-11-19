/**
* @file cartesian_product.hpp
* @brief 
* @author Rui Dong
* @date 4/22/24
*/

#pragma once
#include <spdlog/spdlog.h>
#include <numeric>
#include <vector>

namespace foresighter::common {
template <typename T>
std::vector<std::vector<T>> cartesianProduct(
    const std::vector<std::vector<T>>& V) {
  if (V.empty()) {
    return {};
  }
  auto Product = [](long long A, const std::vector<T>& B) {
    return A * B.size();
  };
  const long long TotalCount =
      std::accumulate(V.begin(), V.end(), 1LL, Product);
  std::vector<std::vector<T>> Results(TotalCount);
  std::vector<T> U(V.size());
  for (long long N = 0; N < TotalCount; ++N) {
    lldiv_t Q{N, 0};
    for (long long I = V.size() - 1; 0 <= I; --I) {
      Q = lldiv(Q.quot, V[I].size());
      U[I] = V[I][Q.rem];
    }
    Results[N] = std::move(U);
    U = std::vector<T>(V.size());
  }
  return Results;
}

//an iterator version of cartesian product
template <typename T>
class CartesianProductIterator {
 public:
  explicit CartesianProductIterator(const std::vector<std::vector<T>>& V)
      : V(V), N(0), U(V.size()), TotalCount(0) {
    if (V.empty()) {
      return;
    }
    auto Product = [](long long A, const std::vector<T>& B) {
      return A * B.size();
    };
    TotalCount = std::accumulate(V.begin(), V.end(), 1LL, Product);
  }

  bool hasNext() { return N < TotalCount; }

  const std::vector<T>& next() {
    lldiv_t Q{N, 0};
    for (long long I = V.size() - 1; 0 <= I; --I) {
      Q = lldiv(Q.quot, V[I].size());
      U[I] = V[I][Q.rem];
    }
    ++N;
    return U;
  }

  unsigned long long size() { return TotalCount; }

 private:
  std::vector<std::vector<T>> V;
  std::vector<T> U;
  long long N;
  long long TotalCount;
};
}  // namespace foresighter::common
