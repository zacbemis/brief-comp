#ifndef BRIEF_TESTS_ERRORS_SUPPORT_H
#define BRIEF_TESTS_ERRORS_SUPPORT_H

#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  Diagnostics errors;
  SemaProgram *program;
} Result;
static inline Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "errors.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  // Explicit failures and implicit converter tags have error kind. Shared
  // :none remains neutral.
  for (const SemaSymSet *set = r.program->symbol_sets; set; set = set->next) {
    if (!sema_ok(&set->type))
      continue;
    for (size_t i = 0; i < set->count; i++) {
      const SemaTag *tag = set->variants[i].tag;
      if (tag != r.program->ok && tag->module && tag->kind_declared)
        ASSERT_EQ_INT(SEMA_TAG_ERROR, tag->kind);
    }
  }
  return r;
}
static inline Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  if (r.program->checked) {
    IrProgram *ir = ir_lower(a, r.program);
    ASSERT_TRUE(ir != NULL);
    ir_insert_rc(a, ir);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static inline Result invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    found |= strstr(r.errors.items[i]->message, message) != NULL;
  if (!found)
    diagnostics_print(&r.errors, stderr);
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  return r;
}
static inline void diagnostic_errors(Arena *a, const char *text,
                                     const char *required, size_t handling,
                                     const char *other, size_t other_count) {
  Result r = check(a, text);
  unsigned repeats = r.program->resolved ? 2u : 1u;
  for (unsigned repeat = 0; repeat < repeats; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != handling + other_count) {
      fprintf(stderr, "handling recovery case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(handling + other_count, r.errors.count);
    size_t actual_handling = 0, actual_other = 0;
    for (size_t i = 0; i < r.errors.count; i++) {
      const char *message = r.errors.items[i]->message;
      actual_handling += strcmp(message, required) == 0;
      actual_other += other && strcmp(message, other) == 0;
    }
    ASSERT_EQ_INT(handling, actual_handling);
    ASSERT_EQ_INT(other_count, actual_other);
    if (!other)
      for (size_t i = 0; i < r.program->count; i++)
        ASSERT_TRUE(!r.program->functions[i].inference_conflict);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
}
static inline void handling_errors(Arena *a, const char *text, size_t handling,
                                   const char *other, size_t other_count) {
  diagnostic_errors(
      a, text, "A call that can fail must be handled with ?, !, or a match.",
      handling, other, other_count);
}

#endif
