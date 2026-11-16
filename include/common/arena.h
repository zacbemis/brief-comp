#ifndef BRIEF_ARENA_H
#define BRIEF_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
  ArenaBlock *blocks;
} Arena;

void arena_init(Arena *arena);
void arena_destroy(Arena *arena);
// Allocations are aligned for any ordinary C type. Zero-size allocations are
// permitted. Allocation failure or size overflow terminates the process.
void *arena_alloc(Arena *arena, size_t size);
void *arena_calloc(Arena *arena, size_t count, size_t size);
// Ensures room for one more item after `count` items, doubling *capacity
// (starting at `initial`) and copying into a new array when full. Returns
// the array to use; outgrown storage stays in the arena.
void *arena_grow(Arena *arena, void *items, size_t count, size_t *capacity,
                 size_t size, size_t initial);
#define ARENA_GROW(arena, items, count, capacity, initial)                     \
  ((items) = arena_grow((arena), (items), (count), &(capacity),                \
                        sizeof(*(items)), (initial)))
char *arena_strndup(Arena *arena, const char *text, size_t length);
char *arena_strdup(Arena *arena, const char *text);

#endif
