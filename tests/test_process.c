#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include "test_utils.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fputs("usage: test_process fake_cc\n", stderr);
    return 2;
  }
  Arena a;
  arena_init(&a);
  TEST("stdout and stderr captures exceed pipe buffers and retain zero bytes") {
    const char *command[] = {argv[1], "--helper", "large", NULL};
    ProcessResult r = process_run(&a, command, NULL);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ_INT(0, r.status);
    ASSERT_EQ_INT(64 * 4096, r.out.length);
    ASSERT_EQ_INT(64 * 4096, r.err.length);
    for (size_t i = 0; i < r.out.length; i++)
      if (r.out.data[i] != (i % 4096 ? 'o' : 0)) {
        FAIL("stdout bytes changed");
        break;
      }
    for (size_t i = 0; i < r.err.length; i++)
      if (r.err.data[i] != (i % 4096 ? 'e' : 0)) {
        FAIL("stderr bytes changed");
        break;
      }
  }
  TEST("merged capture uses a shared descriptor and preserves binary bytes") {
    const char *command[] = {argv[1], "--helper", "large", NULL};
    ProcessOptions options = {.merge_stderr = 1};
    ProcessResult r = process_run(&a, command, &options);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ_INT(0, r.status);
    ASSERT_EQ_INT(0, r.err.length);
    ASSERT_EQ_INT(128 * 4096, r.out.length);
    size_t zeros = 0, outs = 0, errs = 0;
    for (size_t i = 0; i < r.out.length; i++) {
      zeros += r.out.data[i] == 0;
      outs += r.out.data[i] == 'o';
      errs += r.out.data[i] == 'e';
    }
    ASSERT_EQ_INT(128, zeros);
    ASSERT_EQ_INT(64 * 4095, outs);
    ASSERT_EQ_INT(64 * 4095, errs);
  }
  TEST("observed exits are collected after group cleanup, without zombies") {
    const char *command[] = {argv[1], NULL};
    TestEnv env[] = {{"FAKE_EXIT_STATUS", "23"}, {"FAKE_GROUP_SIGINT", NULL}};
    ProcessOptions options = {.env = env, .env_count = 2};
    for (int i = 0; i < 16; i++) {
      ProcessResult r = process_run(&a, command, &options);
      ASSERT_TRUE(r.ok);
      ASSERT_EQ_INT(23, r.status);
      errno = 0;
      ASSERT_EQ_INT(-1, waitpid(-1, NULL, WNOHANG));
      ASSERT_EQ_INT(ECHILD, errno);
    }
  }
  TEST("timeouts kill and reap the child") {
    const char *command[] = {argv[1], "--helper", "sleep", NULL};
    ProcessOptions options = {.timeout_ms = 100};
    ProcessResult r = process_run(&a, command, &options);
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(r.timed_out);
    ASSERT_EQ_INT(-SIGKILL, r.status);
  }
  TEST("signal termination and exec failure are distinct from success") {
    const char *command[] = {argv[1], "--helper", "signal", NULL};
    ProcessResult r = process_run(&a, command, NULL);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ_INT(-SIGTERM, r.status);
    const char *missing[] = {"/no/such/brief-test-command", NULL};
    r = process_run(&a, missing, NULL);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ_INT(127, r.status);
    ASSERT_TRUE(r.err.length != 0);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
