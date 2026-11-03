#include "common/diagnostic.h"

#include <string.h>

void diagnostics_init(Diagnostics *diagnostics, Arena *arena) {
  *diagnostics = (Diagnostics){.arena = arena};
}

Diagnostic *diagnostics_error(Diagnostics *diagnostics, Span span,
                              const char *message) {
  if (diagnostics->count == diagnostics->capacity) {
    size_t capacity = diagnostics->capacity ? diagnostics->capacity * 2 : 16;
    // Source-sized lists cannot normally reach this limit; arena_calloc also
    // checks multiplication overflow before allocating the array.
    if (capacity < diagnostics->capacity)
      capacity = (size_t)-1;
    Diagnostic **items =
        arena_calloc(diagnostics->arena, capacity, sizeof(*items));
    if (diagnostics->count)
      memcpy(items, diagnostics->items, diagnostics->count * sizeof(*items));
    diagnostics->items = items;
    diagnostics->capacity = capacity;
  }
  Diagnostic *error = arena_calloc(diagnostics->arena, 1, sizeof(*error));
  error->span = span_make(span.file, span.start, span.end);
  error->message = arena_strdup(diagnostics->arena, message);
  diagnostics->items[diagnostics->count++] = error;
  return error;
}

void diagnostic_note(Diagnostics *diagnostics, Diagnostic *error, Span span,
                     const char *message) {
  DiagnosticNote *note = arena_calloc(diagnostics->arena, 1, sizeof(*note));
  note->span = span_make(span.file, span.start, span.end);
  note->message = arena_strdup(diagnostics->arena, message);
  if (error->last_note)
    error->last_note->next = note;
  else
    error->notes = note;
  error->last_note = note;
}

int diagnostic_fix(Diagnostics *diagnostics, Diagnostic *error, Span span,
                   const char *replacement) {
  if (!span.file || span.start > span.end || span.end > span.file->length)
    return 0;
  DiagnosticFix *fix = arena_calloc(diagnostics->arena, 1, sizeof(*fix));
  fix->span = span;
  fix->replacement = arena_strdup(diagnostics->arena, replacement);
  if (error->last_fix)
    error->last_fix->next = fix;
  else
    error->fixes = fix;
  error->last_fix = fix;
  return 1;
}

// Count UTF-8 code points rather than bytes. Wide and combining characters
// will need a fuller terminal-width policy; byte columns in locations stay
// unchanged. Use the same calculation for excerpts and their underlines.
static size_t next_display_column(size_t column, unsigned char c) {
  if (c == '\t')
    return column + 4 - column % 4;
  return column + ((c & 0xc0) != 0x80);
}

static size_t display_width(const char *text, size_t length) {
  size_t width = 0;
  for (size_t i = 0; i < length; i++)
    width = next_display_column(width, (unsigned char)text[i]);
  return width;
}

static void print_excerpt(FILE *output, Span span) {
  if (!span.file)
    return;
  SourcePosition position = source_position(span.file, span.start);
  size_t length;
  const char *line = source_line(span.file, position.line, &length);
  size_t start = position.column - 1;
  if (start > length)
    start = length;
  size_t end = span.end - span.file->line_starts[position.line - 1];
  if (end > length)
    end = length;
  if (end < start)
    end = start;

  int digits = 1;
  for (size_t n = position.line; n >= 10; n /= 10)
    digits++;
  fprintf(output, "  --> %s:%zu:%zu\n", span.file->path, position.line,
          position.column);
  fprintf(output, "%*s |\n", digits, "");
  fprintf(output, "%zu | ", position.line);
  size_t column = 0;
  for (size_t i = 0; i < length; i++) {
    if (line[i] == '\t') {
      size_t spaces = 4 - column % 4;
      for (size_t j = 0; j < spaces; j++)
        fputc(' ', output);
      column += spaces;
    } else {
      // Do not allow source control characters to alter diagnostic layout.
      unsigned char c = (unsigned char)line[i];
      fputc(c < 32 || c == 127 ? '?' : c, output);
      column = next_display_column(column, c);
    }
  }
  fputc('\n', output);
  fprintf(output, "%*s | ", digits, "");
  size_t indent = display_width(line, start);
  for (size_t i = 0; i < indent; i++)
    fputc(' ', output);
  size_t width = display_width(line, end) - indent;
  fputc('^', output);
  for (size_t i = 1; i < width; i++)
    fputc('~', output);
  fputc('\n', output);
}

// Keep help on one terminal line even for multi-line edits or control bytes.
// The structured fix always retains the exact, unescaped source replacement.
static void print_fix_text(FILE *output, const char *text, size_t length) {
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    switch (c) {
    case '\n':
      fputs("\\n", output);
      break;
    case '\r':
      fputs("\\r", output);
      break;
    case '\t':
      fputs("\\t", output);
      break;
    case '`':
      fputs("\\`", output);
      break;
    default:
      if (c < 32 || c == 127)
        fprintf(output, "\\x%02x", (unsigned int)c);
      else
        fputc(c, output);
      break;
    }
  }
}

static void print_fix(FILE *output, const DiagnosticFix *fix, Span primary) {
  Span span = fix->span;
  SourcePosition start = source_position(span.file, span.start);
  SourcePosition end = source_position(span.file, span.end);
  size_t length;
  const char *line = source_line(span.file, end.line, &length);
  size_t line_end = (size_t)(line - span.file->text) + length;
  if (line_end < span.end)
    line_end = span.end; // A span may end inside a CRLF line ending.
  fputs("help: ", output);
  if (span.file != primary.file ||
      start.line != source_position(primary.file, primary.start).line)
    fprintf(output, "%s:%zu:%zu: ", span.file->path, start.line, start.column);
  fputs("write `", output);
  size_t line_start = span.file->line_starts[start.line - 1];
  print_fix_text(output, span.file->text + line_start, span.start - line_start);
  print_fix_text(output, fix->replacement, strlen(fix->replacement));
  print_fix_text(output, span.file->text + span.end, line_end - span.end);
  fputs("`\n", output);
}

void diagnostics_print(const Diagnostics *diagnostics, FILE *output) {
  for (size_t i = 0; i < diagnostics->count; i++) {
    const Diagnostic *error = diagnostics->items[i];
    fprintf(output, "error: %s\n", error->message);
    print_excerpt(output, error->span);
    for (const DiagnosticNote *note = error->notes; note; note = note->next) {
      fprintf(output, "note: %s\n", note->message);
      print_excerpt(output, note->span);
    }
    for (const DiagnosticFix *fix = error->fixes; fix; fix = fix->next)
      print_fix(output, fix, error->span);
  }
}
