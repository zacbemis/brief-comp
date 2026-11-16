#ifndef BRIEF_TESTS_PARSER_SUPPORT_H
#define BRIEF_TESTS_PARSER_SUPPORT_H

#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

#include <stdint.h>

typedef struct {
  SourceFile *source;
  Diagnostics diagnostics;
  InternTable names;
  Program *program;
  TokenArray raw;
} Parsed;
static inline Parsed *parse(Arena *arena, const char *text) {
  Parsed *v = arena_calloc(arena, 1, sizeof(*v));
  v->source = source_create(arena, "test.brf", text, strlen(text));
  diagnostics_init(&v->diagnostics, arena);
  intern_init(&v->names, arena);
  v->raw = lexer_tokenize(arena, v->source, &v->diagnostics);
  v->program = parse_program(arena, &v->names, newline_filter(arena, v->raw),
                             &v->diagnostics);
  return v;
}
static inline char *dump(Arena *arena, const Program *program) {
  FILE *out = tmpfile();
  if (!out)
    return NULL;
  ast_print(out, program);
  fflush(out);
  long length = ftell(out);
  if (length < 0 || fseek(out, 0, SEEK_SET)) {
    fclose(out);
    return NULL;
  }
  char *text = arena_calloc(arena, (size_t)length + 1, 1);
  size_t count = fread(text, 1, (size_t)length, out);
  text[count] = '\0';
  fclose(out);
  return text;
}
static inline void program_dump(Arena *arena, const char *text,
                                const char *expected) {
  Parsed *v = parse(arena, text);
  if (v->diagnostics.count)
    diagnostics_print(&v->diagnostics, stderr);
  ASSERT_EQ_INT(0, v->diagnostics.count);
  char *actual = dump(arena, v->program);
  ASSERT_TRUE(actual != NULL);
  if (actual)
    ASSERT_EQ_STRN(expected, actual, strlen(actual));
}
static inline void expression_dump(Arena *arena, const char *text,
                                   const char *expected) {
  char *source = arena_alloc(arena, strlen(text) + 32);
  sprintf(source, "main() {\n%s\n}\n", text);
  char *tree = arena_alloc(arena, strlen(expected) + 64);
  sprintf(tree, "(program (fn main (params) _ (block %s)))\n", expected);
  program_dump(arena, source, tree);
}
static inline void invalid(Arena *arena, const char *source,
                           const char *message) {
  Parsed *v = parse(arena, source);
  ASSERT_TRUE(v->diagnostics.count > 0);
  int found = 0;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    if (strstr(v->diagnostics.items[i]->message, message))
      found = 1;
  if (!found)
    diagnostics_print(&v->diagnostics, stderr);
  ASSERT_TRUE(found);
}
// Test-only application of diagnostic groups, ordered in original byte space.
static inline char *apply_fixes(Arena *arena, Parsed *v) {
  size_t count = 0, size = v->source->length + 1;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    for (DiagnosticFix *f = v->diagnostics.items[i]->fixes; f; f = f->next) {
      count++;
      size += strlen(f->replacement);
    }
  const DiagnosticFix **fixes = arena_alloc(arena, count * sizeof(*fixes));
  size_t n = 0;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    for (DiagnosticFix *f = v->diagnostics.items[i]->fixes; f; f = f->next) {
      size_t at = n++;
      while (at && fixes[at - 1]->span.start > f->span.start) {
        fixes[at] = fixes[at - 1];
        at--;
      }
      fixes[at] = f;
    }
  char *result = arena_alloc(arena, size);
  size_t source = 0, out = 0;
  for (size_t i = 0; i < count; i++) {
    const DiagnosticFix *f = fixes[i];
    ASSERT_TRUE(f->span.file == v->source && f->span.start >= source);
    if (f->span.start < source)
      continue;
    size_t prefix = f->span.start - source, length = strlen(f->replacement);
    memcpy(result + out, v->source->text + source, prefix);
    out += prefix;
    memcpy(result + out, f->replacement, length);
    out += length;
    source = f->span.end;
  }
  strcpy(result + out, v->source->text + source);
  return result;
}

#endif
