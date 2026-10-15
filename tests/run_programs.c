#define _POSIX_C_SOURCE 200809L
#include "program_expect.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  char **items;
  size_t count, capacity;
} Paths;
static int collect(Arena *a, const char *path, Paths *paths) {
  struct stat st;
  if (lstat(path, &st) != 0) {
    perror(path);
    return 0;
  }
  if (S_ISREG(st.st_mode)) {
    size_t n = strlen(path);
    if (n < 4 || strcmp(path + n - 4, ".brf"))
      return 1;
    if (paths->count == paths->capacity) {
      size_t capacity = paths->capacity ? paths->capacity * 2 : 16;
      char **items = arena_calloc(a, capacity, sizeof(*items));
      if (paths->count)
        memcpy(items, paths->items, paths->count * sizeof(*items));
      paths->items = items;
      paths->capacity = capacity;
    }
    paths->items[paths->count++] = arena_strdup(a, path);
    return 1;
  }
  if (!S_ISDIR(st.st_mode))
    return 1;
  DIR *dir = opendir(path);
  if (!dir) {
    perror(path);
    return 0;
  }
  int ok = 1;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (!entry) {
      if (errno)
        ok = 0;
      break;
    }
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
      if (!collect(a, test_path(a, path, entry->d_name), paths))
        ok = 0;
  }
  if (closedir(dir) != 0)
    ok = 0;
  return ok;
}
static int compare_paths(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}
static int check(const char *brief, const char *cc, const char *path,
                 int debug) {
  Arena a;
  arena_init(&a);
  int ok;
  TestBytes source = test_read_file(&a, path, &ok);
  Expectations e;
  const char *failure =
      ok ? parse_expectations(&a, source, &e) : "cannot read source";
  char *directory = NULL;
  ProcessResult compiled = {0}, ran = {0};
  if (!failure) {
    const char *tmp = getenv("TMPDIR");
    directory =
        test_path(&a, tmp && *tmp ? tmp : "/tmp", "brief-program-XXXXXX");
    if (!mkdtemp(directory)) {
      failure = "cannot create temporary directory";
      directory = NULL;
    }
  }
  if (!failure) {
    char *binary = test_path(&a, directory, "program");
    const char *command[] = {
        brief, "build", path, "-o", binary, debug ? "--debug" : NULL, NULL};
    TestEnv env = {"CC", cc};
    ProcessOptions options = {.env = &env, .env_count = 1, .timeout_ms = 30000};
    compiled = process_run(&a, command, &options);
    int exists = access(binary, F_OK) == 0;
    ProcessResult *result = NULL;
    if (compiled.ok && compiled.status == 0 && !e.error_count) {
      const char *run[] = {binary, NULL};
      options.timeout_ms = 10000;
      options.env_count = 0;
      ran = process_run(&a, run, &options);
      result = &ran;
    }
    failure = check_result(&a, &e, &compiled, result, exists);
  }
  if (directory && !test_remove_tree(directory) && !failure)
    failure = "cannot remove temporary directory";
  if (failure) {
    fprintf(stderr, "FAIL %s: %s\n", path, failure);
    if (compiled.err.length)
      fwrite(compiled.err.data, 1, compiled.err.length, stderr);
    if (ran.err.length)
      fwrite(ran.err.data, 1, ran.err.length, stderr);
  }
  int passed = failure == NULL;
  arena_destroy(&a);
  return passed;
}
int main(int argc, char **argv) {
  const char *brief = NULL, *cc = NULL, *programs = NULL;
  int debug = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--debug"))
      debug = 1;
    else if (i + 1 < argc && !strcmp(argv[i], "--brief"))
      brief = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--cc"))
      cc = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--programs"))
      programs = argv[++i];
    else {
      fputs("invalid program-runner arguments\n", stderr);
      return 2;
    }
  }
  if (!brief || !cc || !programs) {
    fputs("usage: run_programs --brief executable --cc compiler --programs "
          "directory [--debug]\n",
          stderr);
    return 2;
  }
  Arena a;
  arena_init(&a);
  Paths paths = {0};
  if (!collect(&a, programs, &paths) || !paths.count) {
    fputs("cannot enumerate program tests (or no .brf files found)\n", stderr);
    arena_destroy(&a);
    return 1;
  }
  qsort(paths.items, paths.count, sizeof(*paths.items), compare_paths);
  size_t failures = 0;
  for (size_t i = 0; i < paths.count; i++)
    if (!check(brief, cc, paths.items[i], debug))
      failures++;
  printf("%zu program tests, %zu failures%s\n", paths.count, failures,
         debug ? " (sanitized)" : "");
  arena_destroy(&a);
  return failures != 0;
}
