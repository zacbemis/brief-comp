#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  Diagnostics errors;
  SemaProgram *program;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "errors.brf", text, strlen(text));
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
static Result invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    found |= strstr(r.errors.items[i]->message, message) != NULL;
  if (!found)
    diagnostics_print(&r.errors, stderr);
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  return r;
}
static void handling_errors(Arena *a, const char *text, size_t handling,
                             const char *other, size_t other_count) {
  const char *required =
      "A call that can fail must be handled with ?, !, or a match.";
  Result r = check(a, text);
  unsigned repeats = r.program->resolved ? 2u : 1u;
  for (unsigned repeat = 0; repeat < repeats; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != handling + other_count) {
      fprintf(stderr, "handling recovery case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(handling + other_count, r.errors.count);
    size_t actual_handling = 0, actual_other = 0;
    for (size_t i = 0; i < r.errors.count; i++) {
      const char *message = r.errors.items[i]->message;
      actual_handling += strcmp(message, required) == 0;
      actual_other += other && strcmp(message, other) == 0;
    }
    ASSERT_EQ_INT(handling, actual_handling);
    ASSERT_EQ_INT(other_count, actual_other);
    if (!other)
      for (size_t i = 0; i < r.program->count; i++)
        ASSERT_TRUE(!r.program->functions[i].inference_conflict);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("success-only functions retain their plain ABI and support symbol "
       "handlers") {
    Result r = valid(&a, "double(n: int): int { n * 2 }\n"
                         "only(): Sym<:ok(str)> { :ok(\"a\" + \"b\") }\n"
                         "main() { let n = double(2) + 1\n"
                         "double(n) |> { :ok(value) => print(str(value)) }\n"
                         "only() |> print()\nprint(only()!)\nprint(only()?) }");
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    ASSERT_TRUE(sema_ok(r.program->functions[0].symbol.outcome_type) != NULL);
    ASSERT_EQ_INT(SEMA_STR, r.program->functions[1].symbol.return_type->kind);
    valid(&a, "main() { let n = :ok(1)\nprint(str(n))\n"
              "let x: Sym<:ok(int)> = :ok(2)\nprint(str(x))\n"
              "x |> { :ok(v) => print(str(v)) } }");
    valid(&a,
          "f(): Sym<:ok(())> { :ok(()) }\nmain() { f() |> { :ok(x) => x } }");
    valid(&a, "f(): lng { ret :ok(4294967296) }\n"
              "g(): flt { :ok(0.1) }\nmain() { f()\ng() }");
    valid(&a,
          "type Data = Sym<:a, :b>\n"
          "f(): Sym<:ok(Data)> { :ok(:a) }\n"
          "main() { f() |> { :ok(data) => data |> { :a => ()\n:b => () } } }");
    invalid(&a, "main() { 1? }", "require a symbol outcome");
    invalid(&a, "main() { :data! }", "require a symbol outcome");
    invalid(&a, "f() {}\nmain() { f() |> { :ok(x) => x } }", "payload shape");
  }
  TEST("typed errors propagate and panic, with explicit or implicit success") {
    valid(&a,
          "f(b: bool): Sym<:ok(lng), :bad(str)> { if b { ret :bad(\"bad\") }\n"
          "4294967296 }\n"
          "g(): Sym<:ok(lng), :bad(str)> { f(false)? }\n"
          "h(): Sym<:ok(lng), :bad(str)> { :ok(f(false)!) }\n"
          "main() { print(str(g()?))\nprint(str(h()!)) }");
    valid(&a, "f(): Sym<:ok, :bad> { :ok }\nmain() { f()?\nf()! }");
    invalid(&a,
            "f(): Sym<:ok(int), :bad> { :bad }\ng(): int { f()? }\nmain() {}",
            "Propagated variants");
    invalid(&a,
            "f(): Sym<:ok(lng), :bad> { 1 + 4294967296 }\n"
            "g(): Sym<:ok(int), :bad> { f()? }\nmain() {}",
            "body does not match");
    invalid(&a, "f(): Sym<:ok(int)> { ret :bad }\nmain() {}", "Return value");
  }
  TEST("fallible calls must be handled at the call, in every position") {
    const char *prefix = "f(): Sym<:ok(int), :bad> { 1 }\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%smain() { %s }", prefix, body), text)
    invalid(&a, CASE("f()"), "must be handled");
    invalid(&a, CASE("let x = f()"), "must be handled");
    invalid(&a, CASE("ret f()"), "must be handled");
    invalid(&a, CASE("print(str(f()))"), "must be handled");
    invalid(&a, CASE(":ok(f())"), "must be handled");
    invalid(&a, CASE("f() |> str()"), "must be handled");
    valid(&a, CASE("f() |> { :ok(n) => print(str(n))\n:bad => () }"));
    valid(&a, CASE("print(str(f()!))\nf()?"));
#undef CASE
    valid(&a, "f() { :a }\nmain(): () { let x = f()\nf() }");
    invalid(&a,
            "f(): Sym<:ok(int), :bad> { 1 }\nmain() { true |> { true => "
            "()\nfalse => f() } }",
            "must be handled");
  }
  TEST("unhandled outcomes do not become usable binding or argument types") {
    const char *prefix =
        "P { value: int }\nPair { number: int\nflag: bool }\n"
        "type Result = Sym<:ok(int), :bad>\n"
        "source(): Result { 1 }\n"
        "text(): Sym<:ok(str), :bad> { \"owned\" + \" text\" }\n"
        "object(): Sym<:ok(P), :bad> { P { value: 1 } }\n"
        "effect(): Sym<:ok, :bad> { :ok }\n"
        "take(value: int, flag: bool) {}\ntakeFloat(value: flt) {}\n"
        "takeData(value: Result) {}\n"
        "impl P { read(value: Self): int { value.value } }\n";
    struct {
      const char *bad, *good;
    } cases[] = {
        {"f() { let value = source()\ntake(value, true) }",
         "f() { let value = source()!\ntake(value, true) }"},
        {"f() { take(source(), true) }", "f() { take(source()!, true) }"},
        {"f() { take((source()), true) }", "f() { take((source())!, true) }"},
        {"f() { let value: int = source()\ntake(value, true) }",
         "f() { let value: int = source()!\ntake(value, true) }"},
        {"f() { let value = object()\nvalue.value }",
         "f() { let value = object()!\nvalue.value }"},
        {"f() { object().value }", "f() { object()!.value }"},
        {"f() { object().read() }", "f() { object()!.read() }"},
        {"f() { print(text()) }", "f() { print(text()!) }"},
        {"f() { str(source()) }", "f() { str(source()!) }"},
        {"f() { int(source()) }", "f() { int(source()!) }"},
        {"f() { source() |> str() }", "f() { source()! |> str() }"},
        {"f() { :outer(source()) }", "f() { :outer(source()!) }"},
        {"f() { takeData(source()) }",
         "f() { takeData(source() |> { other => other }) }"},
        {"f() { let value = source()\nvalue? }",
         "f() { let value = source() |> { other => other }\nvalue? }"},
        {"f() { let value = source()\nvalue! }",
         "f() { let value = source() |> { other => other }\nvalue! }"},
        {"f() { let value = source()\nvalue |> { :ok(n) => take(n, true)\n"
         ":bad => () } }",
         "f() { let value = source() |> { other => other }\n"
         "value |> { :ok(n) => take(n, true)\n:bad => () } }"},
        {"f(): int { ret source() }", "f(): int { ret source()! }"},
        {"f(): Result { source() }", "f(): Result { source()? }"},
        {"f(): Sym<:ok(int), _> { source() }",
         "f(): Sym<:ok(int), _> { source()? }"},
        {"f() { effect() }", "f() { effect()! }"},
        {"f() { ret 0\nlet value = source()\ntake(value, true) }",
         "f() { ret 0\nlet value = source()!\ntake(value, true) }"},
        {"f() { let value = flt(\"1\")\ntakeFloat(value) }",
         "f() { let value = flt(\"1\")!\ntakeFloat(value) }"}};
    char code[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "%s%s\nmain() {}", prefix, cases[i].bad);
      handling_errors(&a, code, 1, NULL, 0);
      snprintf(code, sizeof(code), "%s%s\nmain() {}", prefix, cases[i].good);
      valid(&a, code);
    }
    snprintf(code, sizeof(code), "%s%s", prefix,
             "f() { let value = source()\ntake(value, true) }\nmain() {}");
    Result r = check(&a, code);
    SemaFunction *f = NULL;
    for (size_t i = 0; i < r.program->count; i++)
      if (!strcmp(r.program->functions[i].symbol.name, "f"))
        f = &r.program->functions[i];
    ASSERT_TRUE(f != NULL);
    if (f) {
      SemaExpr *binding = f->body->as.block.items[0];
      SemaExpr *call = binding->as.binding.value;
      ASSERT_EQ_INT(SEMA_ERROR, call->type->kind);
      ASSERT_EQ_INT(SEMA_ERROR, call->raw_type->kind);
      ASSERT_EQ_INT(SEMA_ERROR, binding->as.binding.variable->type->kind);
      ASSERT_TRUE(call->flow & SEMA_NEXT); // Bad values do not hide later syntax.
      ASSERT_TRUE(!f->inference_conflict);
      const char *site = strstr(code, "= source()");
      ASSERT_TRUE(site != NULL);
      if (site && r.errors.count) {
        size_t start = (size_t)(site - code) + 2;
        ASSERT_EQ_INT(start, r.errors.items[0]->span.start);
        ASSERT_EQ_INT(start + strlen("source()"), r.errors.items[0]->span.end);
      }
      ASSERT_TRUE(sema_ok(call->as.call.target->outcome_type) != NULL);
      ASSERT_EQ_INT(2, call->as.call.target->outcome_type->symbols->count);
    }
    valid(&a, "data(): Sym<:left, :right> { :left }\n"
              "only(): Sym<:ok(str)> { :ok(\"owned\" + \" text\") }\n"
              "main() { let data = data()\nlet text = only()\nprint(text)\n"
              "data |> { :left => ()\n:right => () } }");
  }
  TEST("handling recovery preserves unrelated errors and SCC fallibility") {
    const char *prefix =
        "P { value: int }\nPair { number: int\nflag: bool }\n"
        "source(): Sym<:ok(int), :bad> { 1 }\n"
        "take(value: int, flag: bool) {}\n"
        "writable(value: &P): Sym<:ok(P), :bad> { value }\n";
    struct {
      const char *body, *message;
      size_t handling;
    } cases[] = {
        {"f() { take(source(), \"wrong\") }",
         "Function argument has the wrong type.", 1},
        {"f() { let value = source()\ntake(value, \"wrong\") }",
         "Function argument has the wrong type.", 1},
        {"f() { source()\ntake() }", "Wrong number of function arguments.", 1},
        {"f() { let value = source()\nlet wrong: int = false }",
         "Binding value does not match its annotation.", 1},
        {"f() { Pair { number: source(), flag: \"wrong\" } }",
         "Struct field initializer has the wrong type.", 1},
        {"f(): int { source()\nfalse }",
         "Function body does not match the written return type.", 1},
        {"f(): Sym<:ok(int), _> { source()\nfalse }",
         "Function body does not match the written return type.", 1},
        {"f(flag: bool) { if flag { ret 1 }\nif flag { ret true }\nsource() }",
         "Incompatible return types.", 1},
        {"f() { take(source(), missing) }", "Unknown binding.", 1},
        {"f(value: P) { writable(value) }",
         "A read-only value cannot be passed to an & parameter; take the "
         "parameter it comes from as &.", 1}};
    char code[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "%s%s\nmain() {}", prefix, cases[i].body);
      handling_errors(&a, code, cases[i].handling, cases[i].message, 1);
    }
    snprintf(code, sizeof(code), "%s%s", prefix,
             "both(a: int, b: int): Sym<:ok, :other> { :ok }\n"
             "f() { both(source(), source()) }\nmain() {}");
    handling_errors(&a, code, 3, NULL, 0);
    handling_errors(&a,
        "data(): Sym<:left, :right> { :left }\n"
        "take(value: int) {}\nf() { take(data()) }\nmain() {}",
        0, "Function argument has the wrong type.", 1);
    // Deferring, rather than poisoning, probe results keeps the anchored
    // component fallible so its illegal recursive calls cannot disappear.
    handling_errors(&a,
        "a(n: int) { if n == 0 { ret :bad }\nif n == 1 { ret 1 }\nb(n) }\n"
        "b(n: int) { a(n) }\nmain() {}", 1, NULL, 0);
    handling_errors(&a,
        "a(n: int) { if n == 0 { ret :bad }\nif n == 1 { ret 1 }\nb(n) }\n"
        "b(n: int) { if n == 0 { ret :bad }\nif n == 1 { ret 1 }\na(n) }\n"
        "main() {}", 2, NULL, 0);
    handling_errors(&a,
        "a() { b() }\nb() { source() }\n"
        "source(): Sym<:ok(int), :bad> { 1 }\nmain() {}", 1, NULL, 0);
  }
  TEST("return inference accumulates errors and narrows handled variants") {
    Result r = valid(&a, "type Problems = Sym<:a(str), :b(int)>\n"
                         "source(b: bool): Sym<:ok(str), Problems> { if b { "
                         "ret :a(\"a\") }\n\"x\" }\n"
                         "f() { source(false)? }\n"
                         "g() { source(false) |> { :ok(text) => text\n:a(_) => "
                         "\"\"\nother => ret other } }\n"
                         "main() { f()!\ng()! }");
    const SemaType *f = r.program->functions[1].symbol.outcome_type;
    const SemaType *g = r.program->functions[2].symbol.outcome_type;
    ASSERT_EQ_INT(3, f->symbols->count);
    ASSERT_EQ_INT(2, g->symbols->count);
    ASSERT_TRUE(!sema_variant(g, sema_tag(r.program, "a")));
    Result again;
    diagnostics_init(&again.errors, &a);
    sema_check(r.program, &again.errors);
    ASSERT_EQ_INT(0, again.errors.count);
    ASSERT_TRUE(g == r.program->functions[2].symbol.outcome_type);
    invalid(
        &a,
        "a(): Sym<:ok, :bad(int)> { :ok }\nb(): Sym<:ok, :bad(str)> { :ok }\n"
        "f() { a()?\nb()? }\nmain() {}",
        "conflicting payload");
  }
  TEST(
      "partial returns fix success types and grow only compatible error sets") {
    Result r =
        valid(&a, "source(): Sym<:ok(lng), :bad> { 4294967296 }\n"
                  "f(): Sym<:ok(lng), _> { source()? }\nmain() { f()! }");
    ASSERT_EQ_INT(2,
                  r.program->functions[1].symbol.outcome_type->symbols->count);
    valid(&a, "f(b: bool): Sym<:ok(lng), _> { if b { ret :bad }\n4294967296 "
              "}\nmain() {}\n");
    invalid(&a, "f(): Sym<:ok(int), _> { false }\nmain() {}",
            "body does not match");
    invalid(&a, "f(): Sym<:a, _> { :a }\nmain() {}", "must declare :ok");
    invalid(&a, "f(): Sym<:ok, _, _> {}\nmain() {}", "Duplicate partial");
    invalid(&a, "type A = Sym<:ok, _>\nmain() {}", "Partial symbol sets");
  }
  TEST("written return mismatches do not become partial-set collisions") {
    struct {
      const char *text;
      size_t returns, bodies;
    } cases[] = {
        {"f(): Sym<:ok(int), _> { false }", 0, 1},
        {"f(): Sym<:ok(A), _> { B {} }", 0, 1},
        {"f(): Sym<:ok(int), _> { ret false }", 1, 0},
        {"f(b: bool): Sym<:ok(int), _> { if b { ret false }\n1 }", 1, 0},
        {"f(): Sym<:ok(int), _> { :ok(false) }", 0, 1},
        {"f(): Sym<:ok(int), _> { ret :ok }", 1, 0},
        {"f(b: bool): Sym<:ok(int), _> { if b { ret false }\n\"bad\" }", 1, 1},
        {"f(): Sym<:ok(int), _> { ret 1\nret false }", 1, 0},
        {"f(): Sym<:ok(A), _> { B {} }\ng() { f()! }", 0, 1},
        {"f(): Sym<:ok(A), _> { B {} }\ng(): Sym<:ok(int), _> { false }", 0, 2},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[1024];
      snprintf(text, sizeof(text), "A {}\nB {}\n%s\nmain() {}", cases[i].text);
      Result r = check(&a, text);
      for (int repeat = 0; repeat < 2; repeat++) {
        if (repeat) {
          diagnostics_init(&r.errors, &a);
          sema_check(r.program, &r.errors);
        }
        ASSERT_EQ_INT(cases[i].returns + cases[i].bodies, r.errors.count);
        size_t returns = 0, bodies = 0;
        for (size_t j = 0; j < r.errors.count; j++) {
          returns +=
              strcmp(r.errors.items[j]->message,
                     "Return value does not match the written return type.") ==
              0;
          bodies +=
              strcmp(r.errors.items[j]->message,
                     "Function body does not match the written return type.") ==
              0;
        }
        ASSERT_EQ_INT(cases[i].returns, returns);
        ASSERT_EQ_INT(cases[i].bodies, bodies);
        ASSERT_TRUE(!r.program->functions[0].inference_conflict);
        ASSERT_TRUE(!r.program->checked);
        ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
        // Rejected returns contribute nothing: the written :ok remains the
        // function's success type, so callers are still checked against it.
        const SemaFunction *f = &r.program->functions[0];
        ASSERT_EQ_INT(SEMA_SYM, f->symbol.outcome_type->kind);
        const SemaVariant *ok = sema_ok(f->symbol.outcome_type);
        ASSERT_TRUE(ok && ok->payload == sema_ok(f->written_type)->payload);
      }
    }
    Result callers = check(&a, "f(b: bool): Sym<:ok(int), :bad, _> {\n"
                               "if b { ret :bad }\nfalse }\n"
                               "g() { f(true)\nlet s: str = f(true)! }\n"
                               "main() {}");
    const char *caller_messages[] = {
        "Function body does not match the written return type.",
        "A call that can fail must be handled with ?, !, or a match.",
        "Binding value does not match its annotation."};
    ASSERT_EQ_INT(3, callers.errors.count);
    for (size_t i = 0; i < 3; i++)
      ASSERT_TRUE(!strcmp(caller_messages[i], callers.errors.items[i]->message));
    Result collision =
        invalid(&a,
                "a(): Sym<:ok(int), :bad(int)> { 1 }\n"
                "b(): Sym<:ok(int), :bad(str)> { 1 }\n"
                "f(): Sym<:ok(int), _> { a()?\nb()? }\nmain() {}",
                "Symbol tag has conflicting payload types.");
    ASSERT_EQ_INT(1, collision.errors.count);
  }
  TEST("recursive success anchors and accumulated error sets reach a fixed "
       "point") {
    Result r = valid(&a, "f(n: int) { if n == 0 { ret :first }\ng(n - 1)? }\n"
                         "g(n: int) { if n == 0 { ret :second }\nif n == 1 { "
                         "ret 0.5 }\nf(n - 1)? }\n"
                         "main() {}\n");
    const SemaType *type = r.program->functions[0].symbol.outcome_type;
    ASSERT_TRUE(type == r.program->functions[1].symbol.outcome_type);
    ASSERT_EQ_INT(3, type->symbols->count);
    ASSERT_EQ_INT(SEMA_DUB, sema_ok(type)->payload->kind);
    valid(&a, "f(b: bool) { if b { ret :bad }\ng(b)? }\n"
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
          "f%zu(n: int) { if n == 0 { ret :%s }\nf%zu(n - 1)?\n1 }\n", i,
          i == 2049 ? "last" : "first", (i + 1) % 2050);
    snprintf(text + length, capacity - length, "main() {}\n");
    Result r = valid(&a, text);
    for (size_t i = 0; i < 2050; i++)
      ASSERT_EQ_INT(
          3, r.program->functions[i].symbol.outcome_type->symbols->count);
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
            "f%zu(n: int): Sym<:ok(int), _> { if n == 0 { ret :e%zu }\n"
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
          if (sema_variant(type, sema_tag(r.program, arena_strdup(&a, name))))
            actual |= 1u << j;
        }
        ASSERT_EQ_INT(reach[i], actual);
        ASSERT_EQ_INT(SEMA_INT, sema_ok(type)->payload->kind);
      }
    }
  }
  TEST("permissions follow success projection, handlers and propagated "
       "payloads") {
    const char *prefix = "P { x: int }\npass(p: P): Sym<:ok(P), :bad> { p }\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, body), text)
    invalid(&a, CASE("f(p: P) { pass(p)!.x = 1 }"), "Cannot mutate");
    invalid(&a, CASE("f(p: P) { let q = pass(p)?\nq.x = 1 }"), "Cannot mutate");
    invalid(&a,
            CASE("f(p: P) { pass(p) |> { :ok(q) => q.x = 1\n:bad => () } }"),
            "Cannot mutate");
    valid(&a, CASE("f(p: &P) { pass(p)!.x = 1 }"));
#undef CASE
    invalid(&a,
            "P { x: int }\nidentity(p: P) { :ok(p) }\nf(p: P) { identity(p).x "
            "= 1 }\nmain() {}",
            "Cannot mutate");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
