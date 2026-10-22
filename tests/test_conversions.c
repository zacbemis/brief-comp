#include "../runtime/brief_runtime.c"
#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

static SemaProgram *check(Arena *a, const char *text, const char *error) {
  Diagnostics d;
  diagnostics_init(&d, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, "conversions.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, source, &d)), &d);
  SemaProgram *p = sema_resolve(a, ast, &d);
  sema_check(p, &d);
  if (error) {
    int found = 0;
    for (size_t i = 0; i < d.count; i++)
      found |= strstr(d.items[i]->message, error) != NULL;
    if (!found)
      diagnostics_print(&d, stderr);
    ASSERT_TRUE(found);
    ASSERT_TRUE(!p->checked);
  } else {
    if (d.count)
      diagnostics_print(&d, stderr);
    ASSERT_EQ_INT(0, d.count);
    ASSERT_TRUE(p->checked);
  }
  return p;
}
static int parse_number(const char *text, int kind, unsigned bits,
                        brt_number *out) {
  brt_string s = {{0, NULL}, strlen(text), (const unsigned char *)text};
  return brt_convert_text(&s, kind, bits, out);
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("all numeric source/target domains have the promised outcome and plain "
       "success ABI") {
    static const int infallible[8][8] = {
        {1, 0, 1, 0, 1, 0, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 0, 1, 0, 1, 1}, {0, 0, 0, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 1}};
    const char *names[] = {"shrt", "ushrt", "int", "uint",
                           "lng",  "ulng",  "flt", "dub"};
    for (size_t i = 0; i < 8; i++)
      for (size_t j = 0; j < 8; j++) {
        char source[256];
        snprintf(source, sizeof(source), "f(x: %s) { %s(x)! }\nmain() {}",
                 names[i], names[j]);
        SemaProgram *p = check(&a, source, NULL);
        const SemaType *from = &p->types[SEMA_SHRT + i],
                       *to = &p->types[SEMA_SHRT + j];
        const SemaType *outcome = sema_conversion(p, from, to);
        ASSERT_EQ_INT(infallible[i][j] ? 1 : 2, outcome->symbols->count);
        ASSERT_TRUE(sema_ok(outcome)->payload == to);
        ASSERT_TRUE(p->functions[0].symbol.return_type == to);
        IrProgram *ir = ir_lower(&a, p);
        ASSERT_TRUE(ir != NULL);
        if (!ir)
          continue;
        ir_insert_rc(&a, ir);
        size_t count = ir->functions[0].count;
        ir_insert_rc(&a, ir);
        ASSERT_EQ_INT(count, ir->functions[0].count);
        FILE *out = tmpfile();
        ASSERT_TRUE(out != NULL);
        if (out) {
          ASSERT_TRUE(emit_c(out, ir));
          fclose(out);
        }
        Diagnostics again;
        diagnostics_init(&again, &a);
        sema_check(p, &again);
        ASSERT_EQ_INT(0, again.count);
        ASSERT_TRUE(p->functions[0].symbol.return_type == to);
      }
  }
  TEST(
      "typed sources retain their domain; literal arguments adopt the target") {
    check(&a, "main() { shrt(1)\nflt(16777217)\n lng(4294967296) }", NULL);
    check(&a, "main() { let x = 1\nshrt(x) }", "must be handled");
    check(&a, "main() { let d = 0.1\nflt(d) }", "must be handled");
    check(&a, "main() { shrt(70000) }", "outside the range");
    check(&a, "main() { 70000 |> shrt() }", "outside the range");
    check(&a, "main() { uint(-1) }", "outside the range");
    check(&a, "main() { uint(-0) }", NULL);
    check(&a, "main() { int(2.5) }", "must be handled");
    check(&a, "main() { let x = 1\nlng(1 + x)\nlng(x + 1) }", NULL);
    check(&a, "main() { int(\"1\") }", "must be handled");
    check(&a, "main() { int() }", "exactly one");
    check(&a, "main() { int(1, 2) }", "exactly one");
    check(&a, "main() { int(true) }", "number or str");
    check(&a, "int() {}\nmain() {}", "Expected a declaration");
    check(&a, "main() { int('1') }", "number or str");
    check(&a, "Box {}\nmain() { int(Box {}) }", "number or str");
    check(&a, "f(): int { int(\"1\")? }\nmain() {}", "Propagated variants");
    SemaProgram *literal_types =
        check(&a,
              "f() { shrt((1)) }\ng() { (1) |> shrt() }\n"
              "h() { shrt(-32768) }\ni() { lng(4294967296) }\n"
              "j() { flt(0.1) }\nk() { 0.1 |> flt() }\nmain() {}",
              NULL);
    const SemaTypeKind expected[] = {SEMA_SHRT, SEMA_SHRT, SEMA_SHRT,
                                     SEMA_LNG,  SEMA_FLT,  SEMA_FLT};
    for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); i++)
      ASSERT_EQ_INT(expected[i],
                    literal_types->functions[i].symbol.return_type->kind);
    check(&a, "f() { int(f())? }\nmain() {}", NULL);
    check(&a,
          "f() { if true { ret :bad }\nint(g())? }\ng() { f()? }\nmain() {}",
          NULL);
    check(&a, "main() { \"12\" |> int()? |> lng() |> str() |> print() }", NULL);
  }
  TEST("conversion definitions borrow invalid strings and get standalone "
       "ownership insertion") {
    SemaProgram *p = check(&a,
                           "main() { int(\"b\" + \"ad\") |> { :ok(_) => "
                           "()\n:invalid(s) => print(s) } }",
                           NULL);
    IrProgram *ir = ir_lower(&a, p);
    ir_insert_rc(&a, ir);
    int found = 0;
    IrFunction *f = &ir->functions[0];
    for (size_t i = 1; i + 1 < f->count; i++)
      if (f->instructions[i].op == IR_CONVERT) {
        found++;
        ASSERT_EQ_INT(IR_DROP_TEMP, f->instructions[i - 1].op);
        ASSERT_EQ_INT(IR_RETAIN, f->instructions[i + 1].op);
        ASSERT_EQ_INT(f->instructions[i].result, f->instructions[i + 1].a);
      }
    ASSERT_EQ_INT(1, found);
  }
  TEST("every small integer value converts without wraparound") {
    brt_number out;
    for (int64_t value = INT16_MIN; value <= UINT16_MAX; value++)
      for (unsigned bits = 16; bits <= 64; bits *= 2) {
        int signed_ok = value >= brt_min_s(bits) && value <= brt_max_s(bits);
        int unsigned_ok = value >= 0 && (uint64_t)value <= brt_max_u(bits);
        ASSERT_EQ_INT(signed_ok, brt_convert_s(value, BRT_SIGNED, bits, &out));
        if (signed_ok)
          ASSERT_TRUE(out.s == value);
        ASSERT_EQ_INT(unsigned_ok,
                      brt_convert_s(value, BRT_UNSIGNED, bits, &out));
        if (unsigned_ok)
          ASSERT_TRUE(out.u == (uint64_t)value);
        if (value >= 0) {
          ASSERT_EQ_INT(signed_ok,
                        brt_convert_u((uint64_t)value, BRT_SIGNED, bits, &out));
          ASSERT_TRUE(brt_convert_u((uint64_t)value, BRT_UNSIGNED, bits, &out));
          ASSERT_TRUE(out.u == (uint64_t)value);
        }
      }
  }
  TEST("64-bit bounds and fractional/nonfinite floating inputs are checked "
       "before casting") {
    brt_number out;
    ASSERT_TRUE(brt_convert_f(-0x1p63, BRT_SIGNED, 64, &out));
    ASSERT_TRUE(out.s == INT64_MIN);
    ASSERT_TRUE(!brt_convert_f(0x1p63, BRT_SIGNED, 64, &out));
    ASSERT_TRUE(
        !brt_convert_f(nextafter(-0x1p63, -INFINITY), BRT_SIGNED, 64, &out));
    ASSERT_TRUE(brt_convert_f(nextafter(0x1p64, 0.0), BRT_UNSIGNED, 64, &out));
    ASSERT_TRUE(out.u == UINT64_MAX - UINT64_C(2047));
    ASSERT_TRUE(!brt_convert_f(0x1p64, BRT_UNSIGNED, 64, &out));
    ASSERT_TRUE(!brt_convert_u(UINT64_MAX, BRT_SIGNED, 64, &out));
    ASSERT_TRUE(!brt_convert_s(INT64_MIN, BRT_UNSIGNED, 64, &out));
    double invalid[] = {NAN, INFINITY, -INFINITY, 0.5, -0.5};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
      ASSERT_TRUE(!brt_convert_f(invalid[i], BRT_SIGNED, 64, &out));
      ASSERT_TRUE(!brt_convert_f(invalid[i], BRT_UNSIGNED, 64, &out));
    }
    ASSERT_TRUE(brt_convert_f(-0.0, BRT_UNSIGNED, 64, &out));
    ASSERT_EQ_INT(0, out.u);
    // Integer-to-floating conversions are direct C casts, not runtime calls.
    ASSERT_TRUE(!brt_convert_s(INT64_C(16777217), BRT_FLOAT, 32, &out));
    ASSERT_TRUE(!brt_convert_u(UINT64_C(1), BRT_DOUBLE, 64, &out));
    ASSERT_TRUE(brt_convert_f(0.1, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == (double)0.1f);
    ASSERT_TRUE(brt_convert_f(0x1.000001p0, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 1.0); // Halfway, lower significand is even.
    ASSERT_TRUE(brt_convert_f(0x1.000003p0, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 0x1.000004p0); // Halfway, upper significand is even.
    ASSERT_TRUE(brt_convert_f(-0x1p-1074, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 0 && signbit(out.f));
    ASSERT_TRUE(brt_convert_f(0x1p-149, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 0x1p-149);
    ASSERT_TRUE(brt_convert_f(nextafter((double)FLT_MAX, INFINITY), BRT_FLOAT,
                              32, &out));
    ASSERT_TRUE(out.f == FLT_MAX);
    ASSERT_TRUE(brt_convert_f(nextafter(-(double)FLT_MAX, -INFINITY), BRT_FLOAT,
                              32, &out));
    ASSERT_TRUE(out.f == -FLT_MAX);
    double overflow = (double)FLT_MAX + 0x1p103;
    ASSERT_TRUE(brt_convert_f(nextafter(overflow, 0.0), BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == FLT_MAX);
    ASSERT_TRUE(!brt_convert_f(overflow, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(!brt_convert_f(-overflow, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(!brt_convert_f(DBL_MAX, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(brt_convert_f(INFINITY, BRT_FLOAT, 32, &out) && isinf(out.f));
    ASSERT_TRUE(brt_convert_f(NAN, BRT_FLOAT, 32, &out) && isnan(out.f));
    ASSERT_TRUE(brt_convert_f(-0.0, BRT_FLOAT, 32, &out) && signbit(out.f));
  }
  TEST("random wide integers satisfy integer target ranges") {
    uint64_t random = UINT64_C(87654321);
    for (size_t i = 0; i < 10000; i++) {
      random = random * UINT64_C(6364136223846793005) +
               UINT64_C(1442695040888963407);
      int64_t signed_value;
      memcpy(&signed_value, &random, sizeof(signed_value));
      brt_number out;
      for (unsigned bits = 16; bits <= 64; bits *= 2) {
        int fits_signed = random <= (uint64_t)brt_max_s(bits);
        int fits_unsigned = random <= brt_max_u(bits);
        ASSERT_EQ_INT(fits_signed,
                      brt_convert_u(random, BRT_SIGNED, bits, &out));
        if (fits_signed)
          ASSERT_TRUE(out.s == (int64_t)random);
        ASSERT_EQ_INT(fits_unsigned,
                      brt_convert_u(random, BRT_UNSIGNED, bits, &out));
        if (fits_unsigned)
          ASSERT_TRUE(out.u == random);
        int s_to_u =
            signed_value >= 0 && (uint64_t)signed_value <= brt_max_u(bits);
        ASSERT_EQ_INT(s_to_u,
                      brt_convert_s(signed_value, BRT_UNSIGNED, bits, &out));
      }
    }
  }
  TEST("binary-safe decimal parsing checks grammar and full-width ranges") {
    brt_number out;
    ASSERT_TRUE(parse_number("-9223372036854775808", BRT_SIGNED, 64, &out));
    ASSERT_TRUE(out.s == INT64_MIN);
    ASSERT_TRUE(parse_number("18446744073709551615", BRT_UNSIGNED, 64, &out));
    ASSERT_TRUE(out.u == UINT64_MAX);
    ASSERT_TRUE(parse_number("-0", BRT_UNSIGNED, 64, &out) && out.u == 0);
    const char *invalid[] = {"",
                             "+",
                             "-",
                             " 1",
                             "1 ",
                             "1\n",
                             "1_0",
                             "0x10",
                             "1.0",
                             "1e2",
                             "9223372036854775808",
                             "-9223372036854775809"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
      ASSERT_TRUE(!parse_number(invalid[i], BRT_SIGNED, 64, &out));
    ASSERT_TRUE(!parse_number("18446744073709551616", BRT_UNSIGNED, 64, &out));
    ASSERT_TRUE(!parse_number("-1", BRT_UNSIGNED, 64, &out));
    static const unsigned char bytes[] = {'1', 0, '2'};
    brt_string binary = {{0, NULL}, sizeof(bytes), bytes};
    ASSERT_TRUE(!brt_convert_text(&binary, BRT_SIGNED, 32, &out));
    ASSERT_TRUE(!brt_convert_text(&binary, BRT_FLOAT, 32, &out));
    static const unsigned char unterminated[] = {'1', '.', '5'};
    brt_string bounded = {{0, NULL}, sizeof(unterminated), unterminated};
    ASSERT_TRUE(brt_convert_text(&bounded, BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 1.5);
  }
  TEST("decimal floating parsing rounds directly and admits finite underflow") {
    brt_number out;
    ASSERT_TRUE(
        parse_number("1.000000059604644830901776231257827021181583404541015625",
                     BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == (double)nextafterf(1.0f, INFINITY));
    ASSERT_TRUE(parse_number("-1e-9999", BRT_FLOAT, 32, &out));
    ASSERT_TRUE(out.f == 0 && signbit(out.f));
    ASSERT_TRUE(parse_number("+inf", BRT_FLOAT, 32, &out) && isinf(out.f));
    ASSERT_TRUE(parse_number("nan", BRT_DOUBLE, 64, &out) && isnan(out.f));
    ASSERT_TRUE(parse_number(".5", BRT_DOUBLE, 64, &out) && out.f == 0.5);
    ASSERT_TRUE(parse_number("1.", BRT_FLOAT, 32, &out) && out.f == 1.0);
    const char *invalid[] = {".",   "1e",     "1e+",      "0x1p2",
                             "NaN", "nan(1)", "infinity", "1e9999",
                             "--1", "1.0 ",   "1.0\n"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
      ASSERT_TRUE(!parse_number(invalid[i], BRT_FLOAT, 32, &out));
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  TEST("random floating patterns round-trip through the public string format") {
    uint64_t random = UINT64_C(123456789);
    for (size_t i = 0; i < 10000; i++) {
      random = random * UINT64_C(6364136223846793005) +
               UINT64_C(1442695040888963407);
      double original;
      memcpy(&original, &random, sizeof(original));
      brt_number out;
      const brt_string *s = brt_str_dub(original);
      ASSERT_TRUE(brt_convert_text(s, BRT_DOUBLE, 64, &out));
      ASSERT_TRUE(isnan(original) ? isnan(out.f) : out.f == original);
      if (original == 0)
        ASSERT_TRUE(!!signbit(out.f) == !!signbit(original));
      brt_release(s);
      uint32_t raw = (uint32_t)(random >> 32);
      float single;
      memcpy(&single, &raw, sizeof(single));
      s = brt_str_flt(single);
      ASSERT_TRUE(brt_convert_text(s, BRT_FLOAT, 32, &out));
      ASSERT_TRUE(isnan(single) ? isnan(out.f) : out.f == (double)single);
      if (single == 0)
        ASSERT_TRUE(!!signbit(out.f) == !!signbit(single));
      brt_release(s);
    }
    ASSERT_EQ_INT(0, brt_live_objects);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
