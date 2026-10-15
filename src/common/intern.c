#include "common/intern.h"

#include <stdint.h>
#include <string.h>

struct InternEntry {
  InternEntry *next;
  uint64_t hash;
  size_t length;
  const char *text;
};

void intern_init(InternTable *table, Arena *arena) {
  *table = (InternTable){.arena = arena};
}

static uint64_t hash_text(const char *text, size_t length) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < length; i++) {
    hash ^= (unsigned char)text[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void grow(InternTable *table) {
  size_t capacity = table->capacity ? table->capacity * 2 : 64;
  if (capacity < table->capacity)
    capacity = SIZE_MAX;
  InternEntry **buckets =
      arena_calloc(table->arena, capacity, sizeof(*buckets));
  for (size_t i = 0; i < table->capacity; i++) {
    InternEntry *entry = table->buckets[i];
    while (entry) {
      InternEntry *next = entry->next;
      size_t bucket = entry->hash % capacity;
      entry->next = buckets[bucket];
      buckets[bucket] = entry;
      entry = next;
    }
  }
  table->buckets = buckets;
  table->capacity = capacity;
}

const char *intern_n(InternTable *table, const char *text, size_t length) {
  uint64_t hash = hash_text(text, length);
  if (table->capacity) {
    for (InternEntry *entry = table->buckets[hash % table->capacity]; entry;
         entry = entry->next) {
      if (entry->hash == hash && entry->length == length &&
          memcmp(entry->text, text, length) == 0)
        return entry->text;
    }
  }
  if (!table->capacity || table->count >= table->capacity - table->capacity / 4)
    grow(table);
  size_t bucket = hash % table->capacity;
  InternEntry *entry = arena_alloc(table->arena, sizeof(*entry));
  *entry = (InternEntry){table->buckets[bucket], hash, length,
                         arena_strndup(table->arena, text, length)};
  table->buckets[bucket] = entry;
  table->count++;
  return entry->text;
}

const char *intern(InternTable *table, const char *text) {
  return intern_n(table, text, strlen(text));
}
