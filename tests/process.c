#define _POSIX_C_SOURCE 200809L
#include "process.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int bytes_equal(TestBytes a, TestBytes b) {
  return a.length == b.length &&
         (!a.length || memcmp(a.data, b.data, a.length) == 0);
}
int bytes_contains(TestBytes b, TestBytes needle) {
  if (needle.length > b.length)
    return 0;
  for (size_t i = 0; i <= b.length - needle.length; i++)
    if (!needle.length || memcmp(b.data + i, needle.data, needle.length) == 0)
      return 1;
  return 0;
}
int sanitizer_report(TestBytes b) {
  return bytes_contains(b, TEST_BYTES("AddressSanitizer")) ||
         bytes_contains(b, TEST_BYTES("UndefinedBehaviorSanitizer")) ||
         bytes_contains(b, TEST_BYTES("LeakSanitizer")) ||
         bytes_contains(b, TEST_BYTES("runtime error:"));
}
static TestBytes read_stream(Arena *a, FILE *file, int *ok) {
  size_t length = 0, capacity = 4096;
  unsigned char *data = arena_alloc(a, capacity);
  for (;;) {
    size_t n = fread(data + length, 1, capacity - length - 1, file);
    length += n;
    if (ferror(file)) {
      *ok = 0;
      return (TestBytes){0};
    }
    if (feof(file))
      break;
    if (capacity > SIZE_MAX / 2) {
      *ok = 0;
      return (TestBytes){0};
    }
    unsigned char *next = arena_alloc(a, capacity * 2);
    memcpy(next, data, length);
    data = next;
    capacity *= 2;
  }
  data[length] = 0;
  *ok = 1;
  return (TestBytes){data, length};
}
TestBytes test_read_file(Arena *a, const char *path, int *ok) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    *ok = 0;
    return (TestBytes){0};
  }
  TestBytes b = read_stream(a, f, ok);
  if (fclose(f) != 0)
    *ok = 0;
  return b;
}
int test_write_file(const char *path, TestBytes bytes) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  int ok =
      !bytes.length || fwrite(bytes.data, 1, bytes.length, f) == bytes.length;
  return fclose(f) == 0 && ok;
}
int test_copy_file(Arena *a, const char *from, const char *to) {
  int ok;
  TestBytes b = test_read_file(a, from, &ok);
  return ok && test_write_file(to, b);
}
char *test_path(Arena *a, const char *dir, const char *name) {
  size_t size = strlen(dir) + strlen(name) + 2;
  char *path = arena_alloc(a, size);
  snprintf(path, size, "%s/%s", dir, name);
  return path;
}
int test_remove_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0)
    return errno == ENOENT;
  if (!S_ISDIR(st.st_mode))
    return unlink(path) == 0;
  DIR *dir = opendir(path);
  if (!dir)
    return 0;
  int ok = 1;
  Arena a;
  arena_init(&a);
  struct dirent *entry;
  errno = 0;
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
      if (!test_remove_tree(test_path(&a, path, entry->d_name)))
        ok = 0;
    errno = 0;
  }
  if (errno)
    ok = 0;
  if (closedir(dir) != 0)
    ok = 0;
  arena_destroy(&a);
  return rmdir(path) == 0 && ok;
}
static int64_t milliseconds(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
    return -1;
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
ProcessResult process_run(Arena *a, const char *const argv[],
                          const ProcessOptions *options) {
  ProcessOptions defaults = {.timeout_ms = 10000};
  if (!options)
    options = &defaults;
  ProcessResult r = {.status = -1};
  FILE *out = tmpfile(), *err = tmpfile();
  if (!out || !err) {
    r.error = "cannot create process capture files";
    if (out)
      fclose(out);
    if (err)
      fclose(err);
    return r;
  }
  int outfd = fileno(out), errfd = fileno(err);
  if (fcntl(outfd, F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl(errfd, F_SETFD, FD_CLOEXEC) < 0) {
    r.error = "cannot mark capture descriptors close-on-exec";
    goto done;
  }
  int64_t start = milliseconds();
  if (start < 0) {
    r.error = "cannot read monotonic clock";
    goto done;
  }
  pid_t pid = fork();
  if (pid < 0) {
    r.error = "cannot fork process";
    goto done;
  }
  if (!pid) {
    if (setpgid(0, 0) != 0 || dup2(outfd, STDOUT_FILENO) < 0 ||
        dup2(options->merge_stderr ? outfd : errfd, STDERR_FILENO) < 0)
      _exit(126);
    close(outfd);
    close(errfd);
    int input = open("/dev/null", O_RDONLY);
    if (input < 0 || dup2(input, STDIN_FILENO) < 0)
      _exit(126);
    if (input != STDIN_FILENO)
      close(input);
    if (options->cwd && chdir(options->cwd) != 0) {
      perror("chdir");
      _exit(126);
    }
    for (size_t i = 0; i < options->env_count; i++) {
      TestEnv e = options->env[i];
      if ((e.value ? setenv(e.name, e.value, 1) : unsetenv(e.name)) != 0) {
        perror("environment");
        _exit(126);
      }
    }
    execvp(argv[0], (char *const *)argv);
    perror(argv[0]);
    _exit(127);
  }
  // The child also sets its group before exec, closing the timeout race.
  if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH) {
    r.error = "cannot create process group";
    kill(pid, SIGKILL);
  }
  int status = 0, exited = 0, child_owned = 1;
  while (!r.error) {
    siginfo_t info = {0};
    int waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT);
    if (waited == 0 && info.si_pid == pid) {
      exited = 1;
      break;
    }
    if (waited < 0 && errno != EINTR) {
      // If somebody else already collected the child, its ID is no longer
      // ours to signal. The standalone test tools normally have no reaper.
      if (errno == ECHILD)
        child_owned = 0;
      r.error = "waitid failed";
      break;
    }
    int64_t now = milliseconds();
    if (now < 0) {
      r.error = "cannot read monotonic clock";
      break;
    }
    if (now - start >=
        (int64_t)(options->timeout_ms ? options->timeout_ms : 10000)) {
      r.timed_out = 1;
      r.error = "process timed out";
      break;
    }
    struct stat os, es;
    if (fstat(outfd, &os) != 0 || fstat(errfd, &es) != 0) {
      r.error = "cannot inspect captures";
      break;
    }
    if (os.st_size > 16 * 1024 * 1024 || es.st_size > 16 * 1024 * 1024) {
      r.error = "process output exceeded 16 MiB";
      break;
    }
    struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
  }
  // WNOWAIT keeps the leader's PID reserved, even after exit. Kill its group
  // before collecting it, so a recycled ID can never target an unrelated group.
  if (child_owned) {
    kill(-pid, SIGKILL);
    if (!exited)
      kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0) {
      if (errno == EINTR)
        continue;
      r.error = "cannot reap process";
      break;
    }
  }
  if (WIFEXITED(status))
    r.status = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    r.status = -WTERMSIG(status);
  struct stat final_out, final_err;
  if (fstat(outfd, &final_out) != 0 || fstat(errfd, &final_err) != 0 ||
      final_out.st_size > 16 * 1024 * 1024 ||
      final_err.st_size > 16 * 1024 * 1024) {
    r.error = "capture unavailable or exceeded 16 MiB";
    goto done;
  }
  if (fseek(out, 0, SEEK_SET) != 0 || fseek(err, 0, SEEK_SET) != 0) {
    r.error = "cannot rewind captures";
    goto done;
  }
  int ok;
  r.out = read_stream(a, out, &ok);
  if (!ok)
    r.error = "cannot read stdout";
  r.err = read_stream(a, err, &ok);
  if (!ok)
    r.error = "cannot read stderr";
  r.ok = !r.error;
done:
  if (fclose(out) != 0) {
    r.ok = 0;
    r.error = "cannot close stdout capture";
  }
  if (fclose(err) != 0) {
    r.ok = 0;
    r.error = "cannot close stderr capture";
  }
  return r;
}
