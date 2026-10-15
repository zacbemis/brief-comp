#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int copy(const char *from, const char *to) {
  FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
  if (!in || !out) {
    if (in)
      fclose(in);
    if (out)
      fclose(out);
    return 0;
  }
  unsigned char buffer[8192];
  size_t n;
  int ok = 1;
  while ((n = fread(buffer, 1, sizeof(buffer), in)))
    if (fwrite(buffer, 1, n, out) != n) {
      ok = 0;
      break;
    }
  if (ferror(in))
    ok = 0;
  if (fclose(in) != 0)
    ok = 0;
  if (fclose(out) != 0)
    ok = 0;
  return ok && chmod(to, 0700) == 0;
}
int main(int argc, char **argv) {
  // When copied to the requested output, this is the fake compiled program.
  if (argc == 1) {
    if (getenv("FAKE_GROUP_SIGINT")) {
      // Simulate terminal Ctrl-C to both brief and its running program.
      if (kill(0, SIGINT) != 0)
        return 98;
      return 99; // A correct driver restores the child's SIGINT disposition.
    }
    return getenv("FAKE_EXIT_STATUS") ? atoi(getenv("FAKE_EXIT_STATUS")) : 7;
  }
  if (!strcmp(argv[1], "--helper") && argc >= 3) {
    if (!strcmp(argv[2], "sleep")) {
      struct timespec t = {2, 0};
      nanosleep(&t, NULL);
      return 0;
    }
    if (!strcmp(argv[2], "signal")) {
      raise(SIGTERM);
      return 0;
    }
    if (!strcmp(argv[2], "large")) {
      unsigned char out[4096], err[4096];
      memset(out, 'o', sizeof(out));
      memset(err, 'e', sizeof(err));
      out[0] = err[0] = 0;
      for (int i = 0; i < 64; i++)
        if (fwrite(out, 1, sizeof(out), stdout) != sizeof(out) ||
            fwrite(err, 1, sizeof(err), stderr) != sizeof(err))
          return 1;
      return 0;
    }
    return 2;
  }
  const char *record = getenv("RECORD"), *mode = getenv("MODE");
  if (!record)
    return 2;
  FILE *f = fopen(record, "ab");
  if (!f)
    return 2;
  // NUL-separated arguments, followed by an extra NUL between invocations.
  for (int i = 1; i < argc; i++)
    if (fwrite(argv[i], 1, strlen(argv[i]) + 1, f) != strlen(argv[i]) + 1) {
      fclose(f);
      return 2;
    }
  if (fputc(0, f) == EOF) {
    fclose(f);
    return 2;
  }
  if (fclose(f) != 0)
    return 2;
  const char *output = NULL;
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], "-o"))
      output = argv[i + 1];
  if (!output)
    return 2;
  if (mode && !strcmp(mode, "fail")) {
    f = fopen(output, "wb");
    if (!f)
      return 2;
    fputs("incomplete executable", f);
    if (fclose(f) != 0)
      return 2;
    return 9;
  }
  if (mode && !strcmp(mode, "exit"))
    return copy(argv[0], output) ? 0 : 2;
  const char *cc = getenv("REAL_CC");
  if (!cc)
    return 2;
  argv[0] = (char *)cc;
  execvp(cc, argv);
  perror(cc);
  return 127;
}
