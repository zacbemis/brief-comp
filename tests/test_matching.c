#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"
#include <math.h>

typedef struct {
  Diagnostics errors;
  SemaProgram *program;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "matching.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  if (r.program->checked) {
    IrProgram *ir = ir_lower(a, r.program);
    ASSERT_TRUE(ir != NULL);
    ir_insert_rc(a, ir);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static void invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    if (strstr(r.errors.items[i]->message, message))
      found = 1;
  if (!found)
    diagnostics_print(&r.errors, stderr);
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("pipeline lookup prefers methods, then module functions") {
    Result r = valid(&a, "A {}\nimpl A { get(a: Self) { true } }\n"
                         "get(a: A) { 1 }\nmain() { A {} |> get() }");
    ASSERT_EQ_INT(SEMA_BOOL, r.program->entry->return_type->kind);
    r = valid(&a, "A {}\nget(a: A) { 1 }\nmain() { A {} |> get() }");
    ASSERT_EQ_INT(SEMA_INT, r.program->entry->return_type->kind);
    valid(&a, "A {}\nimpl A { get(a: Self) { true } }\n"
              "get(a: A) { 1 }\nmain() { let get = 2\nA {} |> get() }");
    valid(&a, "A {}\nimpl A { get(a: Self) { true } }\n"
              "main() { A {} |> A.get() }");
    invalid(&a, "main() { let f = 1\n2 |> f() }", "not callable");
    invalid(&a, "f(x: int) {}\nmain() { let f = 1\n2 |> f() }", "not callable");
    valid(&a, "wide(n: lng): lng { n }\nmain() { let n: lng = 4294967296 |> "
              "wide() }");
    valid(&a,
          "small(n: flt): flt { n }\nmain() { let n: flt = 0.1 |> small() }");
    valid(&a, "A {}\nimpl A { small(a: Self, n: flt): flt { n } }\n"
              "main() { let a = A {}\nlet n: flt = 0.1 |> a.small() }");
    invalid(&a, "main() { 1 |> missing() }", "Unknown function");
    invalid(&a, "f() {}\nmain() { 1 |> f() }", "Wrong number");
  }
  TEST("match arms infer values and keep independent lexical bindings") {
    valid(&a, "main() { let x: dub = true |> { true => 1\nfalse => 2.0 } }");
    Result r = valid(
        &a, "f(b: bool) { b |> { true => ret 1\nfalse => 2.0 } }\nmain() {}");
    ASSERT_EQ_INT(SEMA_DUB, r.program->functions[0].symbol.return_type->kind);
    valid(&a, "main() { let x = 9\nlet y = 2 |> { x => x + 1 }\nx + y }");
    valid(&a, "f(b: bool) { b |> { true => 1\nfalse => true |> f() } }\nmain() "
              "{ f(false) }");
    valid(&a, "main() { () |> { x => x } }");
    invalid(&a, "main() { 1 |> { x => x++ } }", "immutable binding");
    invalid(&a, "main() { 1 |> { x => x }\nx }", "Unknown binding");
    invalid(&a, "main() { let n = true |> { true => 1\nfalse => false } }",
            "compatible types");
    invalid(&a, "main() { 1 |> {} }", "not exhaustive");
    invalid(&a, "main() { true |> { true => () } }", "not exhaustive");
    invalid(&a, "main() { 1 |> { 0..10 => ()\n>= 10 => () } }",
            "not exhaustive");
    invalid(&a, "main() { 1 |> { 2..2 => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { 1 |> { < 10 => ()\n< 5 => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { true |> { _ => ()\nfalse => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { 1 |> { true => ()\n_ => () } }", "wrong type");
    invalid(&a, "main() { true |> { >= false => () } }", "require numbers");
    r = check(&a, "main() { true |> { < false => ()\n_ => () } }");
    ASSERT_EQ_INT(1,
                  r.errors.count); // No empty-range cascade after type error.
    valid(&a, "main() { \"x\" |> { \"x\" => ()\n_ => () } }");
  }
  TEST("string literals use decoded bytes and need a catch-all") {
    valid(&a, "f(s: str) { s |> { \"a\\0b\" => 1\n\"a\" => 2\nrest => 3 } "
              "}\nmain() {}");
    valid(&a,
          "f(s: str) { s |> { \"é\" => 1\n\"é\" => 2\n_ => 3 } }\nmain() {}");
    valid(&a, "f(s: str) { s |> { \"\" => \"e\" + \"mpty\"\nrest => rest } "
              "}\nmain() {}");
    valid(&a,
          "f(s: str) { s |> { \"end\" => ret 1\nrest => g(rest) } }\n"
          "g(s: str) { s |> { \"end\" => 2\nrest => f(rest) } }\nmain() {}");
    invalid(&a, "main() { \"x\" |> { \"x\" => () } }", "not exhaustive");
    invalid(&a, "main() { \"x\" |> {} }", "not exhaustive");
    invalid(&a, "main() { \"x\" |> { \"x\" => ()\n\"x\" => ()\n_ => () } }",
            "Unreachable");
    invalid(&a, "main() { \"x\" |> { _ => ()\n\"x\" => () } }", "Unreachable");
    invalid(&a, "main() { \"x\" |> { \"'\" => ()\n\"\\'\" => ()\n_ => () } }",
            "Unreachable");
    invalid(&a,
            "main() { \"x\" |> { \"a\\0b\" => ()\n\"a\\0b\" => ()\n_ => () } }",
            "Unreachable");
    invalid(&a, "main() { \"x\" |> { true => ()\n_ => () } }", "wrong type");
    invalid(&a, "main() { \"x\" |> { 1 => ()\n_ => () } }", "wrong type");
    invalid(&a, "main() { \"x\" |> { :a => ()\n_ => () } }",
            "require a symbol");
    invalid(&a, "main() { \"x\" |> { rest => { rest = \"new\" } } }",
            "immutable");
    const char *bad[] = {"main() { \"x\" |> { < \"a\" => ()\n_ => () } }",
                         "main() { \"x\" |> { \"a\"..\"z\" => ()\n_ => () } }",
                         "main() { \"x\" |> { 1 => () } }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Result r = check(&a, bad[i]);
      ASSERT_EQ_INT(
          1, r.errors.count); // No coverage cascades for invalid patterns.
    }
    Result r =
        valid(&a, "f(s: str) { s |> { \"a\\0b\" => 1\n_ => 2 } }\nmain() {}");
    Diagnostics again;
    diagnostics_init(&again, &a);
    sema_check(r.program, &again);
    ASSERT_EQ_INT(0, again.count);
    IrProgram *ir = ir_lower(&a, r.program);
    ASSERT_TRUE(ir != NULL);
    if (ir) {
      int literal = 0, equality = 0;
      for (size_t i = 0; i < ir->functions[0].count; i++) {
        IrInstruction *in = &ir->functions[0].instructions[i];
        if (in->op == IR_STRING) {
          ASSERT_EQ_INT(3, in->as.string.length);
          ASSERT_TRUE(!memcmp(in->as.string.bytes, "a\0b", 3));
          literal++;
        }
        if (in->op == IR_BINARY && in->type->kind == SEMA_STR) {
          ASSERT_EQ_INT(BINARY_EQ, in->as.binary);
          equality++;
        }
      }
      ASSERT_EQ_INT(1, literal);
      ASSERT_EQ_INT(1, equality);
      ir_insert_rc(&a, ir);
      size_t count = ir->functions[0].count;
      ir_insert_rc(&a, ir);
      ASSERT_EQ_INT(count, ir->functions[0].count);
    }
  }
  TEST("string usefulness agrees with an independent finite-atom oracle") {
    Result r = check(&a, "main() {}");
    const SemaType *type = &r.program->types[SEMA_STR];
    const char *values[] = {"",  "a",   "a\0", "a\0b", "a\0c", "\0",
                            "b", "\\n", "\n",  "é",    "é",    "\xff"};
    const size_t lengths[] = {0, 1, 2, 3, 3, 1, 1, 2, 1, 2, 3, 1};
    uint32_t random = 12345;
    for (size_t trial = 0; trial < 500; trial++) {
      SemaExpr input = {.type = type}, match = {.kind = SEMA_EXPR_MATCH};
      SemaArm arms[9] = {0};
      SemaExpr literals[9] = {0};
      unsigned seen = 0;
      int all = 0, expected_errors = 0, useful[9];
      for (size_t i = 0; i < 9; i++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned atom = (random >> 16) % 12;
        int any = (random % 11) == 0;
        arms[i].kind = any ? PAT_WILDCARD : PAT_LITERAL;
        if (!any) {
          literals[i].kind = SEMA_EXPR_STRING;
          literals[i].type = type;
          literals[i].as.text.bytes = (const unsigned char *)values[atom];
          literals[i].as.text.length = lengths[atom];
          arms[i].lower = &literals[i];
        }
        useful[i] = !all && (any || !(seen & (1u << atom)));
        expected_errors += !useful[i];
        if (any)
          all = 1;
        else
          seen |= 1u << atom;
      }
      expected_errors += !all;
      match.as.match.input = &input;
      match.as.match.arms = arms;
      match.as.match.count = 9;
      for (int repeat = 0; repeat < 2; repeat++) {
        Diagnostics errors;
        diagnostics_init(&errors, &a);
        sema_match_coverage(r.program, &match, &errors);
        ASSERT_EQ_INT(expected_errors, errors.count);
        for (size_t i = 0; i < 9; i++)
          ASSERT_EQ_INT(useful[i], arms[i].useful);
      }
    }
  }
  TEST("all integer widths and both IEEE widths normalize extrema") {
    const char *types[] = {"shrt", "ushrt", "int", "uint", "lng", "ulng"};
    const char *min[] = {
        "-32768", "0", "-2147483648", "0", "-9223372036854775808", "0"};
    const char *max[] = {"32767",
                         "65535",
                         "2147483647",
                         "4294967295",
                         "9223372036854775807",
                         "18446744073709551615"};
    for (size_t i = 0; i < 6; i++) {
      char text[1024];
      snprintf(text, sizeof(text),
               "f(n: %s) { n |> { %s..%s => 1\n%s => 2 } }\nmain() {}",
               types[i], min[i], max[i], max[i]);
      valid(&a, text);
      snprintf(text, sizeof(text),
               "f(n: %s) { n |> { < %s => 1\n_ => 2 } }\nmain() {}", types[i],
               min[i]);
      invalid(&a, text, "Unreachable match arm");
      snprintf(text, sizeof(text),
               "f(n: %s) { n |> { > %s => 1\n_ => 2 } }\nmain() {}", types[i],
               max[i]);
      invalid(&a, text, "Unreachable match arm");
    }
    valid(
        &a,
        "f(n: flt) { n |> { < 0 => 1\n0 => 2\n> 0 => 3\n_ => 4 } }\nmain() {}");
    valid(&a, "f(n: dub) { n |> { < -0.0 => 1\n0 => 2\n> 0 => 3\n_ => 4 } "
              "}\nmain() {}");
    invalid(&a, "f(n: dub) { n |> { < 0 => 1\n>= 0 => 2 } }\nmain() {}",
            "catch-all for NaN");
    invalid(&a, "f(n: flt) { n |> { -0.0 => 1\n0.0 => 2\n_ => 3 } }\nmain() {}",
            "Unreachable match arm");
  }
  TEST("coverage and usefulness agree with an exhaustive chr oracle") {
    Result r = check(&a, "main() {}");
    const SemaType *t = &r.program->types[SEMA_CHR];
    uint32_t random = 123;
    for (size_t trial = 0; trial < 500; trial++) {
      SemaExpr input = {.type = t}, match = {.kind = SEMA_EXPR_MATCH};
      SemaArm arms[9] = {0};
      SemaExpr endpoints[16] = {0};
      unsigned char seen[256] = {0};
      int expected[9] = {0};
      int expected_errors = 0;
      for (size_t i = 0; i < 9; i++) {
        int useful = 0;
        unsigned lo = 0, hi = 255;
        if (i == 8)
          arms[i].kind = PAT_WILDCARD;
        else {
          random = random * UINT32_C(1664525) + UINT32_C(1013904223);
          lo = random >> 24;
          random = random * UINT32_C(1664525) + UINT32_C(1013904223);
          hi = random >> 24;
          endpoints[i * 2] = (SemaExpr){.type = t, .integer = lo};
          endpoints[i * 2 + 1] = (SemaExpr){.type = t, .integer = hi};
          arms[i].lower = &endpoints[i * 2];
          arms[i].upper = &endpoints[i * 2 + 1];
          arms[i].kind = PAT_RANGE;
        }
        for (unsigned v = 0; v < 256; v++)
          if (i == 8 || (v >= lo && v < hi)) {
            useful |= !seen[v];
            seen[v] = 1;
          }
        expected_errors += !useful;
        expected[i] = useful;
      }
      match.as.match.input = &input;
      match.as.match.arms = arms;
      match.as.match.count = 9;
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      sema_match_coverage(r.program, &match, &errors);
      ASSERT_EQ_INT(expected_errors, errors.count);
      for (size_t i = 0; i < 9; i++)
        ASSERT_EQ_INT(expected[i], arms[i].useful);
    }
  }
  TEST("normalized IEEE intervals preserve first-match behavior") {
    Result r = check(&a, "main() {}");
    const SemaType *t = &r.program->types[SEMA_FLT];
    uint32_t random = 991;
    for (size_t trial = 0; trial < 200; trial++) {
      SemaExpr input = {.type = t}, match = {.kind = SEMA_EXPR_MATCH};
      SemaExpr endpoints[10] = {0};
      SemaArm arms[6] = {0};
      for (size_t i = 0; i < 5; i++) {
        for (size_t j = 0; j < 2; j++) {
          random = random * UINT32_C(1664525) + UINT32_C(1013904223);
          float f;
          memcpy(&f, &random, sizeof(f));
          if (!isfinite(f) || (trial + i + j) % 5 == 0)
            f = j ? 0.0f : -0.0f;
          endpoints[2 * i + j] = (SemaExpr){.type = t, .real = f};
        }
        arms[i].kind = i % 3 == 0   ? PAT_LITERAL
                       : i % 3 == 1 ? PAT_COMPARE
                                    : PAT_RANGE;
        arms[i].lower = &endpoints[2 * i];
        if (arms[i].kind == PAT_RANGE)
          arms[i].upper = &endpoints[2 * i + 1];
        arms[i].comparison = (BinaryOp)(BINARY_LT + (random >> 30));
      }
      arms[5].kind = PAT_WILDCARD;
      match.as.match.input = &input;
      match.as.match.arms = arms;
      match.as.match.count = 6;
      sema_match_coverage(r.program, &match, NULL);
      for (size_t sample = 0; sample < 100; sample++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        float f;
        memcpy(&f, &random, sizeof(f));
        double x = sample == 0   ? -0.0
                   : sample == 1 ? 0.0
                   : sample == 2 ? NAN
                   : sample == 3 ? INFINITY
                   : sample == 4 ? -INFINITY
                                 : f;
        size_t expected = 5, actual = 5;
        for (size_t i = 0; i < 5; i++) {
          SemaArm *arm = &arms[i];
          double lo = arm->lower->real;
          int hits = arm->kind == PAT_LITERAL ? x == lo
                     : arm->kind == PAT_RANGE ? x >= lo && x < arm->upper->real
                     : arm->comparison == BINARY_LT ? x < lo
                     : arm->comparison == BINARY_LE ? x <= lo
                     : arm->comparison == BINARY_GT ? x > lo
                                                    : x >= lo;
          if (hits) {
            expected = i;
            break;
          }
        }
        for (size_t i = 0; i < 5; i++)
          if (arms[i].useful && x >= arms[i].first_value->real &&
              x <= arms[i].last_value->real) {
            actual = i;
            break;
          }
        ASSERT_EQ_INT(expected, actual);
      }
    }
  }
  TEST("permissions survive match bindings, values, joins and pipelines") {
    invalid(&a, "A { n: int }\nf(a: A) { a |> { x => x.n++ } }\nmain() {}",
            "Cannot mutate through");
    invalid(
        &a,
        "A { n: int }\nf(a: A) { let x = a |> { x => x }\nx.n++ }\nmain() {}",
        "Cannot mutate through");
    invalid(&a,
            "A { n: int }\nf(a: A, b: bool) { var x = A { n: 0 }\nb |> { true "
            "=> x = a\nfalse => () }\nx.n++ }\nmain() {}",
            "Cannot mutate through");
    invalid(
        &a,
        "A {}\nQ { p: A }\nf(a: A, q: &Q) { a |> { x => q.p = x } }\nmain() {}",
        "cannot be stored in a field");
    valid(&a,
          "A { n: int }\nf(a: A, b: bool) { var x = A { n: 0 }\n"
          "b |> { true => { x = a\nret }\nfalse => () }\nx.n++ }\nmain() {}");
    invalid(&a, "A {}\nmut(a: &A) {}\nf(a: A) { a |> mut() }\nmain() {}",
            "read-only value cannot be passed");
    valid(&a, "A { n: int }\nf(a: &A) { a |> { x => x.n++ } }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A, b: bool) { var x = a\nb |> { true => x = "
              "A { n: 1 }\nfalse => x = A { n: 2 } }\nx.n++ }\nmain() {}");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
