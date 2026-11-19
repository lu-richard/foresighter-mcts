#pragma once
/**
 * @file types.hpp
 * @brief Fundamental type aliases: IDType (uint32_t) and WeightType (uint32_t).
 *
 * Used throughout the framework for state/transition IDs and production costs.
 */

#include <cstdint>

namespace foresighter::implementation::basics {
using IDType = uint32_t;
using WeightType = uint32_t;
}  // namespace foresighter::implementation::basics
