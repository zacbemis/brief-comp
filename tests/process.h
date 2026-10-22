#ifndef BRIEF_TEST_PROCESS_H
#define BRIEF_TEST_PROCESS_H

#include "common/arena.h"

typedef struct {
  const unsigned char *data;
  size_t length;
} TestBytes;
#define TEST_BYTES(s) ((TestBytes){(const unsigned char *)(s), sizeof(s) - 1})
typedef struct {
  const char *name;
  const char *value;
} TestEnv;
typedef struct {
  const char *cwd;
  const TestEnv *env; // Child-only overrides; NULL value unsets a variable.
  size_t env_count;
  unsigned timeout_ms;
  int merge_stderr; // dup stderr onto stdout's shared capture; err stays empty.
} ProcessOptions;
typedef struct {
  int ok;
  int status; // Exit code, or negative signal number (as in subprocess.run).
  int timed_out;
  TestBytes out, err;
  const char *error;
} ProcessResult;

// POSIX, shell-free. Captures are binary-safe, arena-owned and NUL-terminated
// for convenience. Each stream is capped at 16 MiB. Timeout/failure kills the
// child's process group and reaps the child. Default timeout: 10 seconds.
ProcessResult process_run(Arena *arena, const char *const argv[],
                          const ProcessOptions *options);
int bytes_equal(TestBytes a, TestBytes b);
int bytes_contains(TestBytes bytes, TestBytes needle);
int sanitizer_report(TestBytes bytes);
TestBytes test_read_file(Arena *arena, const char *path, int *ok);
int test_write_file(const char *path, TestBytes bytes);
int test_copy_file(Arena *arena, const char *from, const char *to);
char *test_path(Arena *arena, const char *directory, const char *name);
int test_remove_tree(const char *path); // Does not follow symlinks.

#endif
