#include "../runtime/brief_runtime.c"
#include "process.h"
#include "test_utils.h"

typedef struct {
  brt_object header;
  const brt_string *child;
} Box;
static void destroy_box(brt_object *p) { brt_release(((Box *)p)->child); }
static void text(const brt_string *s, const char *expected) {
  ASSERT_EQ_INT(strlen(expected), s->length);
  ASSERT_TRUE(memcmp(s->bytes, expected, s->length) == 0);
  ASSERT_EQ_INT(0, s->bytes[s->length]);
  brt_release(s);
}
int main(int argc, char **argv) {
  if (argc == 2) {
    brt_locate("runtime.brf", 7);
    if (!strcmp(argv[1], "size"))
      (void)brt_string_alloc(SIZE_MAX);
    else if (!strcmp(argv[1], "concat")) {
      const brt_string a = {{0, NULL}, SIZE_MAX - 1, NULL},
                       b = {{0, NULL}, 2, NULL};
      (void)brt_concat(&a, &b);
    } else if (!strcmp(argv[1], "retain")) {
      brt_object o = {SIZE_MAX, NULL};
      brt_retain(&o);
    } else if (!strcmp(argv[1], "live")) {
      const brt_string *s = brt_string_new("owned", 5);
      brt_finish();
      brt_release(s);
      return 0;
    }
    return 99;
  }
  TEST("immortal strings never write through const storage") {
    static const unsigned char bytes[] = "a\0b";
    static const brt_string literal = {{0, NULL}, 3, bytes};
    for (int i = 0; i < 100; i++) {
      brt_retain(&literal);
      brt_release(&literal);
    }
    ASSERT_EQ_INT(0, literal.object.refs);
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("heap strings have exact lengths and NUL termination") {
    const brt_string *a = brt_string_new("a\0b", 3),
                     *b = brt_string_new("\0c", 2);
    const brt_string *s = brt_concat(a, b);
    ASSERT_EQ_INT(5, s->length);
    ASSERT_TRUE(memcmp(s->bytes, "a\0b\0c", 5) == 0);
    ASSERT_EQ_INT(0, s->bytes[5]);
    ASSERT_EQ_INT(3, brt_live_objects);
    brt_release(a);
    brt_release(b);
    brt_release(s);
    text(brt_string_new(NULL, 0), "");
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST(
      "stores own children, reads can pin, and replacement preserves aliases") {
    Box *box = brt_alloc(sizeof(*box), destroy_box);
    const brt_string *old = brt_string_new("old", 3);
    brt_retain(old);
    box->child = old;
    brt_release(old); // Only the field owns it now.
    const brt_string *snapshot = box->child;
    brt_retain(snapshot);
    const brt_string *next = brt_string_new("new", 3);
    brt_retain(next);
    brt_release(box->child);
    box->child = next;
    brt_release(next);
    ASSERT_EQ_INT(3, brt_live_objects);
    text(snapshot, "old");
    brt_release(box);
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("string comparison is unsigned-byte lexicographic and binary-safe") {
    const brt_string *a = brt_string_new("a\0b", 3),
                     *b = brt_string_new("a\0c", 3);
    const brt_string *prefix = brt_string_new("a\0", 2);
    const brt_string *high = brt_string_new("\377", 1);
    ASSERT_TRUE(brt_compare(a, b) < 0);
    ASSERT_TRUE(brt_compare(b, a) > 0);
    ASSERT_TRUE(brt_compare(a, prefix) > 0);
    ASSERT_EQ_INT(0, brt_compare(a, a));
    ASSERT_TRUE(brt_compare(high, b) > 0);
    brt_release(a);
    brt_release(b);
    brt_release(prefix);
    brt_release(high);
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("integer, bool and IEEE special formatting") {
    text(brt_str_s(INT64_MIN), "-9223372036854775808");
    text(brt_str_u(UINT64_MAX), "18446744073709551615");
    text(brt_str_bool(1), "true");
    text(brt_str_bool(0), "false");
    text(brt_str_flt(0.1f), "0.1");
    text(brt_str_dub(0.1), "0.1");
    text(brt_str_dub(10), "10");
    text(brt_str_dub(1000), "1000");
    text(brt_str_dub(1.2345678901234567e21), "1234567890123456774144");
    text(brt_str_dub(-0.0), "-0");
    text(brt_str_flt(-0.0f), "-0");
    text(brt_str_dub(INFINITY), "inf");
    text(brt_str_flt(-INFINITY), "-inf");
    text(brt_str_dub(NAN), "nan");
    text(brt_str_flt(-NAN), "nan");
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("finite float formatting round-trips including extremes and random bit "
       "patterns") {
    const double edges[] = {DBL_MIN, DBL_MAX, 0x1p-1074, -DBL_MAX,
                            FLT_MIN, FLT_MAX, 0x1p-149};
    for (size_t i = 0; i < sizeof(edges) / sizeof(*edges); i++) {
      const brt_string *s = brt_str_dub(edges[i]);
      ASSERT_TRUE(strtod((const char *)s->bytes, NULL) == edges[i]);
      brt_release(s);
    }
    uint64_t state = 12345;
    for (int i = 0; i < 10000; i++) {
      state = state * UINT64_C(6364136223846793005) + 1;
      double d;
      memcpy(&d, &state, sizeof(d));
      if (isfinite(d)) {
        const brt_string *s = brt_str_dub(d);
        double parsed = strtod((const char *)s->bytes, NULL);
        ASSERT_TRUE(parsed == d && !!signbit(parsed) == !!signbit(d));
        brt_release(s);
      }
      uint32_t bits = (uint32_t)(state >> 32);
      float f;
      memcpy(&f, &bits, sizeof(f));
      if (isfinite(f)) {
        const brt_string *s = brt_str_flt(f);
        float parsed = strtof((const char *)s->bytes, NULL);
        ASSERT_TRUE(parsed == f && !!signbit(parsed) == !!signbit(f));
        brt_release(s);
      }
    }
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("size/count overflow checks precede unsafe allocation or reads") {
    Arena a;
    arena_init(&a);
    const char *cases[] = {"size", "concat", "retain", "live"};
    for (size_t i = 0; i < 4; i++) {
      const char *args[] = {argv[0], cases[i], NULL};
      ProcessOptions options = {.timeout_ms = 3000};
      ProcessResult r = process_run(&a, args, &options);
      ASSERT_TRUE(r.ok && !r.timed_out);
      ASSERT_EQ_INT(i == 3 ? 0 : 1, r.status);
      ASSERT_EQ_INT(0, r.out.length);
      const char *expected =
          i == 3 ? "debug: 1 objects still alive at exit (cycles are not "
                   "collected)\n"
          : i == 2 ? "panic: reference count overflow\n  --> runtime.brf:7\n"
                   : "panic: string too large\n  --> runtime.brf:7\n";
      ASSERT_EQ_INT(strlen(expected), r.err.length);
      ASSERT_TRUE(memcmp(expected, r.err.data, r.err.length) == 0);
    }
    arena_destroy(&a);
  }
  RUN_TESTS();
}
