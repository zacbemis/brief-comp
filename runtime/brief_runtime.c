/* Embedded verbatim. Strings are still permanent literals; heap ownership
 * arrives in M4. Arithmetic checks are present in every build mode. */
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                   FLT_MAX_EXP == 128,
               "Brief requires binary32 float");
_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Brief requires binary64 double");
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
// The first-version runtime is single-threaded. Each checked IR operation
// records its source position immediately before calling a checked helper;
// nested helpers (power/shift) preserve that original operation's position.
static const char *brt_source_file;
static size_t brt_source_line;
void brt_locate(const char *file, size_t line) {
  brt_source_file = file;
  brt_source_line = line;
}
_Noreturn void brt_panic(const char *message) {
  fprintf(stderr, "panic: %s\n", message);
  if (brt_source_file)
    fprintf(stderr, "  --> %s:%zu\n", brt_source_file, brt_source_line);
  exit(1);
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
