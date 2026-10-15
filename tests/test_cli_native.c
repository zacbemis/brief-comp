#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include "test_utils.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  Arena *arena;
  const char *brief, *cc, *root, *temp;
} Cli;
static int directory_clean(const char *path, const char *prefix) {
  DIR *dir = opendir(path);
  if (!dir)
    return 0;
  int clean = 1;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (!entry) {
      if (errno)
        clean = 0;
      break;
    }
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") &&
        (!prefix || !strncmp(entry->d_name, prefix, strlen(prefix))))
      clean = 0;
  }
  if (closedir(dir) != 0)
    clean = 0;
  return clean;
}
static ProcessResult call(Cli *c, const char *const args[], int status,
                          const TestEnv *extra, size_t count) {
  const char *argv[32] = {c->brief};
  size_t n = 1;
  for (size_t i = 0; args[i] && n < 31; i++)
    argv[n++] = args[i];
  TestEnv env[16] = {{"CC", c->cc}, {"TMPDIR", c->temp}};
  for (size_t i = 0; i < count; i++)
    env[2 + i] = extra[i];
  ProcessOptions options = {
      .cwd = c->root, .env = env, .env_count = count + 2, .timeout_ms = 30000};
  ProcessResult r = process_run(c->arena, argv, &options);
  ASSERT_TRUE(r.ok);
  ASSERT_EQ_INT(status, r.status);
  ASSERT_TRUE(!sanitizer_report(r.err));
  ASSERT_TRUE(directory_clean(c->temp, NULL));
  ASSERT_TRUE(directory_clean(c->root, ".brief-"));
  if (status == 0)
    ASSERT_EQ_INT(0, r.err.length);
  if (!r.ok && r.error)
    fprintf(stderr, "%s\n", r.error);
  if (r.status != status && r.err.length)
    fwrite(r.err.data, 1, r.err.length, stderr);
  return r;
}
static TestBytes read_file(Arena *a, const char *path) {
  int ok;
  TestBytes bytes = test_read_file(a, path, &ok);
  ASSERT_TRUE(ok);
  return bytes;
}
static void expect_binary(Cli *c, const char *binary) {
  const char *argv[] = {binary, NULL};
  ProcessOptions options = {.cwd = c->root, .timeout_ms = 10000};
  ProcessResult r = process_run(c->arena, argv, &options);
  ASSERT_TRUE(r.ok);
  ASSERT_EQ_INT(0, r.status);
  ASSERT_TRUE(bytes_equal(TEST_BYTES("Hello\n"), r.out));
  ASSERT_EQ_INT(0, r.err.length);
}
static int argument_recorded(TestBytes record, const char *argument) {
  size_t offset = 0, length = strlen(argument);
  while (offset < record.length) {
    const unsigned char *end =
        memchr(record.data + offset, 0, record.length - offset);
    if (!end)
      return 0;
    size_t n = (size_t)(end - record.data - offset);
    if (n == length && !memcmp(record.data + offset, argument, n))
      return 1;
    offset += n + 1;
  }
  return 0;
}
int main(int argc, char **argv) {
  if ((argc != 4 && argc != 5) ||
      (argc == 5 && strcmp(argv[4], "--debug-supported"))) {
    fputs("usage: test_cli_native brief cc fake_cc [--debug-supported]\n",
          stderr);
    return 2;
  }
  Arena a;
  arena_init(&a);
  char root[] = "/tmp/brief cli ' $ ; XXXXXX";
  if (!mkdtemp(root)) {
    perror("mkdtemp");
    arena_destroy(&a);
    return 1;
  }
  char *temp = test_path(&a, root, "temporary files"),
       *source_dir = test_path(&a, root, "??");
  char *source = test_path(&a, source_dir, "hello ' \" ; $x \\ path.brf");
  char *brief = test_path(&a, root, "brief copy"),
       *wrapper = test_path(&a, root, "cc wrapper ; $name");
  TestBytes source_text = TEST_BYTES("main() { print(\"Hello\") }\n");
  if (mkdir(temp, 0700) != 0 || mkdir(source_dir, 0700) != 0 ||
      !test_write_file(source, source_text) ||
      !test_copy_file(&a, argv[1], brief) || chmod(brief, 0700) != 0 ||
      !test_copy_file(&a, argv[3], wrapper) || chmod(wrapper, 0700) != 0) {
    perror("native test setup");
    test_remove_tree(root);
    arena_destroy(&a);
    return 1;
  }
  Cli c = {&a, brief, argv[2], root, temp};
  char *output = test_path(&a, root, "output ' ; $name");
  char *record = test_path(&a, root, "cc.args");
  TestEnv wrapped[] = {{"CC", wrapper},
                       {"REAL_CC", argv[2]},
                       {"RECORD", record},
                       {"MODE", "compile"},
                       {"FAKE_EXIT_STATUS", "7"}};
  TEST("run and standalone C emission work after relocating the compiler") {
    ProcessResult r =
        call(&c, (const char *[]){"run", source, NULL}, 0, NULL, 0);
    ASSERT_TRUE(bytes_equal(r.out, TEST_BYTES("Hello\n")));
    r = call(&c, (const char *[]){"emit-c", source, NULL}, 0, NULL, 0);
    ASSERT_TRUE(bytes_contains(r.out, TEST_BYTES("brt_print")));
    ASSERT_TRUE(bytes_contains(r.out, TEST_BYTES("void brf_main(void)")));
    ASSERT_TRUE(bytes_contains(r.out, TEST_BYTES("= \"Hello\";")));
    char *file = test_path(&a, root, "standalone.c"),
         *binary = test_path(&a, root, "standalone");
    ASSERT_TRUE(test_write_file(file, r.out));
    const char *cc[] = {argv[2],   "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                        "-Werror", file,       "-o",    binary,    NULL};
    ProcessResult compiled = process_run(&a, cc, NULL);
    ASSERT_TRUE(compiled.ok);
    ASSERT_EQ_INT(0, compiled.status);
    ASSERT_EQ_INT(0, compiled.out.length);
    ASSERT_EQ_INT(0, compiled.err.length);
    expect_binary(&c, binary);
  }
  TEST("build uses a.out by default and accepts an explicit path") {
    call(&c, (const char *[]){"build", source, NULL}, 0, NULL, 0);
    expect_binary(&c, test_path(&a, root, "a.out"));
    call(&c, (const char *[]){"build", source, "-o", output, NULL}, 0, NULL, 0);
    expect_binary(&c, output);
  }
  TEST("source files cannot be overwritten directly or through symlinks") {
    call(&c, (const char *[]){"build", source, "-o", source, NULL}, 1, NULL, 0);
    ASSERT_TRUE(bytes_equal(source_text, read_file(&a, source)));
    char *alias = test_path(&a, root, "source alias");
    ASSERT_EQ_INT(0, symlink(source, alias));
    call(&c, (const char *[]){"build", source, "-o", alias, NULL}, 1, NULL, 0);
    ASSERT_TRUE(bytes_equal(source_text, read_file(&a, source)));
  }
  TEST("the stand-in compiler records the native flags without a shell") {
    call(&c, (const char *[]){"build", source, "-o", output, NULL}, 0, wrapped,
         5);
    TestBytes normal = read_file(&a, record);
    ASSERT_TRUE(argument_recorded(normal, "-std=c11"));
    ASSERT_TRUE(argument_recorded(normal, "-O2"));
    ASSERT_TRUE(!argument_recorded(normal, "-fsanitize=address,undefined"));
    if (argc == 5) {
      call(&c, (const char *[]){"build", "--debug", source, "-o", output, NULL},
           0, wrapped, 5);
      TestBytes all = read_file(&a, record);
      ASSERT_TRUE(all.length > normal.length);
      if (all.length > normal.length) {
        ASSERT_TRUE(bytes_equal(normal, (TestBytes){all.data, normal.length}));
        TestBytes debug = {all.data + normal.length,
                           all.length - normal.length};
        ASSERT_TRUE(argument_recorded(debug, "-O0"));
        ASSERT_TRUE(argument_recorded(debug, "-g"));
        ASSERT_TRUE(argument_recorded(debug, "-fsanitize=address,undefined"));
        ASSERT_TRUE(argument_recorded(debug, "-fno-sanitize-recover=all"));
      }
    }
  }
  TEST("compiler failures preserve existing output; run propagates status") {
    TestBytes before = read_file(&a, output);
    wrapped[3].value = "fail";
    call(&c, (const char *[]){"build", source, "-o", output, NULL}, 1, wrapped,
         5);
    ASSERT_TRUE(bytes_equal(before, read_file(&a, output)));
    wrapped[3].value = "exit";
    call(&c, (const char *[]){"run", source, NULL}, 7, wrapped, 5);
    wrapped[3].value = "compile";
    TestEnv missing_cc = {"CC", test_path(&a, root, "missing cc")};
    call(&c, (const char *[]){"run", source, NULL}, 1, &missing_cc, 1);
    TestEnv missing_tmp = {"TMPDIR", test_path(&a, root, "missing temp")};
    call(&c, (const char *[]){"run", source, NULL}, 1, &missing_tmp, 1);
    char *bad = test_path(&a, root, "invalid.brf");
    ASSERT_TRUE(test_write_file(bad, TEST_BYTES("main() { missing() }\n")));
    TestBytes calls_before = read_file(&a, record);
    ProcessResult r = call(
        &c, (const char *[]){"build", bad, "-o", output, NULL}, 1, wrapped, 5);
    ASSERT_TRUE(bytes_contains(r.err, TEST_BYTES("Unknown function.")));
    ASSERT_EQ_INT(0, r.out.length);
    ASSERT_TRUE(bytes_equal(calls_before, read_file(&a, record)));
    ASSERT_TRUE(bytes_equal(before, read_file(&a, output)));
    r = call(&c, (const char *[]){"emit-c", bad, NULL}, 1, NULL, 0);
    ASSERT_EQ_INT(0, r.out.length);
  }
  TEST("Ctrl-C reaches the program but brief survives to remove temporary "
       "files") {
    struct sigaction defaults = {.sa_handler = SIG_DFL},
                     previous = {.sa_handler = SIG_DFL};
    sigemptyset(&defaults.sa_mask);
    ASSERT_EQ_INT(0, sigaction(SIGINT, &defaults, &previous));
    sigset_t interrupt, previous_mask;
    sigemptyset(&interrupt);
    sigemptyset(&previous_mask);
    sigaddset(&interrupt, SIGINT);
    ASSERT_EQ_INT(0, sigprocmask(SIG_UNBLOCK, &interrupt, &previous_mask));
    wrapped[3].value = "exit";
    TestEnv env[6];
    memcpy(env, wrapped, sizeof(wrapped));
    env[5] = (TestEnv){"FAKE_GROUP_SIGINT", "1"};
    ProcessResult r =
        call(&c, (const char *[]){"run", source, NULL}, 128 + SIGINT, env, 6);
    ASSERT_EQ_INT(0, r.out.length);
    ASSERT_EQ_INT(0, r.err.length);
    wrapped[3].value = "compile";
    ASSERT_EQ_INT(0, sigaction(SIGINT, &previous, NULL));
    ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, &previous_mask, NULL));
  }
  TEST("invalid options and -- before a leading-dash filename") {
    const char *bad[][7] = {{"build", NULL},
                            {"build", source, "-o", NULL},
                            {"run", source, "-o", output, NULL},
                            {"emit-c", source, "--debug", NULL},
                            {"build", source, source, NULL},
                            {"build", source, "--unknown", NULL}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      call(&c, bad[i], 2, NULL, 0);
    ASSERT_TRUE(
        test_write_file(test_path(&a, root, "-hello.brf"), source_text));
    ProcessResult r =
        call(&c, (const char *[]){"run", "--", "-hello.brf", NULL}, 0, NULL, 0);
    ASSERT_TRUE(bytes_equal(r.out, TEST_BYTES("Hello\n")));
  }
  ASSERT_TRUE(test_remove_tree(root));
  arena_destroy(&a);
  RUN_TESTS();
}
