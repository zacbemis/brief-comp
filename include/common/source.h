#ifndef BRIEF_SOURCE_H
#define BRIEF_SOURCE_H

#include "common/arena.h"

typedef struct {
  const char *path;
  const char *text; // Arena-owned, with a sentinel NUL after length bytes.
  size_t length;
  size_t *line_starts;
  size_t line_count;
} SourceFile;

typedef struct {
  const SourceFile *file;
  size_t start;
  size_t end; // Exclusive byte offset; an empty span points between bytes.
} Span;

typedef struct {
  size_t line;   // One-based.
  size_t column; // One-based byte column (not a Unicode display column).
} SourcePosition;

SourceFile *source_create(Arena *arena, const char *path, const char *text,
                          size_t length);
// Returns NULL on an I/O error, with an arena-owned message in error_message.
SourceFile *source_load(Arena *arena, const char *path,
                        const char **error_message);
SourcePosition source_position(const SourceFile *file, size_t offset);
// Returns a line without its line ending, or NULL for an invalid line number.
const char *source_line(const SourceFile *file, size_t line, size_t *length);
// Clamps offsets to the file. A NULL file represents a locationless diagnostic.
Span span_make(const SourceFile *file, size_t start, size_t end);

#endif
