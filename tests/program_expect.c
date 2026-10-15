#include "program_expect.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int starts(TestBytes b, const char *s) {
  size_t n = strlen(s);
  return b.length >= n && memcmp(b.data, s, n) == 0;
}
static TestBytes next_line(TestBytes b, size_t *offset) {
  size_t start = *offset;
  while (*offset < b.length && b.data[*offset] != '\n')
    ++*offset;
  size_t end = *offset;
  if (*offset < b.length)
    ++*offset;
  if (end > start && b.data[end - 1] == '\r')
    --end;
  return (TestBytes){b.data + start, end - start};
}
static void append(Arena *a, ExpectedMessage **head, ExpectedMessage **tail,
                   size_t line, TestBytes text) {
  ExpectedMessage *m = arena_calloc(a, 1, sizeof(*m));
  m->line = line;
  m->text = text;
  if (*tail)
    (*tail)->next = m;
  else
    *head = m;
  *tail = m;
}
const char *parse_expectations(Arena *a, TestBytes source, Expectations *e) {
  memset(e, 0, sizeof(*e));
  unsigned char *output = arena_alloc(a, source.length + 1);
  size_t used = 0, offset = 0, number = 0;
  ExpectedMessage *error_tail = NULL, *panic_tail = NULL;
  const char *markers[] = {"// expect:", "// error:", "// panic:", "// exit:"};
  while (offset < source.length) {
    TestBytes line = next_line(source, &offset);
    number++;
    int kind = -1;
    size_t pos = 0;
    for (; pos < line.length && kind < 0; pos++)
      for (int k = 0; k < 4; k++)
        if (starts((TestBytes){line.data + pos, line.length - pos},
                   markers[k])) {
          kind = k;
          break;
        }
    if (kind < 0)
      continue;
    size_t begin = pos - 1 + strlen(markers[kind]);
    if (begin < line.length && line.data[begin] == ' ')
      begin++;
    TestBytes value = {line.data + begin, line.length - begin};
    if (kind == 0) {
      for (size_t i = 0; i < value.length; i++) {
        unsigned char c = value.data[i];
        if (c == '\\' && i + 1 < value.length) {
          unsigned char n = value.data[i + 1];
          if (n == '0' || n == 'n' || n == 't' || n == 'r' || n == '\\') {
            c = n == '0'   ? 0
                : n == 'n' ? '\n'
                : n == 't' ? '\t'
                : n == 'r' ? '\r'
                           : '\\';
            i++;
          }
        }
        output[used++] = c;
      }
      output[used++] = '\n';
    } else if (kind == 1) {
      append(a, &e->errors, &error_tail, number, value);
      e->error_count++;
    } else if (kind == 2) {
      append(a, &e->panics, &panic_tail, number, value);
      e->panic_count++;
    } else {
      if (e->has_exit)
        return "duplicate exit expectation";
      char *text = arena_strndup(a, (const char *)value.data, value.length),
           *end;
      errno = 0;
      long status = strtol(text, &end, 10);
      if (end == text || errno == ERANGE || status < INT_MIN ||
          status > INT_MAX)
        return "invalid exit expectation";
      while (*end && isspace((unsigned char)*end))
        end++;
      if (*end || (size_t)(end - text) != value.length)
        return "invalid exit expectation";
      e->has_exit = 1;
      e->exit_status = (int)status;
    }
  }
  output[used] = 0;
  e->output = (TestBytes){output, used};
  if (e->error_count && (used || e->panic_count || e->has_exit))
    return "compile-error tests cannot also expect output, panics, or exit "
           "status";
  return NULL;
}
static int decimal(TestBytes b, size_t *number) {
  if (!b.length)
    return 0;
  size_t n = 0;
  for (size_t i = 0; i < b.length; i++) {
    unsigned c = b.data[i];
    if (c < '0' || c > '9' || n > (SIZE_MAX - (c - '0')) / 10)
      return 0;
    n = n * 10 + c - '0';
  }
  *number = n;
  return n != 0;
}
static int location_line(TestBytes b, size_t *number) {
  if (!starts(b, "  --> "))
    return 0;
  size_t last = b.length, previous = b.length;
  for (size_t i = b.length; i > 0; i--) {
    if (b.data[i - 1] == ':') {
      if (last == b.length)
        last = i - 1;
      else {
        previous = i - 1;
        break;
      }
    }
  }
  if (last == b.length || previous == b.length || previous < 6)
    return 0;
  size_t column;
  return decimal((TestBytes){b.data + previous + 1, last - previous - 1},
                 number) &&
         decimal((TestBytes){b.data + last + 1, b.length - last - 1}, &column);
}
const char *check_result(Arena *a, const Expectations *e,
                         const ProcessResult *compiled,
                         const ProcessResult *ran, int executable_exists) {
  if (!compiled->ok || compiled->timed_out)
    return compiled->error ? compiled->error : "compilation process failed";
  if (sanitizer_report(compiled->err))
    return "sanitizer report during compilation";
  if (compiled->out.length)
    return "compilation wrote standard output";
  if (e->error_count) {
    if (compiled->status != 1)
      return "expected compilation status 1";
    if (executable_exists)
      return "failed compilation published an executable";
    unsigned char *matched = arena_calloc(a, e->error_count, 1);
    size_t offset = 0, count = 0;
    while (offset < compiled->err.length) {
      TestBytes line = next_line(compiled->err, &offset);
      if (!starts(line, "error:"))
        continue;
      if (!starts(line, "error: "))
        return "malformed compiler diagnostic";
      TestBytes message = {line.data + 7, line.length - 7};
      TestBytes loc = next_line(compiled->err, &offset);
      size_t number;
      if (!location_line(loc, &number))
        return "diagnostic has no source location";
      ExpectedMessage *expected = e->errors;
      size_t index = 0;
      for (; expected; expected = expected->next, index++)
        if (!matched[index] && number == expected->line &&
            bytes_equal(message, expected->text))
          break;
      if (!expected)
        return "unexpected diagnostic message or source line";
      matched[index] = 1;
      count++;
    }
    return count == e->error_count ? NULL : "missing expected diagnostic";
  }
  if (compiled->status != 0 || compiled->err.length)
    return "compilation failed or was not silent";
  if (!ran)
    return "program was not run";
  if (!ran->ok || ran->timed_out)
    return ran->error ? ran->error : "program process failed";
  if (sanitizer_report(ran->err))
    return "sanitizer report during execution";
  if (!bytes_equal(e->output, ran->out))
    return "program output differs (byte comparison)";
  if (e->panic_count) {
    if (!ran->status)
      return "expected a failing program";
    for (ExpectedMessage *m = e->panics; m; m = m->next)
      if (!bytes_contains(ran->err, m->text))
        return "missing expected panic message";
  } else if (ran->err.length)
    return "unexpected program standard error";
  if ((e->has_exit || !e->panic_count) &&
      ran->status != (e->has_exit ? e->exit_status : 0))
    return "unexpected program exit status";
  return NULL;
}
