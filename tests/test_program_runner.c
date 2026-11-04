#include "program_expect.h"
#include "test_utils.h"

static const char *validate(Arena *a, const char *text, int compile_status,
                            TestBytes compile_errors, TestBytes output,
                            TestBytes errors, int status, int exists) {
  Expectations e;
  const char *failure = parse_expectations(
      a, (TestBytes){(const unsigned char *)text, strlen(text)}, &e);
  if (failure)
    return failure;
  ProcessResult compiled = {
      .ok = 1, .status = compile_status, .err = compile_errors};
  ProcessResult ran = {.ok = 1, .status = status, .out = output, .err = errors};
  return check_result(a, &e, &compiled, &ran, exists);
}
int main(void) {
  Arena a;
  arena_init(&a);
  TestBytes empty = TEST_BYTES("");
  TEST("UTF-8 and decoded expectation bytes include zeros, tabs and "
       "backslashes") {
    ASSERT_TRUE(validate(&a, "// expect: café\\0\\n\\t\\\\\\x", 0, empty,
                         TEST_BYTES("café\0\n\t\\\\x\n"), empty, 0, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// expect: a\\0b", 0, empty, TEST_BYTES("a\0b\n"),
                         empty, 0, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// expect: a\\0b", 0, empty, TEST_BYTES("a\0c\n"),
                         empty, 0, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// expect: a\\0b", 0, empty,
                         TEST_BYTES("a\0b\n\0"), empty, 0, 1) != NULL);
  }
  TEST("output and explicit exit status are checked exactly") {
    ASSERT_TRUE(validate(&a, "// expect: ok\n// exit: 7", 0, empty,
                         TEST_BYTES("ok\n"), empty, 7, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// expect: ok", 0, empty, TEST_BYTES("wrong\n"),
                         empty, 0, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// exit: 7", 0, empty, empty, empty, 0, 1) !=
                NULL);
    ASSERT_TRUE(validate(&a, "", 0, empty, empty, TEST_BYTES("unexpected"), 0,
                         1) != NULL);
    ASSERT_TRUE(validate(&a, "", 1, TEST_BYTES("error: unexpected\n"), empty,
                         empty, 0, 0) != NULL);
  }
  TEST("standard error expectations are binary-safe, exact and "
       "status-independent") {
    ASSERT_TRUE(validate(&a, "// stderr: error: :bad(\"a\\\\0b\")\n// exit: 1",
                         0, empty, empty,
                         TEST_BYTES("error: :bad(\"a\\0b\")\n"), 1, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// stderr: a\\0b\n// exit: 1", 0, empty, empty,
                         TEST_BYTES("a\0b\n"), 1, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// stderr: a\n// exit: 1", 0, empty, empty,
                         TEST_BYTES("a\nextra\n"), 1, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// stderr: a\n// exit: 1", 0, empty, empty,
                         TEST_BYTES("a\n"), 0, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// stderr: a", 0, empty, empty, TEST_BYTES("a\n"),
                         0, 1) == NULL);
    ASSERT_TRUE(validate(&a, "// stderr: AddressSanitizer: bad\n// exit: 1", 0,
                         empty, empty, TEST_BYTES("AddressSanitizer: bad\n"), 1,
                         1) != NULL);
  }
  TEST("compile diagnostics require exact messages, locations and "
       "multiplicities") {
    const char *source = "main() { bad() } // error: Unknown function.\n";
    TestBytes diagnostic =
        TEST_BYTES("error: Unknown function.\n  --> colon:path.brf:1:10\n");
    ASSERT_TRUE(validate(&a, source, 1, diagnostic, empty, empty, 0, 0) ==
                NULL);
    ASSERT_TRUE(validate(&a, source, 1, diagnostic, empty, empty, 0, 1) !=
                NULL);
    ASSERT_TRUE(
        validate(&a, source, 1,
                 TEST_BYTES("error: Unknown function.\n  --> test.brf:2:10\n"),
                 empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(
        validate(&a, source, 1,
                 TEST_BYTES("error: Wrong message.\n  --> test.brf:1:10\n"),
                 empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, source, 1, empty, empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, source, 1,
                         TEST_BYTES("error: Unknown function.\n"), empty, empty,
                         0, 0) != NULL);
    ASSERT_TRUE(
        validate(&a, "// error: a\n// error: b", 1,
                 TEST_BYTES("error: b\n  --> f:2:1\nerror: a\n  --> f:1:1\n"),
                 empty, empty, 0, 0) == NULL);
    ASSERT_TRUE(
        validate(&a, "// error: a", 1,
                 TEST_BYTES("error: a\n  --> f:1:1\nerror: a\n  --> f:1:1\n"),
                 empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(
        validate(
            &a, source, 1,
            TEST_BYTES(
                "error: Unknown function.\n  --> test.brf:1:1\nerror: extra\n"),
            empty, empty, 0, 0) != NULL);
  }
  TEST("fix expectations match each diagnostic's help lines exactly once") {
    const char *source = "  a && b // error: bad // fix: a and b\n";
    TestBytes base = TEST_BYTES("error: bad\n  --> f.brf:1:5\n"
                                "  |\n1 |   a && b\n  |     ^~\n");
    TestBytes fixed =
        TEST_BYTES("error: bad\n  --> f.brf:1:5\n"
                   "help: write `  a and b // error: bad // fix: a and b`\n");
    ASSERT_TRUE(validate(&a, source, 1, fixed, empty, empty, 0, 0) == NULL);
    // A fix with no marker, a marker with no fix, and a different fix fail.
    ASSERT_TRUE(validate(&a, "  a && b // error: bad\n", 1, fixed, empty, empty,
                         0, 0) != NULL);
    ASSERT_TRUE(validate(&a, source, 1, base, empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, "  a && b // error: bad // fix: a or b\n", 1,
                         fixed, empty, empty, 0, 0) != NULL);
    // The code must be the whole line before the fixture's own comment.
    ASSERT_TRUE(validate(&a, "  a && b // error: bad // fix: a and\n", 1, fixed,
                         empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, "  a && b // error: bad // fix: a and b c\n", 1,
                         fixed, empty, empty, 0, 0) != NULL);
    // Several fixes match in any order, and a location prefix is allowed.
    ASSERT_TRUE(validate(&a, "x // error: bad // fix: one // fix: two\n", 1,
                         TEST_BYTES("error: bad\n  --> f.brf:1:1\n"
                                    "help: f.brf:2:1: write `two`\n"
                                    "help: write `one`\n"),
                         empty, empty, 0, 0) == NULL);
    ASSERT_TRUE(validate(&a, "x // error: bad // fix: one // fix: one\n", 1,
                         TEST_BYTES("error: bad\n  --> f.brf:1:1\n"
                                    "help: write `one`\n"),
                         empty, empty, 0, 0) != NULL);
    // Fixes belong to the error on their own line.
    ASSERT_TRUE(validate(&a, "x // error: a // fix: y\nz // error: b\n", 1,
                         TEST_BYTES("error: a\n  --> f.brf:1:1\n"
                                    "error: b\n  --> f.brf:2:1\n"
                                    "help: write `y`\n"),
                         empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, "x // error: bad // fix: y\n", 1,
                         TEST_BYTES("help: write `y`\nerror: bad\n"
                                    "  --> f.brf:1:1\n"),
                         empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, "x // error: bad // fix: y\n", 1,
                         TEST_BYTES("error: bad\n  --> f.brf:1:1\n"
                                    "help: write y\n"),
                         empty, empty, 0, 0) != NULL);
    Expectations e;
    const char *bad[] = {"x // fix: y", "x // error: bad // fix:",
                         "x // error: bad // fix:   // fix: y"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      ASSERT_TRUE(parse_expectations(&a,
                                     (TestBytes){(const unsigned char *)bad[i],
                                                 strlen(bad[i])},
                                     &e) != NULL);
    ASSERT_TRUE(parse_expectations(&a, TEST_BYTES("x // error: bad  // fix: y"),
                                   &e) == NULL);
    ASSERT_EQ_INT(1, e.error_count);
    ASSERT_EQ_INT(1, e.fix_count);
    ASSERT_EQ_STRN("bad", (const char *)e.errors->text.data,
                   e.errors->text.length);
  }
  TEST("CRLF and standalone CR preserve compiler line numbers") {
    ASSERT_TRUE(validate(&a, "// expect: ok\r\n", 0, empty, TEST_BYTES("ok\n"),
                         empty, 0, 1) == NULL);
    ASSERT_TRUE(validate(&a, "\"raw\rcharacter\"\n// error: bad", 1,
                         TEST_BYTES("error: bad\n  --> test.brf:2:1\n"), empty,
                         empty, 0, 0) == NULL);
  }
  TEST("invalid or conflicting metadata is rejected") {
    Expectations e;
    const char *bad[] = {"// error: bad\n// expect: ok",
                         "// error: bad\n// panic: bad",
                         "// error: bad\n// exit: 1",
                         "// error: bad\n// stderr: bad",
                         "// panic: bad\n// stderr: bad",
                         "// exit: 0\n// exit: 1",
                         "// exit:",
                         "// exit: nope",
                         "// exit: 1 trailing",
                         "// exit: 999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      ASSERT_TRUE(parse_expectations(&a,
                                     (TestBytes){(const unsigned char *)bad[i],
                                                 strlen(bad[i])},
                                     &e) != NULL);
  }
  TEST("panic expectations require failure, messages and any explicit status") {
    ASSERT_TRUE(validate(&a, "// panic: division by zero", 0, empty, empty,
                         TEST_BYTES("panic: division by zero\n"), 1,
                         1) == NULL);
    ASSERT_TRUE(validate(&a, "// panic: division by zero", 0, empty, empty,
                         TEST_BYTES("panic: overflow\n"), 1, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// panic: division by zero", 0, empty, empty,
                         TEST_BYTES("panic: division by zero\n"), 0,
                         1) != NULL);
    ASSERT_TRUE(validate(&a, "// panic: bad\n// exit: 3", 0, empty, empty,
                         TEST_BYTES("panic: bad"), 1, 1) != NULL);
  }
  TEST("sanitizer reports never count as expected diagnostics or panics") {
    ASSERT_TRUE(
        validate(
            &a, "// error: bad", 1,
            TEST_BYTES("error: bad\n  --> test.brf:1:1\nAddressSanitizer: bad"),
            empty, empty, 0, 0) != NULL);
    ASSERT_TRUE(validate(&a, "// panic: bad", 0, empty, empty,
                         TEST_BYTES("AddressSanitizer: bad"), 1, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// panic: bad", 0, empty, empty,
                         TEST_BYTES("runtime error: bad"), 1, 1) != NULL);
    ASSERT_TRUE(validate(&a, "// panic: bad", 0, empty, empty,
                         TEST_BYTES("UndefinedBehaviorSanitizer: bad"), 1,
                         1) != NULL);
  }
  TEST("process failures and timeouts cannot satisfy expectations") {
    Expectations e;
    ASSERT_TRUE(parse_expectations(&a, empty, &e) == NULL);
    ProcessResult compiled = {.ok = 1}, ran = {.ok = 1};
    ASSERT_TRUE(check_result(&a, &e, &compiled, NULL, 0) != NULL);
    ran.timed_out = 1;
    ASSERT_TRUE(check_result(&a, &e, &compiled, &ran, 1) != NULL);
    compiled.ok = 0;
    ASSERT_TRUE(check_result(&a, &e, &compiled, &ran, 0) != NULL);
    compiled.ok = 1;
    compiled.out = TEST_BYTES("unwanted output");
    ASSERT_TRUE(check_result(&a, &e, &compiled, &ran, 1) != NULL);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
