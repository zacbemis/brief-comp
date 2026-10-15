#include "common/source.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SourceFile *source_create(Arena *arena, const char *path, const char *text,
                          size_t length) {
  SourceFile *file = arena_alloc(arena, sizeof(*file));
  file->path = arena_strdup(arena, path);
  file->text = arena_strndup(arena, text, length);
  file->length = length;
  file->line_count = 1;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '\n')
      file->line_count++;
  }
  file->line_starts = arena_calloc(arena, file->line_count, sizeof(size_t));
  size_t line = 1;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '\n')
      file->line_starts[line++] = i + 1;
  }
  return file;
}

static SourceFile *load_error(Arena *arena, const char **error_message,
                              const char *message) {
  if (error_message)
    *error_message = arena_strdup(arena, message);
  return NULL;
}

SourceFile *source_load(Arena *arena, const char *path,
                        const char **error_message) {
  if (error_message)
    *error_message = NULL;
  FILE *input = fopen(path, "rb");
  if (!input)
    return load_error(arena, error_message, strerror(errno));

  // Read incrementally instead of seeking: this also works for empty files
  // and special files. Only the temporary input buffer uses realloc/free.
  size_t capacity = 4096;
  size_t length = 0;
  char *text = malloc(capacity);
  if (!text) {
    fclose(input);
    return load_error(arena, error_message, "out of memory reading source");
  }
  for (;;) {
    errno = 0;
    size_t read_count = fread(text + length, 1, capacity - length, input);
    length += read_count;
    if (ferror(input)) {
      int saved_errno = errno;
      free(text);
      fclose(input);
      return load_error(arena, error_message,
                        saved_errno ? strerror(saved_errno)
                                    : "cannot read source");
    }
    if (feof(input))
      break;
    if (length == capacity) {
      if (capacity > SIZE_MAX / 2) {
        free(text);
        fclose(input);
        return load_error(arena, error_message, "source file is too large");
      }
      capacity *= 2;
      char *grown = realloc(text, capacity);
      if (!grown) {
        free(text);
        fclose(input);
        return load_error(arena, error_message, "out of memory reading source");
      }
      text = grown;
    }
  }
  if (fclose(input) != 0) {
    int saved_errno = errno;
    free(text);
    return load_error(arena, error_message, strerror(saved_errno));
  }
  SourceFile *file = source_create(arena, path, text, length);
  free(text);
  return file;
}

SourcePosition source_position(const SourceFile *file, size_t offset) {
  if (offset > file->length)
    offset = file->length;
  size_t low = 0;
  size_t high = file->line_count;
  while (low + 1 < high) {
    size_t middle = low + (high - low) / 2;
    if (file->line_starts[middle] <= offset)
      low = middle;
    else
      high = middle;
  }
  return (SourcePosition){low + 1, offset - file->line_starts[low] + 1};
}

const char *source_line(const SourceFile *file, size_t line, size_t *length) {
  *length = 0;
  if (line == 0 || line > file->line_count)
    return NULL;
  size_t start = file->line_starts[line - 1];
  size_t end =
      line < file->line_count ? file->line_starts[line] - 1 : file->length;
  if (end > start && file->text[end - 1] == '\r')
    end--;
  *length = end - start;
  return file->text + start;
}

Span span_make(const SourceFile *file, size_t start, size_t end) {
  if (!file)
    return (Span){NULL, 0, 0};
  if (start > file->length)
    start = file->length;
  if (end > file->length)
    end = file->length;
  if (end < start)
    end = start;
  return (Span){file, start, end};
}
