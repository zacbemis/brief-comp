#include "ir/ir.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/sema.h"
#include "test_utils.h"
#include <float.h>
#include <math.h>

typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, "sema.brf", text, strlen(text));
  TokenArray tokens = newline_filter(a, lexer_tokenize(a, source, &r.errors));
  Program *ast = parse_program(a, &names, tokens, &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static void valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
}
static void invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    if (strstr(r.errors.items[i]->message, message))
      found = 1;
  if (!found) {
    fprintf(stderr, "missing '%s' for %s\n", message, text);
    diagnostics_print(&r.errors, stderr);
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
}
static void real_literal(Arena *a, const char *type, const char *spelling,
                         double expected) {
  size_t capacity = strlen(spelling) + 64;
  char *text = arena_alloc(a, capacity);
  snprintf(text, capacity, "main() {}\nvalue(): %s { %s }", type, spelling);
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  SemaExpr *literal = r.program->functions[1].body->as.block.items[0];
  ASSERT_TRUE(literal->real == expected);
  ASSERT_TRUE(!!signbit(literal->real) == !!signbit(expected));
}
static size_t message_count(const Diagnostics *errors, const char *message) {
  size_t count = 0;
  for (size_t i = 0; i < errors->count; i++)
    if (strstr(errors->items[i]->message, message))
      count++;
  return count;
}
static Result generic_error(Arena *a, const char *text, const char *site,
                             const char *other, size_t other_count) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, "generic.brf", text, strlen(text));
  TokenArray tokens = newline_filter(a, lexer_tokenize(a, source, &r.errors));
  Program *ast = parse_program(a, &names, tokens, &r.errors);
  ASSERT_EQ_INT(0, r.errors.count);
  size_t bytes = tokens.count * sizeof(Token);
  Token *saved = arena_alloc(a, bytes);
  memcpy(saved, tokens.items, bytes);
  // Re-resolve the same immutable AST: resolution diagnostics must not rely
  // on state left by checking an earlier semantic program.
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    diagnostics_init(&r.errors, a);
    r.program = sema_resolve(a, ast, &r.errors);
    sema_check(r.program, &r.errors);
    if (r.errors.count != 1 + other_count) {
      fprintf(stderr, "generic recovery case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1 + other_count, r.errors.count);
    ASSERT_EQ_INT(1, message_count(&r.errors, "Generic calls are not implemented yet."));
    ASSERT_EQ_INT(other_count, other ? message_count(&r.errors, other) : 0);
    const char *call = strstr(text, site);
    ASSERT_TRUE(call != NULL);
    for (size_t i = 0; i < r.errors.count; i++) {
      Diagnostic *d = r.errors.items[i];
      if (!strcmp(d->message, "Generic calls are not implemented yet.") && call) {
        ASSERT_EQ_INT((size_t)(call - text), d->span.start);
        ASSERT_EQ_INT((size_t)(call - text) + strlen(site), d->span.end);
      }
    }
    ASSERT_TRUE(!r.program->resolved);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
    ASSERT_TRUE(memcmp(saved, tokens.items, bytes) == 0);
  }
  return r;
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("all primitives are canonical parameter and return types") {
    const char *types[] = {"()",  "str",  "bool", "chr",  "shrt", "ushrt",
                           "int", "uint", "lng",  "ulng", "flt",  "dub"};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() {}\nf(x: %s): %s { x }", types[i],
               types[i]);
      Result r = check(&a, text);
      ASSERT_EQ_INT(0, r.errors.count);
      ASSERT_TRUE(r.program->functions[1].symbol.params[0]->type ==
                  r.program->functions[1].symbol.return_type);
    }
    valid(&a, "main() { let x = 1\nif true { let x = x + 1 }\nx }");
    valid(&a,
          "main() { \"a normal result is discarded by the entry wrapper\" }");
  }
  TEST("bindings are lexical, checked in declaration order, and mutable only "
       "with var") {
    invalid(&a, "main() { x\nlet x = 1 }", "Unknown binding");
    invalid(&a, "main() { let x = x }", "Unknown binding");
    invalid(&a, "main() { if true { let x = 1 }\nx }", "Unknown binding");
    invalid(&a, "main() { let x = 1\nlet x = 2 }", "Duplicate binding");
    invalid(&a, "main() {}\nf(x: int, x: int) {}", "Duplicate binding");
    invalid(&a, "main() { let x = 1\nx = 2 }", "immutable binding");
    invalid(&a, "main() { let x = 1\nx++ }", "immutable binding");
    invalid(&a, "main() {}\nf(x: int) { x += 1 }", "immutable binding");
    invalid(&a, "main() { let f = 1\nf() }\nf() {}", "not callable");
    invalid(&a, "main() { f }\nf() {}", "Function values");
    invalid(&a, "main() { var x = 1\nx = true }", "Assignment value");
    invalid(&a, "main() { var x = true\nx += false }", "Compound assignment");
    invalid(&a, "main() { (1 + 2)++ }", "increment/decrement target");
    invalid(&a, "main() { var flag = false\nflag++ }", "numeric binding");
    invalid(&a, "main() { missing++ }", "Unknown binding");
    invalid(&a, "main() { object[0]++ }", "Index updates");
    Result updates = check(&a, "main() { var n = 0\nn++\nn-- }");
    ASSERT_EQ_INT(0, updates.errors.count);
    ASSERT_TRUE(updates.program->functions[0].symbol.return_type ==
                &updates.program->types[SEMA_UNIT]);
    ASSERT_EQ_INT(SEMA_EXPR_UPDATE,
                  updates.program->functions[0].body->as.block.items[1]->kind);
    ASSERT_TRUE(updates.program->functions[0].body->as.block.items[1]->type ==
                &updates.program->types[SEMA_UNIT]);
  }
  TEST("unsupported generic headers do not resolve as ordinary callees") {
    const char *prefix =
        "P {}\nimpl P { make(value: int) {}\nread(p: Self, n: int) {} }\n"
        "f(value: shrt) {}\ncheck(value: int) {}\n";
    struct { const char *body, *site; } cases[] = {
        {"f<int>()", "f<int>()"},
        {"f<int>(true)", "f<int>(true)"},
        {"int<int>(\"1\")", "int<int>(\"1\")"},
        {"print<int>(false)", "print<int>(false)"},
        {"missing<int>()", "missing<int>()"},
        {"let f = 0\nf<int>()", "f<int>()"},
        {"let a = 1\nlet d = 2\ncheck(a < b, c > (d))", "a < b, c > (d)"},
        {"P.make<int>()", "P.make<int>()"},
        {"P.missing<int>()", "P.missing<int>()"},
        {"let p = P {}\np.read<int>(true)", "p.read<int>(true)"},
        {"let p = P {}\np.missing<int>()", "p.missing<int>()"},
        {"let P = 1\nP.read<int>()", "P.read<int>()"},
        {"1 |> missing<int>()", "missing<int>()"},
        {"let f = 0\n1 |> f<int>()", "f<int>()"},
        {"P {} |> P.make<int>()", "P.make<int>()"},
        {"f<Sym<:ok(Sym<:inner(int)>)>>(1)", "f<Sym<:ok(Sym<:inner(int)>)>>(1)"},
        {"f<int>()?", "f<int>()"}, {"f<int>()!", "f<int>()"},
        {"f<int>() |> { _ => () }", "f<int>()"},
        {"let value = f<int>()\nvalue.field", "f<int>()"}};
    char code[2048], crlf[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "%smain() {\n%s\n}", prefix, cases[i].body);
      size_t n = 0;
      for (size_t j = 0; code[j]; j++) {
        if (code[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = code[j];
      }
      crlf[n] = '\0';
      generic_error(&a, code, cases[i].site, NULL, 0);
      generic_error(&a, crlf, cases[i].site, NULL, 0);
    }
    Result r = generic_error(&a,
        "callee(value: int): int { value }\nmain() { callee<int>(callee(1)) }",
        "callee<int>(callee(1))", NULL, 0);
    SemaFunction *caller = &r.program->functions[1];
    SemaExpr *call = caller->body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_EXPR_CALL, call->kind);
    ASSERT_TRUE(call->as.call.target == NULL);
    ASSERT_TRUE(call->as.call.method == NULL);
    ASSERT_TRUE(call->as.call.fallback == NULL);
    ASSERT_EQ_INT(SEMA_ERROR, call->type->kind);
    ASSERT_EQ_INT(1, call->as.call.count);
    ASSERT_TRUE(call->flow & SEMA_NEXT);
    ASSERT_TRUE(call->as.call.args[0]->as.call.target == &r.program->functions[0].symbol);
    ASSERT_TRUE(caller->edges != NULL);
    if (caller->edges) {
      ASSERT_EQ_INT(r.program->functions[0].symbol.id, caller->edges->callee);
      ASSERT_TRUE(caller->edges->next == NULL);
    }
    Diagnostics again;
    diagnostics_init(&again, &a);
    sema_check(r.program, &again);
    ASSERT_EQ_INT(0, again.count); // The primary belongs to resolution.
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    Result nested = check(&a, "main() { missing<int>(other<int>()) }");
    ASSERT_EQ_INT(2, nested.errors.count);
    ASSERT_EQ_INT(2, message_count(&nested.errors,
                                  "Generic calls are not implemented yet."));
    ASSERT_TRUE(!nested.program->checked);
    ASSERT_TRUE(ir_lower(&a, nested.program) == NULL);
    valid(&a, "check(left: bool, right: bool) {}\n"
              "main() { let a = 1\nlet b = 2\nlet c = 3\nlet d = 1\n"
              "check(a < b, c > d)\ncheck((a < b), (c > (d))) }");
    invalid(&a, "main() { let f = 0\nf() }", "A local binding is not callable.");
    invalid(&a, "main() { missing() }", "Unknown function.");
    invalid(&a, "P {}\nmain() { P.missing() }", "Unknown associated function.");
    invalid(&a, "P {}\nmain() { P {}.missing() }", "Unknown method for this type.");
  }
  TEST("generic recovery still checks receiver, argument and surrounding syntax") {
    struct { const char *text, *site, *other; } cases[] = {
        {"main() { missing<int>(unknown) }", "missing<int>(unknown)", "Unknown binding."},
        {"main() { missing.read<int>() }", "missing.read<int>()", "Unknown binding."},
        {"P {}\nmake(n: int) { P {} }\nmain() { make(false).read<int>() }",
         "make(false).read<int>()", "Function argument has the wrong type."},
        {"main() { missing<int>(if true { 1 } else { false }) }",
         "missing<int>(if true { 1 } else { false })",
         "Value-producing if branches must have compatible types."},
        {"main() { missing<int>(if true {}) }", "missing<int>(if true {})",
         "An if without else cannot be used as a value."},
        {"take(n: int) {}\nmain() { missing<int>(take(false)) }",
         "missing<int>(take(false))", "Function argument has the wrong type."},
        {"fail(): Sym<:ok(int), :bad> { 1 }\nmain() { missing<int>(fail()) }",
         "missing<int>(fail())", "A call that can fail must be handled with ?, !, or a match."},
        {"P {}\nmut(p: &P) {}\nf(p: P) { missing<int>(mut(p)) }\nmain() {}",
         "missing<int>(mut(p))", "A read-only value cannot be passed to an & parameter; "
         "take the parameter it comes from as &."},
        {"main() { missing<int>()\nlet bad: int = false }", "missing<int>()",
         "Binding value does not match its annotation."},
        {"main() { let local = 0\nmissing<int>()\nlocal() }", "missing<int>()",
         "A local binding is not callable."},
        {"check(left: bool, right: bool) {}\nmain() { let a = 1\nlet d = 2\n"
         "check(a < b, c > (d)) }", "a < b, c > (d)", "Wrong number of function arguments."},
        {"bad(flag: bool) { if flag { ret 1 }\nfalse }\nmain() { missing<int>() }",
         "missing<int>()", "Incompatible return types."},
        {"bad(): () { missing<int>()\nret 1 }\nmain() {}", "missing<int>()",
         "Return value does not match the written return type."},
        {"main() { missing<int>(1 + true) }", "missing<int>(1 + true)",
         "Operator operands must have the same type; numeric conversions are explicit."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      generic_error(&a, cases[i].text, cases[i].site, cases[i].other, 1);
    generic_error(&a,
        "f() { if true { ret 1 }\nmissing<int>(g()) }\ng() { f() }\nmain() {}",
        "missing<int>(g())", NULL, 0);
    Result r = generic_error(&a,
        "f() { missing<int>(if true { ret 1 } else { ret 2 })\nret false }\nmain() {}",
        "missing<int>(if true { ret 1 } else { ret 2 })", NULL, 0);
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    SemaExpr *call = r.program->functions[0].body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_NEVER, call->type->kind);
    ASSERT_TRUE(!(call->flow & SEMA_NEXT));
    ASSERT_TRUE(call->flow & SEMA_RETURN);
  }
  TEST("global const still parses but is rejected as an unimplemented declaration") {
    const char *sources[] = {"const VALUE = 1\nmain() {}",
                             "pub const VALUE = 1\nmain() {}"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      if (r.errors.count)
        ASSERT_EQ_STRN("This top-level declaration is not implemented yet.",
                       r.errors.items[0]->message,
                       strlen(r.errors.items[0]->message));
      ASSERT_TRUE(!r.program->checked);
    }
  }
  TEST("numeric literals use context but typed values never convert "
       "implicitly") {
    valid(&a, "main() { let x: lng = 2\nlet y: lng = 1 + x\nf(3) }\nf(x: "
              "shrt): shrt { x }");
    invalid(&a, "main() { let x: lng = 1\nlet y: int = x }", "annotation");
    invalid(&a, "main() { let x: lng = 1\nlet y: int = 2\nx + y }",
            "same type");
    invalid(&a, "main() { f(true) }\nf(x: int) {}",
            "argument has the wrong type");
    invalid(&a, "main() { let x: flt = 0.5\nlet y: dub = x }", "annotation");
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
    invalid(&a, "main() { 1.0 % 2.0 }", "requires integers");
    invalid(&a, "main() { true + false }", "requires numbers");
    invalid(&a, "main() { 1 and 2 }", "require bool");
    invalid(&a, "main() { !1 }", "requires bool");
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
        &a, "dub",
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
    snprintf(huge, sizeof(huge), "main() { let x: dub = 1%0400d.0 }", 0);
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
    valid(&a, "main(): () { if true { 1 } else { false } }");
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
    invalid(&a, "main() {}\nf(x: bool): int { if x { ret 1 } }",
            "written return type");
    invalid(&a, "main() {}\nf(x: bool) { if x { ret true }\n1 }",
            "Incompatible return types");
    valid(&a, "main() {}\nf() { ret 1\ntrue }");
    valid(&a, "main() {}\nf(x: bool): int { if x { ret 1 } else { ret 2 } }");
    valid(&a, "main() {}\nf(): int { while true {} }");
    valid(&a, "main() {}\nf(x: bool): () { if x { ret }\n\"discard\" }");
    valid(&a, "main() { let zero: uint = -0 }");
    Result dead = check(&a, "main() {}\nf() { for i in (if true { ret 1 } else "
                            "{ ret 2 })..0 { let x: lng = 1\nret x } }");
    ASSERT_EQ_INT(0, dead.errors.count);
    ASSERT_EQ_INT(SEMA_INT,
                  dead.program->functions[1].symbol.return_type->kind);
    invalid(&a, "main() {}\nf(): int { ret true }", "written return type");
    invalid(&a, "main() {}\nf(): () { ret 1 }", "written return type");
    invalid(&a, "main() {}\nf() { f() }", "Cannot infer recursive");
    valid(&a, "main() {}\nf(): int { f() }");
    valid(&a, "main() {}\nf(x: bool) { if x { g(x) } else { 1.0 } }\ng(x: "
              "bool) { if x { f(x) } else { 1 } }");
    invalid(&a,
            "main() {}\nf(x: bool) { if x { g(x) } else { true } }\ng(x: bool) "
            "{ if x { f(x) } else { 1 } }",
            "consistent return type");
  }
  TEST("recursive probe errors preserve contextual literal anchors") {
    struct { const char *type, *bad, *good; } cases[] = {
        {"shrt", "40000", "32000"}, {"ushrt", "-1", "65535"},
        {"uint", "-1", "4294967295"},
        {"ulng", "-1", "18446744073709551615"},
        {"flt", "350000000000000000000000000000000000000.0", "1.0000001"}};
    const char *message = "Number literal is outside the range of its type.";
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char f[512], g[512], text[2048], crlf[4096];
      snprintf(f, sizeof(f), "f(p: %s, b: bool) { if b { ret p }\ng(p, b) }\n",
               cases[i].type);
      for (unsigned order = 0; order < 2; order++) {
        snprintf(g, sizeof(g), "g(p: %s, b: bool) { if b { ret f(p, b) }\n%s }\n",
                 cases[i].type, cases[i].bad);
        snprintf(text, sizeof(text), "%s%smain() {}", order ? g : f, order ? f : g);
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
                ASSERT_EQ_INT((size_t)(at - source), r.errors.items[0]->span.start);
                ASSERT_EQ_INT((size_t)(at - source) + strlen(cases[i].bad),
                              r.errors.items[0]->span.end);
              }
            }
            SemaFunction *anchor = &r.program->functions[order ? 1 : 0];
            SemaFunction *peer = &r.program->functions[order ? 0 : 1];
            ASSERT_EQ_INT(anchor->component, peer->component);
            ASSERT_TRUE(anchor->symbol.return_type == anchor->symbol.params[0]->type);
            ASSERT_TRUE(peer->symbol.return_type == anchor->symbol.return_type);
            ASSERT_TRUE(!anchor->inference_conflict && !peer->inference_conflict);
            ASSERT_TRUE(!r.program->checked);
            ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
          }
        }
        snprintf(g, sizeof(g), "g(p: %s, b: bool) { if b { ret f(p, b) }\n%s }\n",
                 cases[i].type, cases[i].good);
        snprintf(text, sizeof(text), "%s%smain() {}", order ? g : f, order ? f : g);
        Result good = check(&a, text);
        if (good.errors.count)
          diagnostics_print(&good.errors, stderr);
        ASSERT_EQ_INT(0, good.errors.count);
        ASSERT_TRUE(good.program->checked);
        ASSERT_TRUE(ir_lower(&a, good.program) != NULL);
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
    snprintf(text + length, capacity - length, "f2048(): () { f0() }\n");
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
