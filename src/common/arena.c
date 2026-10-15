#include "common/arena.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ArenaBlock {
  ArenaBlock *next;
  size_t used;
  size_t capacity;
  max_align_t data[];
};

static void allocation_failed(void) {
  fputs("brief: out of memory or allocation size overflow\n", stderr);
  abort();
}

void arena_init(Arena *arena) { arena->blocks = NULL; }

void arena_destroy(Arena *arena) {
  ArenaBlock *block = arena->blocks;
  while (block) {
    ArenaBlock *next = block->next;
    free(block);
    block = next;
  }
  arena->blocks = NULL;
}

void *arena_alloc(Arena *arena, size_t size) {
  const size_t alignment = _Alignof(max_align_t);
  if (size == 0)
    size = 1;
  if (size > SIZE_MAX - (alignment - 1))
    allocation_failed();
  size = (size + alignment - 1) / alignment * alignment;

  ArenaBlock *block = arena->blocks;
  if (!block || size > block->capacity - block->used) {
    size_t capacity = size > 4096 ? size : 4096;
    if (capacity > SIZE_MAX - sizeof(ArenaBlock))
      allocation_failed();
    block = malloc(sizeof(ArenaBlock) + capacity);
    if (!block)
      allocation_failed();
    block->next = arena->blocks;
    block->used = 0;
    block->capacity = capacity;
    arena->blocks = block;
  }
  void *result = (unsigned char *)block->data + block->used;
  block->used += size;
  return result;
}

void *arena_calloc(Arena *arena, size_t count, size_t size) {
  if (size && count > SIZE_MAX / size)
    allocation_failed();
  size_t bytes = count * size;
  void *result = arena_alloc(arena, bytes);
  memset(result, 0, bytes);
  return result;
}

char *arena_strndup(Arena *arena, const char *text, size_t length) {
  if (length == SIZE_MAX)
    allocation_failed();
  char *copy = arena_alloc(arena, length + 1);
  if (length)
    memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

char *arena_strdup(Arena *arena, const char *text) {
  return arena_strndup(arena, text, strlen(text));
}
