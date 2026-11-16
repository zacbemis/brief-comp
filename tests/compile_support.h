#ifndef BRIEF_TESTS_COMPILE_SUPPORT_H
#define BRIEF_TESTS_COMPILE_SUPPORT_H

#include "codegen/c.h"
#include "driver/compile.h"
#include "test_utils.h"

// Shared front-to-back helpers for checker, lowering and emission tests.
// A suite may define COMPILE_SUPPORT_HOOK(const Result *, const char *text)
// before including this header to assert extra invariants after each check.

typedef struct {
  const char *text;
  Program *ast;
  SemaProgram *program;
  Diagnostics errors;
} Result;

#ifdef COMPILE_SUPPORT_HOOK
static void COMPILE_SUPPORT_HOOK(const Result *r, const char *text);
#endif

// Lexes, parses, resolves and checks. Parsing must not change its tokens.
static inline Result check(Arena *a, const char *text) {
  Compilation c;
  compilation_init(&c, a, source_create(a, "test.brf", text, strlen(text)));
  compilation_lex(&c);
  size_t bytes = c.filtered.count * sizeof(Token);
  Token *saved = arena_alloc(a, bytes);
  memcpy(saved, c.filtered.items, bytes);
  compilation_parse(&c);
  ASSERT_TRUE(!memcmp(saved, c.filtered.items, bytes));
  compilation_resolve(&c);
  compilation_check(&c);
  Result r = {.text = text,
              .ast = c.syntax,
              .program = c.program,
              .errors = c.diagnostics};
#ifdef COMPILE_SUPPORT_HOOK
  COMPILE_SUPPORT_HOOK(&r, text);
#endif
  return r;
}

// Checks again. A program that failed resolution is rebuilt from its text.
static inline void recheck(Arena *a, Result *r) {
  if (!r->program->resolved) {
    *r = check(a, r->text);
    return;
  }
  diagnostics_init(&r->errors, a);
  sema_check(r->program, &r->errors);
}

// The source with LF line endings, or converted to CRLF.
static inline char *lines(Arena *a, const char *source, int crlf) {
  char *text = arena_alloc(a, 2 * strlen(source) + 1);
  size_t n = 0;
  for (; *source; source++) {
    if (crlf && *source == '\n')
      text[n++] = '\r';
    text[n++] = *source;
  }
  text[n] = 0;
  return text;
}

static inline char *emit_text(Arena *a, const IrProgram *ir) {
  FILE *out = tmpfile();
  ASSERT_TRUE(out != NULL);
  if (!out)
    return NULL;
  ASSERT_TRUE(emit_c(out, ir));
  long size = ftell(out);
  ASSERT_TRUE(size >= 0);
  rewind(out);
  char *text = arena_alloc(a, (size_t)size + 1);
  ASSERT_EQ_INT((size_t)size, fread(text, 1, (size_t)size, out));
  text[size] = 0;
  fclose(out);
  return text;
}

// Lowers a checked program. Emission must wait for ownership insertion, which
// is idempotent.
static inline IrProgram *lower(Arena *a, SemaProgram *program) {
  IrProgram *ir = ir_lower(a, program);
  ASSERT_TRUE(ir != NULL);
  if (!ir)
    return NULL;
  FILE *out = tmpfile();
  ASSERT_TRUE(out != NULL);
  if (out) {
    ASSERT_TRUE(!emit_c(out, ir));
    fclose(out);
  }
  ir_insert_rc(a, ir);
  size_t *counts = arena_calloc(a, ir->count, sizeof(*counts));
  for (size_t i = 0; i < ir->count; i++)
    counts[i] = ir->functions[i].count;
  ir_insert_rc(a, ir);
  for (size_t i = 0; i < ir->count; i++)
    ASSERT_EQ_INT(counts[i], ir->functions[i].count);
  (void)emit_text(a, ir);
  return ir;
}

// Return and variable types, in function order, for stability checks.
static inline const SemaType **checked_types(Arena *a, const SemaProgram *p,
                                             size_t *count) {
  *count = 0;
  for (size_t i = 0; i < p->count; i++)
    *count += 1 + p->functions[i].variable_count;
  const SemaType **types = arena_calloc(a, *count, sizeof(*types));
  size_t n = 0;
  for (size_t i = 0; i < p->count; i++) {
    const SemaFunction *f = &p->functions[i];
    types[n++] = f->symbol.return_type;
    for (size_t j = 0; j < f->variable_count; j++)
      types[n++] = f->variables[j]->type;
  }
  return types;
}

// Accepted on three checks with stable types and the source intact, then
// lowered and emitted.
static inline Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  size_t count;
  const SemaType **types = checked_types(a, r.program, &count);
  for (unsigned repeat = 0; repeat < 3; repeat++) {
    if (repeat)
      recheck(a, &r);
    if (r.errors.count) {
      fprintf(stderr, "Unexpected errors:\n%s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_TRUE(!strcmp(text, r.program->module->text));
    size_t now;
    const SemaType **current = checked_types(a, r.program, &now);
    ASSERT_EQ_INT(count, now);
    for (size_t i = 0; i < count && i < now; i++)
      ASSERT_TRUE(types[i] == current[i]);
    for (size_t i = 0; i < r.program->count; i++) {
      const SemaSymbol *s = &r.program->functions[i].symbol;
      ASSERT_TRUE(s->return_type == s->outcome_type);
    }
  }
  if (r.program->checked)
    lower(a, r.program);
  return r;
}

// valid() with LF and with CRLF line endings.
static inline void accepts(Arena *a, const char *source) {
  for (int crlf = 0; crlf < 2; crlf++)
    (void)valid(a, lines(a, source, crlf));
}

static inline int reports(const Result *r, const char *message) {
  for (size_t i = 0; i < r->errors.count; i++)
    if (strstr(r->errors.items[i]->message, message))
      return 1;
  return 0;
}

// Rejected with an error containing message, and never lowered.
static inline Result invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = reports(&r, message);
  if (!found) {
    fprintf(stderr, "missing '%s' for %s\n", message, text);
    diagnostics_print(&r.errors, stderr);
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  return r;
}

#endif
