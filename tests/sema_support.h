#ifndef BRIEF_TESTS_SEMA_SUPPORT_H
#define BRIEF_TESTS_SEMA_SUPPORT_H

#include "ir/ir.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/sema.h"
#include "test_utils.h"

#include <float.h>
#include <math.h>

typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static inline Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, "sema.brf", text, strlen(text));
  TokenArray tokens = newline_filter(a, lexer_tokenize(a, source, &r.errors));
  Program *ast = parse_program(a, &names, tokens, &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static inline void valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
}
static inline void invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    if (strstr(r.errors.items[i]->message, message))
      found = 1;
  if (!found) {
    fprintf(stderr, "missing '%s' for %s\n", message, text);
    diagnostics_print(&r.errors, stderr);
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
}
static inline void real_literal(Arena *a, const char *type,
                                const char *spelling, double expected) {
  size_t capacity = strlen(spelling) + 64;
  char *text = arena_alloc(a, capacity);
  snprintf(text, capacity, "main() {}\nvalue() -> %s { %s }", type, spelling);
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  SemaExpr *literal = r.program->functions[1].body->as.block.items[0];
  ASSERT_TRUE(literal->real == expected);
  ASSERT_TRUE(!!signbit(literal->real) == !!signbit(expected));
}
static inline size_t message_count(const Diagnostics *errors,
                                   const char *message) {
  size_t count = 0;
  for (size_t i = 0; i < errors->count; i++)
    if (strstr(errors->items[i]->message, message))
      count++;
  return count;
}
static inline Result generic_error(Arena *a, const char *text, const char *site,
                                   const char *other, size_t other_count) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, "generic.brf", text, strlen(text));
  TokenArray tokens = newline_filter(a, lexer_tokenize(a, source, &r.errors));
  Program *ast = parse_program(a, &names, tokens, &r.errors);
  ASSERT_EQ_INT(0, r.errors.count);
  size_t bytes = tokens.count * sizeof(Token);
  Token *saved = arena_alloc(a, bytes);
  memcpy(saved, tokens.items, bytes);
  // Re-resolve the same immutable AST: resolution diagnostics must not rely
  // on state left by checking an earlier semantic program.
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    diagnostics_init(&r.errors, a);
    r.program = sema_resolve(a, ast, &r.errors);
    sema_check(r.program, &r.errors);
    if (r.errors.count != 1 + other_count) {
      fprintf(stderr, "generic recovery case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1 + other_count, r.errors.count);
    ASSERT_EQ_INT(
        1, message_count(&r.errors, "Generic calls are not implemented yet."));
    ASSERT_EQ_INT(other_count, other ? message_count(&r.errors, other) : 0);
    const char *call = strstr(text, site);
    ASSERT_TRUE(call != NULL);
    for (size_t i = 0; i < r.errors.count; i++) {
      Diagnostic *d = r.errors.items[i];
      if (!strcmp(d->message, "Generic calls are not implemented yet.") &&
          call) {
        ASSERT_EQ_INT((size_t)(call - text), d->span.start);
        ASSERT_EQ_INT((size_t)(call - text) + strlen(site), d->span.end);
      }
    }
    ASSERT_TRUE(!r.program->resolved);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
    ASSERT_TRUE(memcmp(saved, tokens.items, bytes) == 0);
  }
  return r;
}

#endif
