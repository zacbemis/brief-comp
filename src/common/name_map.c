#include "common/name_map.h"

#include <stdint.h>
#include <string.h>

struct NameEntry {
  const void *owner;
  const char *name; // NULL marks an empty slot.
  uint64_t hash;
  void *value;
};

void name_map_init(NameMap *map, Arena *arena) {
  *map = (NameMap){.arena = arena};
}

static uint64_t hash_name(const void *owner, const char *name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
    hash ^= *c;
    hash *= UINT64_C(1099511628211);
  }
  hash ^= (uint64_t)(uintptr_t)owner;
  return hash * UINT64_C(1099511628211);
}

// Linear probing over a power-of-two table; returns the matching or empty slot.
static NameEntry *slot(NameEntry *entries, size_t capacity, const void *owner,
                       const char *name, uint64_t hash) {
  size_t mask = capacity - 1;
  for (size_t i = (size_t)hash & mask;; i = (i + 1) & mask) {
    NameEntry *entry = &entries[i];
    if (!entry->name || (entry->hash == hash && entry->owner == owner &&
                         strcmp(entry->name, name) == 0))
      return entry;
  }
}

void *name_map_get(const NameMap *map, const void *owner, const char *name) {
  if (!map->capacity)
    return NULL;
  NameEntry *entry =
      slot(map->entries, map->capacity, owner, name, hash_name(owner, name));
  return entry->name ? entry->value : NULL;
}

void *name_map_put(NameMap *map, const void *owner, const char *name,
                   void *value) {
  // Keep the load at most one half so probes stay short and always terminate.
  if (map->count + 1 > map->capacity / 2) {
    size_t capacity = map->capacity ? map->capacity * 2 : 64;
    NameEntry *entries = arena_calloc(map->arena, capacity, sizeof(*entries));
    for (size_t i = 0; i < map->capacity; i++) {
      NameEntry *old = &map->entries[i];
      if (old->name)
        *slot(entries, capacity, old->owner, old->name, old->hash) = *old;
    }
    map->entries = entries;
    map->capacity = capacity;
  }
  uint64_t hash = hash_name(owner, name);
  NameEntry *entry = slot(map->entries, map->capacity, owner, name, hash);
  void *previous = entry->name ? entry->value : NULL;
  if (!entry->name) {
    *entry = (NameEntry){owner, name, hash, NULL};
    map->count++;
  }
  entry->value = value;
  return previous;
}
