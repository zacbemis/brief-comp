#include "errors_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("return inference accumulates errors and narrows handled variants") {
    Result r =
        valid(&a, "Problems = :!a[str] | :!b[int]\n"
                  "source(b: bool) -> :ok[str] | Problems { if b { "
                  "ret :!a[\"a\"] }\n\"x\" }\n"
                  "f() { source(false)? }\n"
                  "g() { source(false) |> { :ok[text] => text\n:!a[_] => "
                  "\"\"\nother => ret other } }\n"
                  "main() { f()!\ng()! }");
    const SemaType *f = r.program->functions[1].symbol.outcome_type;
    const SemaType *g = r.program->functions[2].symbol.outcome_type;
    ASSERT_EQ_INT(3, f->symbols->count);
    ASSERT_EQ_INT(2, g->symbols->count);
    ASSERT_TRUE(!sema_variant(g, sema_tag(r.program, r.program->module, "a")));
    Result again;
    diagnostics_init(&again.errors, &a);
    sema_check(r.program, &again.errors);
    ASSERT_EQ_INT(0, again.errors.count);
    ASSERT_TRUE(g == r.program->functions[2].symbol.outcome_type);
    invalid(&a,
            "a() -> :ok | :!bad[int] { :ok }\nb() -> :ok | :!bad[str] { "
            ":ok }\n"
            "f() { a()?\nb()? }\nmain() {}",
            "conflicting payload");
  }
  TEST(
      "partial returns fix success types and grow only compatible error sets") {
    Result r = valid(&a, "source() -> :ok[lng] | :!bad { 4294967296 }\n"
                         "f() -> :ok[lng] | _ { source()? }\nmain() { f()! }");
    ASSERT_EQ_INT(2,
                  r.program->functions[1].symbol.outcome_type->symbols->count);
    valid(&a, "f(b: bool) -> :ok[lng] | _ { if b { ret :!bad }\n4294967296 "
              "}\nmain() {}\n");
    invalid(&a, "f() -> :ok[int] | _ { false }\nmain() {}",
            "body does not match");
    invalid(&a, "f() -> :a | _ { :a }\nmain() {}", "must declare :ok");
    invalid(&a, "f() -> :ok | _ | _ {}\nmain() {}", "Duplicate partial");
    invalid(&a, "A = :ok | _\nmain() {}", "Partial symbol sets");
  }
  TEST("written return mismatches do not become partial-set collisions") {
    struct {
      const char *text;
      size_t returns, bodies, postfixes;
    } cases[] = {
        {"f() -> :ok[int] | _ { false }", 0, 1, 0},
        {"f() -> :ok[A] | _ { B { unit = () } }", 0, 1, 0},
        {"f() -> :ok[int] | _ { ret false }", 1, 0, 0},
        {"f(b: bool) -> :ok[int] | _ { if b { ret false }\n1 }", 1, 0, 0},
        {"f() -> :ok[int] | _ { :ok[false] }", 0, 1, 0},
        {"f() -> :ok[int] | _ { ret :ok }", 1, 0, 0},
        {"f(b: bool) -> :ok[int] | _ { if b { ret false }\n\"bad\" }", 1, 1, 0},
        {"f() -> :ok[int] | _ { ret 1\nret false }", 1, 0, 0},
        {"f() -> :ok[A] | _ { B { unit = () } }\ng() { f()! }", 0, 1, 1},
        {"f() -> :ok[A] | _ { B { unit = () } }\ng() -> :ok[int] | _ { false }",
         0, 2, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[1024];
      snprintf(text, sizeof(text),
               "A { unit: () }\nB { unit: () }\n%s\nmain() {}", cases[i].text);
      Result r = check(&a, text);
      for (int repeat = 0; repeat < 2; repeat++) {
        if (repeat) {
          diagnostics_init(&r.errors, &a);
          sema_check(r.program, &r.errors);
        }
        ASSERT_EQ_INT(cases[i].returns + cases[i].bodies + cases[i].postfixes,
                      r.errors.count);
        size_t returns = 0, bodies = 0, postfixes = 0;
        for (size_t j = 0; j < r.errors.count; j++) {
          returns +=
              strcmp(r.errors.items[j]->message,
                     "Return value does not match the written return type.") ==
              0;
          bodies +=
              strcmp(r.errors.items[j]->message,
                     "Function body does not match the written return type.") ==
              0;
          postfixes += strcmp(r.errors.items[j]->message,
                              "? and ! cannot handle this value: it has no "
                              "error tags.") == 0;
        }
        ASSERT_EQ_INT(cases[i].returns, returns);
        ASSERT_EQ_INT(cases[i].bodies, bodies);
        ASSERT_EQ_INT(cases[i].postfixes, postfixes);
        ASSERT_TRUE(!r.program->functions[0].inference_conflict);
        ASSERT_TRUE(!r.program->checked);
        ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
        // Rejected returns contribute nothing: the written :ok remains the
        // function's success type, so callers are still checked against it.
        const SemaFunction *f = &r.program->functions[0];
        ASSERT_TRUE(f->symbol.outcome_type == f->written_type);
        ASSERT_TRUE(f->written_ok != NULL);
      }
    }
    Result callers = check(&a, "f(b: bool) -> :ok[int] | :!bad | _ {\n"
                               "if b { ret :!bad }\nfalse }\n"
                               "g() { f(true)\nlet s: str = f(true)! }\n"
                               "main() {}");
    const char *caller_messages[] = {
        "Function body does not match the written return type.",
        "A call that can fail must be handled with ?, !, or a match.",
        "Binding value does not match its annotation."};
    ASSERT_EQ_INT(3, callers.errors.count);
    for (size_t i = 0; i < 3; i++)
      ASSERT_TRUE(
          !strcmp(caller_messages[i], callers.errors.items[i]->message));
    Result collision = invalid(&a,
                               "a() -> :ok[int] | :!bad[int] { 1 }\n"
                               "b() -> :ok[int] | :!bad[str] { 1 }\n"
                               "f() -> :ok[int] | _ { a()?\nb()? }\nmain() {}",
                               "Symbol tag has conflicting payload types.");
    ASSERT_EQ_INT(1, collision.errors.count);
  }
  TEST("recursive success anchors and accumulated error sets reach a fixed "
       "point") {
    Result r = valid(&a, "f(n: int) { if n == 0 { ret :!first }\ng(n - 1)? }\n"
                         "g(n: int) { if n == 0 { ret :!second }\nif n == 1 { "
                         "ret 0.5 }\nf(n - 1)? }\n"
                         "main() {}\n");
    const SemaType *type = r.program->functions[0].symbol.outcome_type;
    ASSERT_TRUE(type == r.program->functions[1].symbol.outcome_type);
    ASSERT_EQ_INT(3, type->symbols->count);
    ASSERT_EQ_INT(SEMA_DBL, sema_single_payload(sema_ok(type))->kind);
    valid(&a, "f(b: bool) { if b { ret :!bad }\ng(b)? }\n"
              "g(b: bool) { if b { ret 1 }\nf(b)? }\nmain() {}\n");
    invalid(&a, "f() { g()? }\ng() { f()? }\nmain() {}",
            "Cannot infer recursive");
  }
  TEST("large inferred error components do not consume the compiler stack") {
    size_t capacity = 350000, length = 0;
    char *text = arena_alloc(&a, capacity);
    for (size_t i = 0; i < 2050; i++)
      length += (size_t)snprintf(
          text + length, capacity - length,
          "f%zu(n: int) { if n == 0 { ret :!%s }\nf%zu(n - 1)?\n1 }\n", i,
          i == 2049 ? "last" : "first", (i + 1) % 2050);
    snprintf(text + length, capacity - length, "main() {}\n");
    Result r = valid(&a, text);
    const SemaType *type = r.program->functions[0].symbol.outcome_type;
    for (size_t i = 0; i < 2050; i++) {
      // Every recursive route reaches an error or recurses forever. The 1
      // after ? cannot be a success anchor for an error-only component.
      const SemaFunction *f = &r.program->functions[i];
      ASSERT_TRUE(type == f->symbol.outcome_type);
      ASSERT_EQ_INT(2, f->symbol.outcome_type->symbols->count);
      ASSERT_TRUE(sema_only_errors(f->symbol.outcome_type));
      ASSERT_TRUE(!sema_ok(f->symbol.outcome_type));
      ASSERT_TRUE(
          !f->body->as.block.items[f->body->as.block.count - 1]->reachable);
    }
    ASSERT_TRUE(
        sema_variant(type, sema_tag(r.program, r.program->module, "first")));
    ASSERT_TRUE(
        sema_variant(type, sema_tag(r.program, r.program->module, "last")));
  }
  TEST("partial-return error closure agrees with an independent graph oracle") {
    uint32_t random = UINT32_C(12345678);
    for (size_t trial = 0; trial < 100; trial++) {
      char text[8192];
      size_t length = 0;
      unsigned reach[6];
      for (size_t i = 0; i < 6; i++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned x = (random >> 16) % 6, y = random % 6;
        reach[i] = (1u << i) | (1u << x) | (1u << y);
        length += (size_t)snprintf(
            text + length, sizeof(text) - length,
            "f%zu(n: int) -> :ok[int] | _ { if n == 0 { ret :!e%zu }\n"
            "f%u(n - 1)?\nf%u(n - 1)?\n1 }\n",
            i, i, x, y);
      }
      snprintf(text + length, sizeof(text) - length, "main() {}\n");
      for (size_t k = 0; k < 6; k++)
        for (size_t i = 0; i < 6; i++)
          if (reach[i] & (1u << k))
            reach[i] |= reach[k];
      Result r = valid(&a, text);
      for (size_t i = 0; i < 6; i++) {
        const SemaType *type = r.program->functions[i].symbol.outcome_type;
        unsigned actual = 0;
        for (size_t j = 0; j < 6; j++) {
          char name[8];
          snprintf(name, sizeof(name), "e%zu", j);
          if (sema_variant(type, sema_tag(r.program, r.program->module,
                                          arena_strdup(&a, name))))
            actual |= 1u << j;
        }
        ASSERT_EQ_INT(reach[i], actual);
        ASSERT_EQ_INT(SEMA_INT, sema_single_payload(sema_ok(type))->kind);
      }
    }
  }
  TEST("permissions follow success projection, handlers and propagated "
       "payloads") {
    const char *prefix = "P { x: int }\npass(p: P) -> :ok[P] | :!bad { p }\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, body), text)
    invalid(&a, CASE("f(p: P) { pass(p)!.x = 1 }"), "Cannot mutate");
    invalid(&a, CASE("f(p: P) { let q = pass(p)?\nq.x = 1 }"), "Cannot mutate");
    invalid(&a,
            CASE("f(p: P) { pass(p) |> { :ok[q] => q.x = 1\n:!bad => () } }"),
            "Cannot mutate");
    valid(&a, CASE("f(p: &P) { pass(p)!.x = 1 }"));
#undef CASE
    invalid(&a,
            "P { x: int }\nidentity(p: P) { :ok[p] }\nf(p: P) { identity(p).x "
            "= 1 }\nmain() {}",
            "Cannot mutate");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
