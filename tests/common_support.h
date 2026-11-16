#ifndef BRIEF_TESTS_COMMON_SUPPORT_H
#define BRIEF_TESTS_COMMON_SUPPORT_H

#include "common/diagnostic.h"
#include "common/intern.h"
#include "common/name_map.h"
#include "test_utils.h"

#include <stdint.h>

static inline char *printed_diagnostics(Arena *arena,
                                        const Diagnostics *diagnostics) {
  FILE *output = tmpfile();
  if (!output)
    return NULL;
  diagnostics_print(diagnostics, output);
  if (fflush(output) != 0 || fseek(output, 0, SEEK_END) != 0) {
    fclose(output);
    return NULL;
  }
  long length = ftell(output);
  if (length < 0 || fseek(output, 0, SEEK_SET) != 0) {
    fclose(output);
    return NULL;
  }
  char *text = arena_alloc(arena, (size_t)length + 1);
  size_t count = fread(text, 1, (size_t)length, output);
  text[count] = '\0';
  fclose(output);
  return text;
}

#endif
