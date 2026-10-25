/* Embedded verbatim. Single-threaded ARC; literals are immortal. Arithmetic
 * and allocation checks are present in every build mode. */
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                   FLT_MAX_EXP == 128,
               "Brief requires binary32 float");
_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Brief requires binary64 double");
typedef struct brt_object brt_object;
struct brt_object {
  size_t refs; // Zero is reserved for immortal program literals.
  void (*destroy)(brt_object *);
};
typedef struct {
  brt_object object;
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
// The first-version runtime is single-threaded. Each checked IR operation
// records its source position immediately before calling a checked helper;
// nested helpers (power/shift) preserve that original operation's position.
static const char *brt_source_file;
static size_t brt_source_line;
void brt_locate(const char *file, size_t line) {
  brt_source_file = file;
  brt_source_line = line;
}
void brt_panic_begin(void) {
  // Flush before the first stderr byte, including when both streams share a
  // file.
  (void)fflush(stdout);
  fputs("panic: ", stderr);
}
_Noreturn void brt_panic_end(void) {
  (void)fflush(stdout);
  if (brt_source_file)
    fprintf(stderr, "  --> %s:%zu\n", brt_source_file, brt_source_line);
  exit(1);
}
_Noreturn void brt_panic(const char *message) {
  brt_panic_begin();
  fprintf(stderr, "%s\n", message);
  brt_panic_end();
}
static size_t brt_live_objects;
void brt_retain(const void *pointer) {
  brt_object *o = (brt_object *)pointer;
  if (o && o->refs) {
    if (o->refs == SIZE_MAX)
      brt_panic("reference count overflow");
    o->refs++;
  }
}
// Destroy callbacks release their fields through brt_release. Objects reaching
// zero during a destroy are queued rather than destroyed recursively, so long
// acyclic chains use bounded C stack. All are still freed before the outermost
// release returns.
static brt_object **brt_pending;
static size_t brt_pending_count, brt_pending_capacity;
static int brt_releasing;
void brt_release(const void *pointer) {
  brt_object *o = (brt_object *)pointer;
  if (!o || !o->refs || --o->refs)
    return;
  if (brt_releasing) {
    if (brt_pending_count == brt_pending_capacity) {
      size_t capacity = brt_pending_capacity ? brt_pending_capacity * 2 : 64;
      if (capacity < brt_pending_capacity ||
          capacity > SIZE_MAX / sizeof(*brt_pending))
        brt_panic("out of memory");
      brt_object **pending =
          realloc(brt_pending, capacity * sizeof(*brt_pending));
      if (!pending)
        brt_panic("out of memory");
      brt_pending = pending;
      brt_pending_capacity = capacity;
    }
    brt_pending[brt_pending_count++] = o;
    return;
  }
  brt_releasing = 1;
  for (;;) {
    o->destroy(o);
    brt_live_objects--;
    free(o);
    if (!brt_pending_count)
      break;
    o = brt_pending[--brt_pending_count];
  }
  brt_releasing = 0;
}
void *brt_alloc(size_t size, void (*destroy)(brt_object *)) {
  brt_object *o = calloc(1, size);
  if (!o)
    brt_panic("out of memory");
  o->refs = 1;
  o->destroy = destroy;
  brt_live_objects++;
  return o;
}
void brt_finish(void) {
  free(brt_pending);
  brt_pending = NULL;
  brt_pending_capacity = 0;
  (void)
      brt_live_objects; // Counter also supports runtime tests without reports.
#ifdef BRT_DEBUG
  if (brt_live_objects) {
    fprintf(
        stderr,
        "debug: %zu objects still alive at exit (cycles are not collected)\n",
        brt_live_objects);
  }
#endif
}
void brt_destroy_string(brt_object *o) { (void)o; }
const brt_string *brt_string_alloc(size_t length) {
  if (length > SIZE_MAX - sizeof(brt_string) - 1)
    brt_panic("string too large");
  brt_string *s = brt_alloc(sizeof(*s) + length + 1, brt_destroy_string);
  s->length = length;
  s->bytes = (const unsigned char *)(s + 1);
  return s;
}
const brt_string *brt_string_new(const void *bytes, size_t length) {
  const brt_string *s = brt_string_alloc(length);
  if (length)
    memcpy((unsigned char *)s->bytes, bytes, length);
  return s;
}
const brt_string *brt_concat(const brt_string *a, const brt_string *b) {
  if (b->length > SIZE_MAX - a->length)
    brt_panic("string too large");
  const brt_string *s = brt_string_alloc(a->length + b->length);
  if (a->length)
    memcpy((unsigned char *)s->bytes, a->bytes, a->length);
  if (b->length)
    memcpy((unsigned char *)s->bytes + a->length, b->bytes, b->length);
  return s;
}
int brt_compare(const brt_string *a, const brt_string *b) {
  size_t common = a->length < b->length ? a->length : b->length;
  int order = common ? memcmp(a->bytes, b->bytes, common) : 0;
  return order ? order : (a->length > b->length) - (a->length < b->length);
}
const brt_string *brt_formatted(const char *buffer, int length,
                                size_t capacity) {
  if (length < 0 || (size_t)length >= capacity)
    brt_panic("cannot format number");
  return brt_string_new(buffer, (size_t)length);
}
const brt_string *brt_str_s(int64_t value) {
  char buffer[32];
  int n = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
  return brt_formatted(buffer, n, sizeof(buffer));
}
const brt_string *brt_str_u(uint64_t value) {
  char buffer[32];
  int n = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
  return brt_formatted(buffer, n, sizeof(buffer));
}
const brt_string *brt_str_bool(_Bool value) {
  return brt_string_new(value ? "true" : "false", value ? 4 : 5);
}
const brt_string *brt_str_real(double value, int single) {
  if (isnan(value))
    return brt_string_new("nan", 3);
  if (isinf(value))
    return brt_string_new(signbit(value) ? "-inf" : "inf",
                          signbit(value) ? 4 : 3);
  char buffer[64];
  int n = 0;
  // First significant-digit precision that round-trips at the source width.
  // Keep -0, and canonicalize nonfinite values above. The C locale is used.
  int precision;
  for (precision = 1; precision <= (single ? 9 : 17); precision++) {
    n = snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (n < 0 || (size_t)n >= sizeof(buffer))
      brt_panic("cannot format number");
    double parsed =
        single ? (double)strtof(buffer, NULL) : strtod(buffer, NULL);
    if (parsed == value && !!signbit(parsed) == !!signbit(value))
      break;
  }
  // Prefer ordinary decimal notation when it is no longer than exponent form.
  char *exponent = strchr(buffer, 'e');
  if (exponent) {
    long e = strtol(exponent + 1, NULL, 10);
    long places = precision - 1 - e;
    if (places < 0)
      places = 0;
    if (places <= 20 && e >= -20 && e <= 21) {
      char decimal[64];
      int d = snprintf(decimal, sizeof(decimal), "%.*f", (int)places, value);
      if (d >= 0 && d <= n) {
        memcpy(buffer, decimal, (size_t)d + 1);
        n = d;
      }
    }
  }
  return brt_formatted(buffer, n, sizeof(buffer));
}
const brt_string *brt_str_flt(float value) {
  return brt_str_real((double)value, 1);
}
const brt_string *brt_str_dub(double value) { return brt_str_real(value, 0); }

// Diagnostic formatting is binary-safe and does not add owners to user data.
void brt_write_quoted(FILE *out, const unsigned char *bytes, size_t length,
                      int quote) {
  fputc(quote, out);
  for (size_t i = 0; i < length; i++) {
    unsigned char c = bytes[i];
    if (c == '\\' || c == quote) {
      fputc('\\', out);
      fputc(c, out);
    } else if (c == '\n')
      fputs("\\n", out);
    else if (c == '\r')
      fputs("\\r", out);
    else if (c == '\t')
      fputs("\\t", out);
    else if (!c)
      fputs("\\0", out);
    else if (c < 32 || c >= 127)
      fprintf(out, "\\x%02x", (unsigned)c);
    else
      fputc(c, out);
  }
  fputc(quote, out);
}
void brt_write_number(FILE *out, const brt_string *text) {
  fwrite(text->bytes, 1, text->length, out);
  brt_release(text);
}

int64_t brt_max_s(unsigned bits) { return INT64_MAX >> (64 - bits); }
int64_t brt_min_s(unsigned bits) { return -brt_max_s(bits) - 1; }
uint64_t brt_max_u(unsigned bits) { return UINT64_MAX >> (64 - bits); }
int64_t brt_add_s(int64_t a, int64_t b, unsigned bits) {
  if ((b > 0 && a > brt_max_s(bits) - b) || (b < 0 && a < brt_min_s(bits) - b))
    brt_panic("integer overflow");
  return a + b;
}
int64_t brt_sub_s(int64_t a, int64_t b, unsigned bits) {
  if ((b < 0 && a > brt_max_s(bits) + b) || (b > 0 && a < brt_min_s(bits) + b))
    brt_panic("integer overflow");
  return a - b;
}
int64_t brt_mul_s(int64_t a, int64_t b, unsigned bits) {
  int64_t min = brt_min_s(bits), max = brt_max_s(bits);
  if ((a > 0 && ((b > 0 && a > max / b) || (b < 0 && b < min / a))) ||
      (a < 0 && ((b > 0 && a < min / b) || (b < 0 && a < max / b))))
    brt_panic("integer overflow");
  return a * b;
}
int64_t brt_div_s(int64_t a, int64_t b, unsigned bits) {
  if (!b)
    brt_panic("division by zero");
  if (a == brt_min_s(bits) && b == -1)
    brt_panic("integer overflow");
  return a / b;
}
int64_t brt_rem_s(int64_t a, int64_t b, unsigned bits) {
  if (!b)
    brt_panic("division by zero");
  if (a == brt_min_s(bits) && b == -1)
    return 0;
  return a % b;
}
int64_t brt_neg_s(int64_t a, unsigned bits) {
  if (a == brt_min_s(bits))
    brt_panic("integer overflow");
  return -a;
}
uint64_t brt_add_u(uint64_t a, uint64_t b, unsigned bits) {
  if (a > brt_max_u(bits) - b)
    brt_panic("integer overflow");
  return a + b;
}
uint64_t brt_sub_u(uint64_t a, uint64_t b, unsigned bits) {
  (void)bits;
  if (a < b)
    brt_panic("integer overflow");
  return a - b;
}
uint64_t brt_mul_u(uint64_t a, uint64_t b, unsigned bits) {
  if (b && a > brt_max_u(bits) / b)
    brt_panic("integer overflow");
  return a * b;
}
uint64_t brt_div_u(uint64_t a, uint64_t b, unsigned bits) {
  (void)bits;
  if (!b)
    brt_panic("division by zero");
  return a / b;
}
uint64_t brt_rem_u(uint64_t a, uint64_t b, unsigned bits) {
  (void)bits;
  if (!b)
    brt_panic("division by zero");
  return a % b;
}
uint64_t brt_neg_u(uint64_t a, unsigned bits) {
  (void)bits;
  if (a)
    brt_panic("integer overflow");
  return 0;
}
int64_t brt_pow_s(int64_t a, int64_t b, unsigned bits) {
  if (b < 0)
    brt_panic("negative integer exponent");
  int64_t result = 1;
  while (b) {
    if (b & 1)
      result = brt_mul_s(result, a, bits);
    b /= 2;
    if (b)
      a = brt_mul_s(a, a, bits);
  }
  return result;
}
uint64_t brt_pow_u(uint64_t a, uint64_t b, unsigned bits) {
  uint64_t result = 1;
  while (b) {
    if (b & 1)
      result = brt_mul_u(result, a, bits);
    b /= 2;
    if (b)
      a = brt_mul_u(a, a, bits);
  }
  return result;
}
int64_t brt_lshift_s(int64_t a, int64_t b, unsigned bits) {
  if (b < 0 || (uint64_t)b >= bits)
    brt_panic("invalid shift count");
  while (b--)
    a = brt_add_s(a, a, bits);
  return a;
}
uint64_t brt_lshift_u(uint64_t a, uint64_t b, unsigned bits) {
  if (b >= bits)
    brt_panic("invalid shift count");
  while (b--)
    a = brt_add_u(a, a, bits);
  return a;
}
int64_t brt_rshift_s(int64_t a, int64_t b, unsigned bits) {
  if (b < 0 || (uint64_t)b >= bits)
    brt_panic("invalid shift count");
  while (b--)
    a = a / 2 - (a < 0 && a % 2 != 0);
  return a;
}
uint64_t brt_rshift_u(uint64_t a, uint64_t b, unsigned bits) {
  if (b >= bits)
    brt_panic("invalid shift count");
  return a >> b;
}
uint64_t brt_not_u(uint64_t a, unsigned bits) { return a ^ brt_max_u(bits); }
#define BRT_FLOAT_DIV(T, name)                                                 \
  T brt_div_##name(T a, T b) {                                                 \
    if (b == (T)0) {                                                           \
      if (a == (T)0 || isnan(a))                                               \
        return (T)NAN;                                                         \
      return signbit(a) != signbit(b) ? (T) - INFINITY : (T)INFINITY;          \
    }                                                                          \
    return a / b;                                                              \
  }
BRT_FLOAT_DIV(float, flt)
BRT_FLOAT_DIV(double, dub)
#undef BRT_FLOAT_DIV

/* Integer targets require exact integral values. Floating targets round to
 * their requested width, like source literals and decimal text parsing. */
enum { BRT_SIGNED, BRT_UNSIGNED, BRT_FLOAT, BRT_DOUBLE };
typedef struct {
  int64_t s;
  uint64_t u;
  double f;
} brt_number;
// Integer sources reach the runtime only for integer targets: every
// integer-to-floating conversion succeeds and is emitted as a direct C cast.
int brt_convert_s(int64_t value, int kind, unsigned bits, brt_number *out) {
  if (kind == BRT_SIGNED) {
    if (value < brt_min_s(bits) || value > brt_max_s(bits))
      return 0;
    out->s = value;
  } else if (kind == BRT_UNSIGNED) {
    if (value < 0 || (uint64_t)value > brt_max_u(bits))
      return 0;
    out->u = (uint64_t)value;
  } else
    return 0;
  return 1;
}
int brt_convert_u(uint64_t value, int kind, unsigned bits, brt_number *out) {
  if (kind == BRT_SIGNED) {
    if (value > (uint64_t)brt_max_s(bits))
      return 0;
    out->s = (int64_t)value;
  } else if (kind == BRT_UNSIGNED) {
    if (value > brt_max_u(bits))
      return 0;
    out->u = value;
  } else
    return 0;
  return 1;
}
int brt_convert_f(double value, int kind, unsigned bits, brt_number *out) {
  if (kind == BRT_FLOAT || kind == BRT_DOUBLE) {
    if (kind == BRT_DOUBLE) {
      out->f = value;
      return 1;
    }
    // Check before narrowing: out-of-range C floating casts are not portable.
    // Values just beyond FLT_MAX still round to it, as literals/text do. Handle
    // that interval explicitly; at the half-ULP overflow tie, round to
    // infinity.
    if (isfinite(value) && (value > FLT_MAX || value < -FLT_MAX)) {
      double overflow = (double)FLT_MAX + 0x1p103;
      if (value >= overflow || value <= -overflow)
        return 0;
      out->f = value < 0 ? -(double)FLT_MAX : (double)FLT_MAX;
      return 1;
    }
    out->f = (float)value;
    return 1;
  }
  if (!isfinite(value) || trunc(value) != value)
    return 0;
  // Half-open powers-of-two bounds avoid rounding INT64_MAX/UINT64_MAX up
  // before the guard, and ensure the following C casts are always defined.
  double limit = ldexp(1.0, kind == BRT_SIGNED ? (int)bits - 1 : (int)bits);
  if (value >= limit || value < (kind == BRT_SIGNED ? -limit : 0.0))
    return 0;
  if (kind == BRT_SIGNED)
    out->s = (int64_t)value;
  else
    out->u = (uint64_t)value;
  return 1;
}
static int brt_decimal_digit(unsigned char c) { return c >= '0' && c <= '9'; }
int brt_convert_text(const brt_string *value, int kind, unsigned bits,
                     brt_number *out) {
  const unsigned char *bytes = value->bytes;
  size_t length = value->length, pos = 0;
  int negative = 0;
  if (pos < length && (bytes[pos] == '+' || bytes[pos] == '-'))
    negative = bytes[pos++] == '-';
  if (pos == length)
    return 0;
  if (kind == BRT_SIGNED || kind == BRT_UNSIGNED) {
    uint64_t limit = kind == BRT_UNSIGNED
                         ? brt_max_u(bits)
                         : (uint64_t)brt_max_s(bits) + (unsigned)negative;
    uint64_t magnitude = 0;
    for (; pos < length; pos++) {
      if (!brt_decimal_digit(bytes[pos]))
        return 0;
      unsigned digit = (unsigned)(bytes[pos] - '0');
      if (magnitude > limit / 10 ||
          (magnitude == limit / 10 && digit > limit % 10))
        return 0;
      magnitude = magnitude * 10 + digit;
    }
    if (kind == BRT_UNSIGNED) {
      if (negative && magnitude)
        return 0;
      out->u = magnitude;
    } else
      out->s = negative && magnitude ? -(int64_t)(magnitude - 1) - 1
                                     : (int64_t)magnitude;
    return 1;
  }
  int special = length - pos == 3 && (!memcmp(bytes + pos, "inf", 3) ||
                                      !memcmp(bytes + pos, "nan", 3));
  if (!special) {
    size_t digits = 0;
    while (pos < length && brt_decimal_digit(bytes[pos])) {
      pos++;
      digits++;
    }
    if (pos < length && bytes[pos] == '.') {
      pos++;
      while (pos < length && brt_decimal_digit(bytes[pos])) {
        pos++;
        digits++;
      }
    }
    if (!digits)
      return 0;
    if (pos < length && (bytes[pos] == 'e' || bytes[pos] == 'E')) {
      pos++;
      if (pos < length && (bytes[pos] == '+' || bytes[pos] == '-'))
        pos++;
      size_t start = pos;
      while (pos < length && brt_decimal_digit(bytes[pos]))
        pos++;
      if (start == pos)
        return 0;
    }
    if (pos != length)
      return 0;
  }
  if (length == SIZE_MAX)
    brt_panic("string too large");
  char *text = malloc(length + 1);
  if (!text)
    brt_panic("out of memory");
  memcpy(text, bytes, length);
  text[length] = 0;
  char *end;
  double result =
      kind == BRT_FLOAT ? (double)strtof(text, &end) : strtod(text, &end);
  int valid = end == text + length && (special || isfinite(result));
  free(text);
  if (valid)
    out->f = result;
  return valid;
}
