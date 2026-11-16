#ifndef BRIEF_TESTS_STRUCTS_SUPPORT_H
#define BRIEF_TESTS_STRUCTS_SUPPORT_H

#include "ir/ir.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/sema.h"
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
  SourceFile *s = source_create(a, "structs.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
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
static inline char *apply_call_fixes(Arena *a, const char *text,
                                     const Diagnostic *d) {
  const DiagnosticFix *fixes[16];
  size_t n = 0, length = strlen(text) + 1;
  for (const DiagnosticFix *f = d->fixes; f; f = f->next) {
    ASSERT_TRUE(n < 16);
    if (n == 16)
      return NULL;
    size_t i = n++;
    while (i && fixes[i - 1]->span.start > f->span.start) {
      fixes[i] = fixes[i - 1];
      i--;
    }
    fixes[i] = f;
    length += strlen(f->replacement);
  }
  char *out = arena_alloc(a, length);
  size_t at = 0, from = 0;
  for (size_t i = 0; i < n; i++) {
    const DiagnosticFix *f = fixes[i];
    ASSERT_TRUE(from <= f->span.start && f->span.end <= strlen(text));
    if (from > f->span.start || f->span.end > strlen(text))
      return NULL;
    size_t bytes = f->span.start - from;
    memcpy(out + at, text + from, bytes);
    at += bytes;
    bytes = strlen(f->replacement);
    memcpy(out + at, f->replacement, bytes);
    at += bytes;
    from = f->span.end;
  }
  strcpy(out + at, text + from);
  return out;
}
static inline Result method_error(Arena *a, const char *text, const char *site,
                                  const char *advice) {
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != 1) {
      fprintf(stderr, "method diagnostic case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count == 1) {
      Diagnostic *d = r.errors.items[0];
      const char *call = strstr(text, site);
      ASSERT_TRUE(call != NULL);
      ASSERT_EQ_STRN("Unknown method for this type.", d->message,
                     strlen(d->message));
      if (call) {
        ASSERT_EQ_INT((size_t)(call - text), d->span.start);
        ASSERT_EQ_INT((size_t)(call - text) + strlen(site), d->span.end);
      }
      if (advice) {
        ASSERT_TRUE(d->notes != NULL);
        if (d->notes) {
          ASSERT_EQ_STRN(advice, d->notes->message, strlen(d->notes->message));
          ASSERT_TRUE(d->notes->next == NULL);
          SemaSymbol *candidate = NULL;
          for (size_t i = 0; i < r.program->count; i++) {
            SemaSymbol *s = &r.program->functions[i].symbol;
            if (!s->owner && d->notes->span.start == s->span.start)
              candidate = s;
          }
          ASSERT_TRUE(candidate != NULL);
          if (candidate) {
            ASSERT_TRUE(d->notes->span.file == candidate->span.file);
            ASSERT_EQ_INT(candidate->span.end, d->notes->span.end);
          }
        }
      } else
        ASSERT_TRUE(d->notes == NULL);
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
  return r;
}
static inline Result recursive_lookup_error(Arena *a, const char *text,
                                            const char *site,
                                            const char *message,
                                            const char *advice) {
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != 1) {
      fprintf(stderr, "recursive lookup case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count == 1) {
      Diagnostic *d = r.errors.items[0];
      const char *at = strstr(text, site);
      ASSERT_TRUE(at != NULL);
      ASSERT_EQ_STRN(message, d->message, strlen(d->message));
      if (at) {
        ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_EQ_INT((size_t)(at - text) + strlen(site), d->span.end);
      }
      if (advice) {
        ASSERT_TRUE(d->notes != NULL);
        if (d->notes) {
          ASSERT_EQ_STRN(advice, d->notes->message, strlen(d->notes->message));
          ASSERT_TRUE(d->notes->next == NULL);
        }
      } else
        ASSERT_TRUE(d->notes == NULL);
    }
    SemaFunction *f = NULL, *g = NULL;
    for (size_t i = 0; i < r.program->count; i++) {
      SemaFunction *fn = &r.program->functions[i];
      if (!strcmp(fn->symbol.name, "f"))
        f = fn;
      if (!strcmp(fn->symbol.name, "g"))
        g = fn;
      ASSERT_TRUE(!fn->inference_conflict);
    }
    ASSERT_TRUE(f != NULL);
    if (f) {
      ASSERT_TRUE(f->symbol.return_type == f->symbol.params[0]->type);
      if (g) {
        ASSERT_EQ_INT(f->component, g->component);
        ASSERT_EQ_INT(SEMA_ERROR, g->symbol.return_type->kind);
      }
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
  return r;
}

#endif
