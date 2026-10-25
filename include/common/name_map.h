#ifndef BRIEF_NAME_MAP_H
#define BRIEF_NAME_MAP_H

#include "common/arena.h"

// Maps an (owner, name) pair to a value. Owners are compared by identity and
// names by their NUL-terminated bytes, so names need not be interned; the map
// keeps the name pointer, which must outlive it. Storage belongs to the arena.
typedef struct NameEntry NameEntry;
typedef struct {
  Arena *arena;
  NameEntry *entries;
  size_t capacity;
  size_t count;
} NameMap;

void name_map_init(NameMap *map, Arena *arena);
// The value stored for owner and name, or NULL when there is none.
void *name_map_get(const NameMap *map, const void *owner, const char *name);
// Stores value for owner and name, returning the value it replaces (or NULL).
void *name_map_put(NameMap *map, const void *owner, const char *name,
                   void *value);

#endif
