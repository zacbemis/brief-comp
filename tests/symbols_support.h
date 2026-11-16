#ifndef BRIEF_TESTS_SYMBOLS_SUPPORT_H
#define BRIEF_TESTS_SYMBOLS_SUPPORT_H

#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/internal.h"
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
  SourceFile *s = source_create(a, "symbols.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  ASSERT_TRUE(r.program->module == s);
  ASSERT_EQ_INT(1, r.program->ok->id);
  size_t id = r.program->tag_count;
  for (const SemaTag *tag = r.program->tags; tag; tag = tag->next) {
    if (!strstr(text, ":!")) {
      if (tag->module && !tag->kind_span.file) {
        ASSERT_TRUE(!strcmp(tag->name, "invalid") ||
                    !strcmp(tag->name, "overflow"));
        ASSERT_TRUE(tag->kind_declared);
        ASSERT_EQ_INT(SEMA_TAG_ERROR, tag->kind);
      } else
        ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, tag->kind);
    }
    ASSERT_EQ_INT(id, tag->id);
    id--;
    ASSERT_TRUE(
        tag->module ==
        ((!strcmp(tag->name, "ok") || !strcmp(tag->name, "none")) ? NULL : s));
  }
  ASSERT_EQ_INT(0, id);
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
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(!emit_c(out, ir));
      ir_insert_rc(a, ir);
      size_t count = ir->functions[0].count;
      ir_insert_rc(a, ir);
      ASSERT_EQ_INT(count, ir->functions[0].count);
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
static inline IrProgram *owned_ir(Arena *a, SemaProgram *p) {
  ASSERT_TRUE(p->checked);
  IrProgram *ir = ir_lower(a, p);
  ASSERT_TRUE(ir != NULL);
  if (!ir)
    return NULL;
  ir_insert_rc(a, ir);
  FILE *out = tmpfile();
  ASSERT_TRUE(out != NULL);
  if (out) {
    ASSERT_TRUE(emit_c(out, ir));
    fclose(out);
  }
  size_t count = ir->functions[0].count;
  ir_insert_rc(a, ir);
  ASSERT_EQ_INT(count, ir->functions[0].count);
  return ir;
}
// Build internal semantic trees for independent checking and lowering tests.
static inline SemaExpr *copy_expr(Arena *a, const SemaExpr *source) {
  SemaExpr *copy = arena_alloc(a, sizeof(*copy));
  *copy = *source;
  return copy;
}
static inline SemaVar *extra_binding(Arena *a, SemaProgram *p, SemaFunction *f,
                                     const char *name, Span span) {
  SemaVar *v = arena_calloc(a, 1, sizeof(*v));
  v->name = name;
  v->span = span;
  v->id = f->variable_count;
  v->type = v->annotation = &p->types[SEMA_UNKNOWN];
  v->data_mutable = 1; // Local binding; flow carries read-only provenance.
  SemaVar **variables =
      arena_calloc(a, f->variable_count + 1, sizeof(*variables));
  memcpy(variables, f->variables, f->variable_count * sizeof(*variables));
  variables[f->variable_count++] = v;
  f->variables = variables;
  f->variable_capacity = f->variable_count;
  return v;
}
static inline void recheck(Arena *a, SemaProgram *p, Diagnostics *errors) {
  diagnostics_init(errors, a);
  sema_check(p, errors);
}
// Permission diagnostics must identify the argument/RHS, not the call or
// destination field, and report once after a loop fixpoint on every check.
static inline void permission_diagnostic(Arena *a, const char *text,
                                         const char *site, const char *value,
                                         const char *message) {
  const char *at = strstr(text, site), *within = strstr(site, value);
  ASSERT_TRUE(at != NULL && within != NULL);
  if (!at || !within)
    return;
  ASSERT_TRUE(strstr(at + strlen(site), site) == NULL);
  size_t start = (size_t)(at - text) + (size_t)(within - site);
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned pass = 0; pass < 3; pass++) {
    Diagnostics again;
    diagnostics_init(&again, a);
    const Diagnostics *errors = &r.errors;
    if (pass == 1) {
      sema_check_permissions(r.program, &again);
      errors = &again;
    } else if (pass == 2) {
      sema_check(r.program, &again);
      errors = &again;
    }
    if (errors->count != 1) {
      fprintf(stderr, "permission case: %s\n", text);
      diagnostics_print(errors, stderr);
    }
    ASSERT_EQ_INT(1, errors->count);
    if (errors->count) {
      ASSERT_EQ_STRN(message, errors->items[0]->message,
                     strlen(errors->items[0]->message));
      ASSERT_EQ_INT(start, errors->items[0]->span.start);
      ASSERT_EQ_INT(start + strlen(value), errors->items[0]->span.end);
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
}

#endif
