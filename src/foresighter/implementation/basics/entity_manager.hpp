#pragma once
/**
 * @file entity_manager.hpp
 * @brief Persistent storage for states and transitions with hash-consing and ID-based lookup.
 *
 * EntityManager<T> assigns a unique IDType to each distinct entity, deduplicating
 * via a hash map. Provides O(1) creation (with dedup) and O(1) read by ID.
 * Used by FTA to manage State and Transition objects.
 */

#include <foresighter/common/exceptions.hpp>
#include <foresighter/common/hashmap.hpp>
#include <string>
#include <vector>

#include <foresighter/implementation/basics/types.hpp>
#include <fstream>
#include "cereal/archives/binary.hpp"
#include "cereal/types/vector.hpp"

namespace foresighter::implementation::basics {
template <typename T>
class EntityManager {
 public:
  using EntityIDType = foresighter::implementation::basics::IDType;
  using EntityType = T;

  EntityManager() {
    // Pre-reserve space to minimize reallocations
    entities.reserve(4096);
    entityMap.reserve(4096);
  }

  //copy and assignment are deleted
  EntityManager(const EntityManager&) = delete;
  EntityManager& operator=(const EntityManager&) = delete;

  //move constructor
  EntityManager(EntityManager&& other) noexcept = default;

  //move assignment
  EntityManager& operator=(EntityManager&& other) noexcept = default;

  template <typename... Args>
  EntityIDType create(Args&&... args) {
    // Construct entity in-place to avoid copies
    T entity(std::forward<Args>(args)...);

    // Use transparent find if possible
    if (auto it = entityMap.find(entity); it != entityMap.end()) {
      return it->second;
    }

    // Double capacity if needed
    if (entities.size() == entities.capacity()) {
      const auto new_cap = entities.capacity() * 2;
      entities.reserve(new_cap);
      entityMap.reserve(new_cap);
    }

    const auto id = entities.size();
    entities.push_back(std::move(entity));
    entityMap.emplace(entities.back(), id);
    return id;
  }

  [[nodiscard]] const EntityType& read(EntityIDType id) const noexcept {
    return entities[id];
  }
  void clear() {
    entities.clear();
    entityMap.clear();
  }

 private:
  friend class cereal::access;

  template <class Archive>
  void serialize(Archive& archive) {
    archive(entities, entityMap);
  }

  std::vector<T> entities;
  emhash8hashmap::HashMap<EntityType, EntityIDType> entityMap;
};

}  // namespace foresighter::implementation::basics
