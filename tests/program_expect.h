#ifndef BRIEF_PROGRAM_EXPECT_H
#define BRIEF_PROGRAM_EXPECT_H

#include "process.h"

typedef struct ExpectedMessage ExpectedMessage;
struct ExpectedMessage {
  size_t line;
  TestBytes text;
  ExpectedMessage *next;
};
typedef struct {
  TestBytes output, standard_error;
  ExpectedMessage *errors, *panics;
  size_t error_count, panic_count;
  int has_exit, exit_status;
} Expectations;

// Pure, binary-safe test logic. Messages borrow source bytes; all other storage
// is arena-owned. NULL means success; otherwise a failure reason.
const char *parse_expectations(Arena *arena, TestBytes source,
                               Expectations *result);
const char *check_result(Arena *arena, const Expectations *expected,
                         const ProcessResult *compiled,
                         const ProcessResult *ran, int executable_exists);
#endif
