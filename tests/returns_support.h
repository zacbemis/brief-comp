#ifndef BRIEF_TESTS_RETURNS_SUPPORT_H
#define BRIEF_TESTS_RETURNS_SUPPORT_H

#include "compile_support.h"

// Exactly one fix-free diagnostic, stable across rechecks, with LF and CRLF
// endings. A NULL site expects the error at the final '}' with one note;
// otherwise the error starts at site and has no notes.
static inline void rejects(Arena *a, const char *text, const char *message,
                           const char *site) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *source = lines(a, text, crlf);
    const char *at =
        site ? strstr(source, lines(a, site, crlf)) : strrchr(source, '}');
    ASSERT_TRUE(at != NULL);
    Result r = check(a, source);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat)
        recheck(a, &r);
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(source, r.program->module->text));
      if (r.errors.count != 1)
        continue;
      Diagnostic *d = r.errors.items[0];
      ASSERT_EQ_INT(0, strcmp(message, d->message));
      if (at)
        ASSERT_EQ_INT((size_t)(at - source), d->span.start);
      ASSERT_TRUE(!d->fixes);
      if (site)
        ASSERT_TRUE(!d->notes);
      else
        ASSERT_TRUE(d->notes && !d->notes->next);
    }
  }
}

#endif
