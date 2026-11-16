#include "sema_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("numeric literals use context but typed values never convert "
       "implicitly") {
    valid(&a, "main() { let x: lng = 2\nlet y: lng = 1 + x\nf(3) }\nf(x: "
              "shrt) -> shrt { x }");
    invalid(&a, "main() { let x: lng = 1\nlet y: int = x }", "annotation");
    invalid(&a, "main() { let x: lng = 1\nlet y: int = 2\nx + y }",
            "same type");
    invalid(&a, "main() { f(true) }\nf(x: int) {}",
            "argument has the wrong type");
    invalid(&a, "main() { let x: flt = 0.5\nlet y: dbl = x }", "annotation");
    invalid(&a, "main() { let x: int = 1.0 }", "annotation");
    invalid(&a, "main() { 2147483648 }", "outside the range");
    invalid(&a, "main() { let x: shrt = -32769 }", "outside the range");
    invalid(&a, "main() { let x: ushrt = 65536 }", "outside the range");
    invalid(&a, "main() { let x: uint = -1 }", "outside the range");
    invalid(&a, "main() { let x: lng = -9223372036854775809 }",
            "outside the range");
    invalid(&a, "main() { let x: ulng = 18446744073709551616 }",
            "outside the range");
    invalid(&a,
            "main() { let x: flt = "
            "999999999999999999999999999999999999999999999.0 }",
            "outside the range");
    // An unknown context type is the only error: the literal is not checked
    // against a default type it was never meant to take.
    char digits[401], huge_real[512];
    memset(digits, '9', 400);
    digits[400] = '\0';
    snprintf(huge_real, sizeof(huge_real), "main() { let x: Bogus = %s.0 }",
             digits);
    const char *unknown_contexts[] = {
        "mk() -> :ok[lng] | :!e[Bogus] { 5000000000 }\nmain() {}",
        "main() { let x: Bogus = 5000000000 }",
        "main() { let x: Bogus = -5000000000 }",
        "f(x: Bogus) {}\nmain() { f(5000000000) }",
        "P { v: Bogus }\nmain() { let p = P { v = 99999999999 } }",
        huge_real};
    for (size_t i = 0; i < sizeof(unknown_contexts) / sizeof(*unknown_contexts);
         i++) {
      Result r = check(&a, unknown_contexts[i]);
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_EQ_INT(0, strcmp("This type is unknown or not implemented yet.",
                              r.errors.items[0]->message));
      ASSERT_TRUE(!r.program->checked);
    }
    Result independent =
        check(&a, "main() { let x: Bogus = 1\nlet y: shrt = 40000 }");
    ASSERT_EQ_INT(2, independent.errors.count);
    ASSERT_EQ_INT(1, message_count(&independent.errors, "outside the range"));
    invalid(&a, "main() { 1.0 % 2.0 }", "requires integers");
    invalid(&a, "main() { true + false }", "requires numbers");
    invalid(&a, "main() { 1 and 2 }", "require bool");
    invalid(&a, "main() { not 1 }", "requires bool");
    valid(&a, "main() { not false }");
    invalid(&a, "main() { let not = true }", "Expected a binding name.");
    invalid(&a, "main() { ~1.0 }", "requires integers");
    valid(&a, "main() { \"a\" == \"b\" }");
  }
  TEST("floating literals round once at their destination precision") {
    real_literal(
        &a, "flt",
        "1.000000059604644776257986737988403547205962240695953369140625",
        0x1.000002p0);
    real_literal(
        &a, "flt",
        "-1.000000059604644776257986737988403547205962240695953369140625",
        -0x1.000002p0);
    real_literal(&a, "flt", "1.000000059604644775390625", 1.0);
    real_literal(&a, "flt", "1.000000178813934326171874", 0x1.000002p0);
    real_literal(&a, "flt", "36028799166447617", 0x1.000002p55);
    real_literal(
        &a, "dbl",
        "1.000000059604644776257986737988403547205962240695953369140625",
        0x1.000001p0);
    real_literal(&a, "flt", "340282356779733661637539395458142568447.0",
                 (double)FLT_MAX);
    real_literal(&a, "flt", "-340282356779733661637539395458142568447.0",
                 -(double)FLT_MAX);
    real_literal(&a, "flt", "0.0000000000000000000000000000000000000000000014",
                 0x1p-149);
    char tiny[128];
    snprintf(tiny, sizeof(tiny), "0.%0100d", 1);
    real_literal(&a, "flt", tiny, 0.0);
    char negative[132];
    snprintf(negative, sizeof(negative), "-%s", tiny);
    real_literal(&a, "flt", negative, -0.0);
    real_literal(&a, "flt", "1.0", 1.0); // No stale ERANGE after underflow.
    invalid(&a,
            "main() { let x: flt = 340282356779733661637539395458142568448.0 }",
            "outside the range");
    invalid(
        &a,
        "main() { let x: flt = -340282356779733661637539395458142568449.0 }",
        "outside the range");
    char huge[512];
    snprintf(huge, sizeof(huge), "main() { let x: dbl = 1%0400d.0 }", 0);
    invalid(&a, huge, "outside the range");
  }
  TEST("conditions, branch values, loops, and control-flow placement are "
       "checked") {
    invalid(&a, "main() { if 1 {} }", "condition must be bool");
    invalid(&a, "main() { while 1 {} }", "condition must be bool");
    invalid(&a, "main() { let x = if true { 1 } }", "without else");
    invalid(&a, "main() { let x = if true { 1 } else { false } }",
            "compatible types");
    invalid(&a, "main() { if true { 1 } else { false } }", "compatible types");
    valid(&a, "main() -> () { if true { 1 } else { false } }");
    invalid(&a, "main() { let x = while false {} }", "Loops cannot");
    invalid(&a, "main() { let x = for i in 0..1 {} }", "Loops cannot");
    invalid(&a, "main() { for i in 0.0..1.0 {} }", "bounds must be integers");
    invalid(&a, "main() { for i in 0..1 { i++ } }", "immutable binding");
    invalid(&a, "main() { break }", "only allowed inside loops");
    invalid(&a, "main() { continue }", "only allowed inside loops");
    invalid(&a, "main() { for i in 0..1 {}\ni }", "Unknown binding");
    invalid(&a, "main() { 0..1 }", "only supported in for");
  }
  TEST(
      "return constraints include explicit returns and reachable fallthrough") {
    invalid(&a, "main() {}\nf(x: bool) -> int { if x { ret 1 } }",
            "written return type");
    invalid(&a, "main() {}\nf(x: bool) { if x { ret true }\n1 }",
            "Incompatible return types");
    valid(&a, "main() {}\nf() { ret 1\ntrue }");
    valid(&a, "main() {}\nf(x: bool) -> int { if x { ret 1 } else { ret 2 } }");
    valid(&a, "main() {}\nf() -> int { while true {} }");
    valid(&a, "main() {}\nf(x: bool) -> () { if x { ret }\n\"discard\" }");
    valid(&a, "main() { let zero: uint = -0 }");
    Result dead = check(&a, "main() {}\nf() { for i in (if true { ret 1 } else "
                            "{ ret 2 })..0 { let x: lng = 1\nret x } }");
    ASSERT_EQ_INT(0, dead.errors.count);
    ASSERT_EQ_INT(SEMA_INT,
                  dead.program->functions[1].symbol.return_type->kind);
    invalid(&a, "main() {}\nf() -> int { ret true }", "written return type");
    invalid(&a, "main() {}\nf() -> () { ret 1 }", "written return type");
    invalid(&a, "main() {}\nf() { f() }", "Cannot infer recursive");
    valid(&a, "main() {}\nf() -> int { f() }");
    valid(&a, "main() {}\nf(x: bool) { if x { g(x) } else { 1.0 } }\ng(x: "
              "bool) { if x { f(x) } else { 1 } }");
    invalid(&a,
            "main() {}\nf(x: bool) { if x { g(x) } else { true } }\ng(x: bool) "
            "{ if x { f(x) } else { 1 } }",
            "consistent return type");
  }
  TEST("recursive probe errors preserve contextual literal anchors") {
    struct {
      const char *type, *bad, *good;
    } cases[] = {
        {"shrt", "40000", "32000"},
        {"ushrt", "-1", "65535"},
        {"uint", "-1", "4294967295"},
        {"ulng", "-1", "18446744073709551615"},
        {"flt", "350000000000000000000000000000000000000.0", "1.0000001"}};
    const char *message = "Number literal is outside the range of its type.";
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char f[512], g[512], text[2048], crlf[4096];
      snprintf(f, sizeof(f), "f(p: %s, b: bool) { if b { ret p }\ng(p, b) }\n",
               cases[i].type);
      for (unsigned order = 0; order < 2; order++) {
        snprintf(g, sizeof(g),
                 "g(p: %s, b: bool) { if b { ret f(p, b) }\n%s }\n",
                 cases[i].type, cases[i].bad);
        snprintf(text, sizeof(text), "%s%smain() {}", order ? g : f,
                 order ? f : g);
        size_t n = 0;
        for (size_t j = 0; text[j]; j++) {
          if (text[j] == '\n')
            crlf[n++] = '\r';
          crlf[n++] = text[j];
        }
        crlf[n] = '\0';
        for (unsigned ending = 0; ending < 2; ending++) {
          const char *source = ending ? crlf : text;
          Result r = check(&a, source);
          ASSERT_TRUE(r.program->resolved);
          for (unsigned repeat = 0; repeat < 2; repeat++) {
            if (repeat) {
              diagnostics_init(&r.errors, &a);
              sema_check(r.program, &r.errors);
            }
            if (r.errors.count != 1)
              diagnostics_print(&r.errors, stderr);
            ASSERT_EQ_INT(1, r.errors.count);
            if (r.errors.count == 1) {
              const char *at = strstr(source, cases[i].bad);
              ASSERT_TRUE(at != NULL);
              ASSERT_EQ_STRN(message, r.errors.items[0]->message,
                             strlen(r.errors.items[0]->message));
              if (at) {
                ASSERT_EQ_INT((size_t)(at - source),
                              r.errors.items[0]->span.start);
                ASSERT_EQ_INT((size_t)(at - source) + strlen(cases[i].bad),
                              r.errors.items[0]->span.end);
              }
            }
            SemaFunction *anchor = &r.program->functions[order ? 1 : 0];
            SemaFunction *peer = &r.program->functions[order ? 0 : 1];
            ASSERT_EQ_INT(anchor->component, peer->component);
            ASSERT_TRUE(anchor->symbol.return_type ==
                        anchor->symbol.params[0]->type);
            ASSERT_TRUE(peer->symbol.return_type == anchor->symbol.return_type);
            ASSERT_TRUE(!anchor->inference_conflict &&
                        !peer->inference_conflict);
            ASSERT_TRUE(!r.program->checked);
            ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
          }
        }
        snprintf(g, sizeof(g),
                 "g(p: %s, b: bool) { if b { ret f(p, b) }\n%s }\n",
                 cases[i].type, cases[i].good);
        snprintf(text, sizeof(text), "%s%smain() {}", order ? g : f,
                 order ? f : g);
        Result good = check(&a, text);
        if (good.errors.count)
          diagnostics_print(&good.errors, stderr);
        ASSERT_EQ_INT(0, good.errors.count);
        ASSERT_TRUE(good.program->checked);
        ASSERT_TRUE(ir_lower(&a, good.program) != NULL);
      }
    }
  }
  TEST("recursive handlers, conversions, operators and coverage fail closed") {
    struct {
      const char *type, *bad, *good, *message, *site;
      size_t width; // Nonzero spans only the pattern within a unique arm site.
    } cases[] = {
        {"shrt", "f(p,b) |> { 40000 => p\n_ => p }",
         "f(p,b) |> { 32000 => p\n_ => p }",
         "Number literal is outside the range of its type.", "40000", 0},
        {"bool", "f(p,b) |> { 1 => p\n_ => p }",
         "f(p,b) |> { true => p\n_ => p }", "Pattern value has the wrong type.",
         "1", 0},
        {"str", "f(p,b) |> { 0..1 => p\n_ => p }",
         "f(p,b) |> { \"x\" => p\n_ => p }",
         "Range and comparison patterns require numbers or chr.", "0..1", 0},
        {":yes[shrt] | :no", "f(p,b) |> { :yes => p\n:no => p }",
         "f(p,b) |> { :yes[_] => p\n:no => p }",
         "Symbol pattern has the wrong payload shape.", ":yes", 0},
        {":yes[shrt] | :no", "f(p,b) |> { :missing => p\n_ => p }",
         "f(p,b) |> { :yes[_] => p\n_ => p }",
         "Symbol pattern tag is not in the input set.", ":missing", 0},
        {":yes[shrt] | :no", "f(p,b) |> { :yes[_] => p }",
         "f(p,b) |> { :yes[_] => p\n:no => p }", "Match is not exhaustive.",
         "f(p,b) |> { :yes[_] => p }", 0},
        {":yes[shrt] | :no", "f(p,b) |> { Wrong => p\n_ => p }",
         "f(p,b) |> { :yes[_] => p\n_ => p }",
         "Symbol tag has conflicting payload types.", "Wrong", 0},
        {":yes[shrt] | :no",
         "f(p,b) |> { :yes[_] => p\n:yes[_] => p\n:no => p }",
         "f(p,b) |> { :yes[_] => p\n:no => p }", "Unreachable match arm.",
         ":yes[_] => p\n:no", 7},
        {"bool", "f(p,b) |> { true => p }",
         "f(p,b) |> { true => p\nfalse => p }", "Match is not exhaustive.",
         "f(p,b) |> { true => p }", 0},
        {"bool", "f(p,b) |> { true => p\ntrue => p\nfalse => p }",
         "f(p,b) |> { true => p\nfalse => p }", "Unreachable match arm.",
         "true => p\nfalse", 4},
        {"str", "f(p,b) |> { \"x\" => p\n\"x\" => p\n_ => p }",
         "f(p,b) |> { \"x\" => p\n_ => p }", "Unreachable match arm.",
         "\"x\" => p\n_", 3},
        {":yes[shrt] | :no", "f(p,b)!", "f(p,b) |> { _ => p }",
         "? and ! cannot handle this value: it has no error tags.", "f(p,b)!",
         0},
        {":yes[shrt] | :no", "f(p,b)?", "f(p,b) |> { _ => p }",
         "? and ! cannot handle this value: it has no error tags.", "f(p,b)?",
         0},
        {"bool", "~f(p,b)", "not f(p,b)",
         "This unary operator requires numbers (bitwise not requires "
         "integers).",
         "~f(p,b)", 0},
        {"bool", "f(p,b) + p", "f(p,b) and p", "Arithmetic requires numbers.",
         "f(p,b) + p", 0},
        {"int", "int(f(p,b), 1)", "int(f(p,b))",
         "Numeric conversion expects exactly one number, chr or str argument.",
         "int(f(p,b), 1)", 0},
        {"P", "let unused = int(f(p,b))\nf(p,b)", "let unused = int(1)\nf(p,b)",
         "Numeric conversion expects a number, chr or str argument.", "f(p,b)",
         0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned order = 0; order < 2; order++) {
        for (unsigned good = 0; good < 2; good++) {
          char f[512], g[512], text[2048], crlf[4096];
          const char *body = good ? cases[i].good : cases[i].bad;
          snprintf(f, sizeof(f),
                   "f(p: %s,b: bool) { if b { ret p }\ng(p,b) }\n",
                   cases[i].type);
          snprintf(g, sizeof(g),
                   "g(p: %s,b: bool) { if b { ret f(p,b) }\n%s }\n",
                   cases[i].type, body);
          snprintf(text, sizeof(text),
                   "P { unit: () }\nWrong = :yes[str]\n%s%smain() {}",
                   order ? g : f, order ? f : g);
          const char *body_at = strstr(text, body);
          ASSERT_TRUE(body_at != NULL);
          const char *at =
              !good && body_at ? strstr(body_at, cases[i].site) : NULL;
          ASSERT_TRUE(good || at != NULL);
          size_t start = at ? (size_t)(at - text) : 0;
          size_t width =
              cases[i].width ? cases[i].width : strlen(cases[i].site);
          // These diagnostic spans contain no newline; only their starting
          // offsets shift when the surrounding source is converted to CRLF.
          size_t n = 0, extra = 0;
          for (size_t j = 0; text[j]; j++) {
            if (text[j] == '\n') {
              crlf[n++] = '\r';
              if (j < start)
                extra++;
            }
            crlf[n++] = text[j];
          }
          crlf[n] = '\0';
          for (unsigned ending = 0; ending < 2; ending++) {
            Result r = check(&a, ending ? crlf : text);
            ASSERT_TRUE(r.program->resolved);
            for (unsigned repeat = 0; repeat < 2; repeat++) {
              if (repeat) {
                diagnostics_init(&r.errors, &a);
                sema_check(r.program, &r.errors);
              }
              if (r.errors.count != (good ? 0u : 1u)) {
                fprintf(stderr, "recursive recovery case %zu: %s\n", i, text);
                diagnostics_print(&r.errors, stderr);
              }
              ASSERT_EQ_INT(good ? 0 : 1, r.errors.count);
              if (!good && r.errors.count == 1) {
                ASSERT_EQ_INT(
                    0, strcmp(cases[i].message, r.errors.items[0]->message));
                size_t offset = start + (ending ? extra : 0);
                ASSERT_EQ_INT(offset, r.errors.items[0]->span.start);
                ASSERT_EQ_INT(offset + width, r.errors.items[0]->span.end);
              }
              SemaFunction *anchor = &r.program->functions[order ? 1 : 0];
              SemaFunction *peer = &r.program->functions[order ? 0 : 1];
              ASSERT_EQ_INT(anchor->component, peer->component);
              ASSERT_TRUE(anchor->symbol.return_type ==
                          anchor->symbol.params[0]->type);
              ASSERT_TRUE(peer->symbol.return_type ==
                          anchor->symbol.return_type);
              ASSERT_TRUE(!anchor->inference_conflict &&
                          !peer->inference_conflict);
              ASSERT_EQ_INT(good, r.program->checked);
              ASSERT_TRUE((ir_lower(&a, r.program) != NULL) == (int)good);
            }
          }
        }
      }
    }
  }
  TEST("independent errors do not hide an intrinsic inference conflict") {
    Result r =
        check(&a, "main() { let bad: int = true }\nf(x: bool) { if x { g(x) } "
                  "else { true } }\ng(x: bool) { if x { f(x) } else { 1 } }");
    ASSERT_EQ_INT(2, r.errors.count);
    ASSERT_EQ_INT(1, message_count(&r.errors, "annotation"));
    ASSERT_EQ_INT(1, message_count(&r.errors, "consistent return type"));
    ASSERT_TRUE(!r.program->checked);
    Result same_function =
        check(&a, "main() {}\nf(x: bool) { let bad: int = true\nif x { g(x) } "
                  "else { 1 } }\ng(x: bool) { if x { f(x) } else { true } }");
    ASSERT_EQ_INT(2, same_function.errors.count);
    ASSERT_EQ_INT(1, message_count(&same_function.errors, "annotation"));
    ASSERT_EQ_INT(
        1, message_count(&same_function.errors, "consistent return type"));
    ASSERT_TRUE(!same_function.program->checked);
    Diagnostics again;
    diagnostics_init(&again, &a);
    sema_check(r.program, &again);
    ASSERT_EQ_INT(2, again.count);
    ASSERT_EQ_INT(1, message_count(&again, "consistent return type"));
    ASSERT_TRUE(!r.program->checked);
    Result recursive = check(&a, "main() { main() }");
    diagnostics_init(&again, &a);
    sema_check(recursive.program, &again);
    ASSERT_EQ_INT(1, again.count);
    ASSERT_EQ_INT(1, message_count(&again, "Cannot infer recursive"));
    ASSERT_TRUE(!recursive.program->checked);
    Result propagated = check(&a, "main() { f() }\nf() { missing() }");
    ASSERT_EQ_INT(1, propagated.errors.count);
    ASSERT_EQ_INT(0,
                  message_count(&propagated.errors, "consistent return type"));
  }
  TEST("long chains and large recursive components do not use C recursion") {
    size_t capacity = 200000;
    char *text = arena_alloc(&a, capacity);
    size_t length = (size_t)snprintf(text, capacity, "main() { f0() }\n");
    for (size_t i = 0; i < 2048; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "f%zu() { f%zu() }\n", i, i + 1);
    snprintf(text + length, capacity - length, "f2048() {}\n");
    valid(&a, text);
    snprintf(text + length, capacity - length, "f2048() -> () { f0() }\n");
    Result r = check(&a, text);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->functions[1].component ==
                r.program->functions[2049].component);
    ASSERT_TRUE(r.program->functions[0].component !=
                r.program->functions[1].component);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
