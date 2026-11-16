#ifndef BRIEF_TESTS_ERRORS_SUPPORT_H
#define BRIEF_TESTS_ERRORS_SUPPORT_H

#include "compile_support.h"

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
