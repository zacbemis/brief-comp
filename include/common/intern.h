#ifndef BRIEF_INTERN_H
#define BRIEF_INTERN_H

#include "common/arena.h"

typedef struct InternEntry InternEntry;
typedef struct {
  Arena *arena;
  InternEntry **buckets;
  size_t capacity;
  size_t count;
} InternTable;

void intern_init(InternTable *table, Arena *arena);
// Equal byte strings return the same stable, NUL-terminated pointer within
// this table. No separate teardown is needed; the arena owns everything.
const char *intern_n(InternTable *table, const char *text, size_t length);
const char *intern(InternTable *table, const char *text);

#endif
