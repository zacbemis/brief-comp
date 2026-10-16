#include "process.h"
#include "test_utils.h"
// Exercise exactly the source embedded in generated C, under the compiler's
// sanitizers too. No second implementation of checked operations in tests.
#include "../runtime/brief_runtime.c"

static void panic_case(int which, unsigned bits) {
  int64_t min = brt_min_s(bits), max = brt_max_s(bits);
  uint64_t umax = brt_max_u(bits);
  switch (which) {
  case 0:
    (void)brt_add_s(max, 1, bits);
    break;
  case 1:
    (void)brt_add_s(min, -1, bits);
    break;
  case 2:
    (void)brt_sub_s(min, 1, bits);
    break;
  case 3:
    (void)brt_sub_s(max, -1, bits);
    break;
  case 4:
    (void)brt_mul_s(min, -1, bits);
    break;
  case 5:
    (void)brt_mul_s(max, 2, bits);
    break;
  case 6:
    (void)brt_div_s(min, -1, bits);
    break;
  case 7:
    (void)brt_neg_s(min, bits);
    break;
  case 8:
    (void)brt_pow_s(max, 2, bits);
    break;
  case 9:
    (void)brt_lshift_s(1, (int64_t)bits - 1, bits);
    break;
  case 10:
    (void)brt_add_u(umax, 1, bits);
    break;
  case 11:
    (void)brt_sub_u(0, 1, bits);
    break;
  case 12:
    (void)brt_mul_u(umax, 2, bits);
    break;
  case 13:
    (void)brt_neg_u(1, bits);
    break;
  case 14:
    (void)brt_pow_u(umax, 2, bits);
    break;
  case 15:
    (void)brt_lshift_u(2, bits - 1, bits);
    break;
  case 16:
    (void)brt_div_s(1, 0, bits);
    break;
  case 17:
    (void)brt_rem_s(min, 0, bits);
    break;
  case 18:
    (void)brt_div_u(umax, 0, bits);
    break;
  case 19:
    (void)brt_rem_u(1, 0, bits);
    break;
  case 20:
    (void)brt_lshift_s(0, -1, bits);
    break;
  case 21:
    (void)brt_lshift_s(0, bits, bits);
    break;
  case 22:
    (void)brt_rshift_s(0, -1, bits);
    break;
  case 23:
    (void)brt_rshift_s(0, bits, bits);
    break;
  case 24:
    (void)brt_lshift_u(0, bits, bits);
    break;
  case 25:
    (void)brt_rshift_u(0, bits, bits);
    break;
  case 26:
    (void)brt_pow_s(2, -1, bits);
    break;
  }
}
int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "--case")) {
    panic_case(atoi(argv[2]), (unsigned)atoi(argv[3]));
    return 0;
  }
  TEST("exhaustive small signed arithmetic agrees with mathematical results") {
    for (int a = -128; a <= 127; a++)
      for (int b = -128; b <= 127; b++) {
        int sum = a + b, sub = a - b, mul = a * b;
        if (sum >= -128 && sum <= 127)
          ASSERT_TRUE(brt_add_s(a, b, 8) == sum);
        if (sub >= -128 && sub <= 127)
          ASSERT_TRUE(brt_sub_s(a, b, 8) == sub);
        if (mul >= -128 && mul <= 127)
          ASSERT_TRUE(brt_mul_s(a, b, 8) == mul);
        if (b && !(a == -128 && b == -1))
          ASSERT_TRUE(brt_div_s(a, b, 8) == a / b);
        if (b)
          ASSERT_TRUE(brt_rem_s(a, b, 8) == a % b);
      }
  }
  TEST(
      "exhaustive small unsigned arithmetic agrees with mathematical results") {
    for (unsigned a = 0; a <= 255; a++)
      for (unsigned b = 0; b <= 255; b++) {
        if (a + b <= 255)
          ASSERT_TRUE(brt_add_u(a, b, 8) == a + b);
        if (a >= b)
          ASSERT_TRUE(brt_sub_u(a, b, 8) == a - b);
        if (a * b <= 255)
          ASSERT_TRUE(brt_mul_u(a, b, 8) == a * b);
        if (b) {
          ASSERT_TRUE(brt_div_u(a, b, 8) == a / b);
          ASSERT_TRUE(brt_rem_u(a, b, 8) == a % b);
        }
      }
  }
  TEST("all language widths handle signed minima and unsigned maxima") {
    for (unsigned bits = 16; bits <= 64; bits *= 2) {
      int64_t min = brt_min_s(bits), max = brt_max_s(bits);
      uint64_t umax = brt_max_u(bits);
      ASSERT_TRUE(brt_add_s(min, max, bits) == -1);
      ASSERT_TRUE(brt_sub_s(min, min, bits) == 0);
      ASSERT_TRUE(brt_mul_s(min, 1, bits) == min);
      ASSERT_TRUE(brt_mul_s(min, 0, bits) == 0);
      ASSERT_TRUE(brt_mul_s(-1, max, bits) == -max);
      ASSERT_TRUE(brt_div_s(min, 1, bits) == min);
      ASSERT_TRUE(brt_rem_s(min, -1, bits) == 0);
      ASSERT_TRUE(brt_pow_s(min, 1, bits) == min);
      ASSERT_TRUE(brt_pow_u(umax, 1, bits) == umax);
      ASSERT_TRUE(brt_rshift_s(min, bits - 1, bits) == -1);
      ASSERT_TRUE(brt_lshift_s(-1, bits - 1, bits) == min);
      ASSERT_TRUE(brt_rshift_u(umax, bits - 1, bits) == 1);
      ASSERT_TRUE(brt_not_u(umax, bits) == 0);
    }
  }
  TEST("all invalid operations panic rather than invoking C undefined "
       "behavior") {
    Arena arena;
    arena_init(&arena);
    for (unsigned bits = 16; bits <= 64; bits *= 2)
      for (int which = 0; which < 27; which++) {
        char number[16], width[16];
        snprintf(number, sizeof(number), "%d", which);
        snprintf(width, sizeof(width), "%u", bits);
        const char *args[] = {argv[0], "--case", number, width, NULL};
        ProcessOptions options = {.timeout_ms = 3000};
        ProcessResult r = process_run(&arena, args, &options);
        ASSERT_TRUE(r.ok && !r.timed_out);
        ASSERT_EQ_INT(1, r.status);
        const char *message = which < 16   ? "panic: integer overflow\n"
                              : which < 20 ? "panic: division by zero\n"
                              : which < 26
                                  ? "panic: invalid shift count\n"
                                  : "panic: negative integer exponent\n";
        ASSERT_EQ_INT(strlen(message), r.err.length);
        ASSERT_TRUE(!memcmp(message, r.err.data, r.err.length));
        ASSERT_EQ_INT(0, r.out.length);
      }
    arena_destroy(&arena);
  }
  TEST("floating zero division preserves signs and produces NaN") {
    ASSERT_TRUE(isinf(brt_div_dub(1.0, 0.0)) && brt_div_dub(1.0, 0.0) > 0);
    ASSERT_TRUE(isinf(brt_div_flt(1.0f, -0.0f)) &&
                brt_div_flt(1.0f, -0.0f) < 0);
    ASSERT_TRUE(isnan(brt_div_dub(0.0, 0.0)));
    ASSERT_TRUE(isnan(brt_div_flt(NAN, 0.0f)));
  }
  RUN_TESTS();
}
