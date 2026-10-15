/* Embedded verbatim in every generated translation unit. M2 strings are
 * permanent literals only; heap objects and reference counts arrive in M4. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  size_t length;
  const unsigned char *bytes;
} brt_string;

void brt_print(const brt_string *value) {
  if (fwrite(value->bytes, 1, value->length, stdout) != value->length ||
      fputc('\n', stdout) == EOF) {
    fputs("error: cannot write standard output\n", stderr);
    exit(1);
  }
}
