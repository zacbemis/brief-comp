#define _POSIX_C_SOURCE 200809L
#include "driver/native.h"
#include "codegen/c.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char *join(Arena *arena, const char *a, const char *b) {
  size_t size = strlen(a) + strlen(b) + 1;
  char *path = arena_alloc(arena, size);
  snprintf(path, size, "%s%s", a, b);
  return path;
}
static int process(char *const argv[], int compiler,
                   const struct sigaction *child_sigint) {
  // Fork with SIGINT blocked and the child's original disposition installed.
  // The parent then ignores it before unblocking; the child restores its mask.
  // This preserves Ctrl-C for the child even if it arrives during the fork.
  sigset_t blocked, old_mask;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) != 0) {
    perror("error: block SIGINT");
    return 1;
  }
  if (sigaction(SIGINT, child_sigint, NULL) != 0) {
    perror("error: restore child SIGINT disposition");
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    return 1;
  }
  pid_t pid = fork();
  int fork_error = errno;
  if (pid == 0) {
    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) != 0) {
      perror("error: restore child signal mask");
      _exit(127);
    }
    if (compiler && dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
      perror("error: redirect compiler output");
      _exit(127);
    }
    execvp(argv[0], argv);
    fprintf(stderr, "error: cannot execute %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }
  struct sigaction ignore = {.sa_handler = SIG_IGN};
  sigemptyset(&ignore.sa_mask);
  int ignored = sigaction(SIGINT, &ignore, NULL);
  int unblocked = sigprocmask(SIG_SETMASK, &old_mask, NULL);
  if (pid < 0) {
    errno = fork_error;
    perror("error: fork");
    return 1;
  }
  if (ignored != 0 || unblocked != 0) {
    fputs("error: cannot restore parent signal state\n", stderr);
    kill(pid, SIGKILL);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    return 1;
  }
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    perror("error: waitpid");
    return 1;
  }
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return 1;
}
int native_build(Arena *arena, const IrProgram *p, const char *output,
                 int debug, int run) {
  const char *root;
  if (run) {
    root = getenv("TMPDIR");
    if (!root || !*root)
      root = "/tmp";
  } else {
    // Do not replace the input source, including hard links and symlinks.
    struct stat source, destination;
    if (p->count && p->functions[0].span.file &&
        stat(p->functions[0].span.file->path, &source) == 0 &&
        stat(output, &destination) == 0 &&
        source.st_dev == destination.st_dev &&
        source.st_ino == destination.st_ino) {
      fputs("error: output would overwrite the source file\n", stderr);
      return 1;
    }
    const char *slash = strrchr(output, '/');
    root = slash
               ? (slash == output
                      ? "/"
                      : arena_strndup(arena, output, (size_t)(slash - output)))
               : ".";
  }
  // Ensure cc never interprets a relative temporary filename as an option.
  if (root[0] != '/')
    root = join(arena, "./", root);
  struct sigaction ignore = {.sa_handler = SIG_IGN}, original_sigint;
  sigemptyset(&ignore.sa_mask);
  if (sigaction(SIGINT, &ignore, &original_sigint) != 0) {
    perror("error: ignore SIGINT");
    return 1;
  }
  char *directory = join(arena, root, "/.brief-XXXXXX");
  if (!mkdtemp(directory)) {
    fprintf(stderr, "error: cannot create temporary directory in %s: %s\n",
            root, strerror(errno));
    sigaction(SIGINT, &original_sigint, NULL);
    return 1;
  }
  char *c_path = join(arena, directory, "/program.c");
  char *binary = join(arena, directory, "/program");
  int result = 1;
  FILE *out = fopen(c_path, "wb");
  if (!out) {
    perror("error: cannot write generated C");
    goto cleanup;
  }
  int written = emit_c(out, p);
  if (fclose(out) != 0)
    written = 0;
  if (!written) {
    fputs("error: cannot write generated C\n", stderr);
    goto cleanup;
  }
  const char *cc = getenv("CC");
  if (!cc || !*cc)
    cc = "cc";
  char *argv[16];
  size_t n = 0;
  argv[n++] = (char *)cc;
  argv[n++] = "-std=c11";
  argv[n++] = debug ? "-O0" : "-O2";
  if (debug) {
    argv[n++] = "-g";
    argv[n++] = "-DBRT_DEBUG";
    argv[n++] = "-fsanitize=address,undefined";
    argv[n++] = "-fno-sanitize-recover=all";
    argv[n++] = "-fno-omit-frame-pointer";
  }
  argv[n++] = c_path;
  argv[n++] = "-lm";
  argv[n++] = "-o";
  argv[n++] = binary;
  argv[n] = NULL;
  if (process(argv, 1, &original_sigint) != 0) {
    fputs("error: C compilation failed\n", stderr);
    goto cleanup;
  }
  if (run) {
    char *run_argv[] = {binary, NULL};
    result = process(run_argv, 0, &original_sigint);
  } else if (rename(binary, output) != 0) {
    fprintf(stderr, "error: cannot write executable %s: %s\n", output,
            strerror(errno));
  } else
    result = 0;
cleanup:
  unlink(binary);
  unlink(c_path);
  rmdir(directory);
  if (sigaction(SIGINT, &original_sigint, NULL) != 0) {
    perror("error: restore SIGINT disposition");
    result = 1;
  }
  return result;
}
