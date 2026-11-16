#include "sema_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("all primitives are canonical parameter and return types") {
    const char *types[] = {"()",  "str",  "bool", "chr",  "shrt", "ushrt",
                           "int", "uint", "lng",  "ulng", "flt",  "dbl"};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() {}\nf(x: %s) -> %s { x }", types[i],
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
  TEST("dbl is the canonical binary64 type and converter") {
    Result r =
        check(&a, "f(x: dbl) -> dbl { x }\nmain() { let x: dbl = dbl(1)\n"
                  "f(x)\n0.5 |> dbl() }");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_TRUE(r.program->functions[0].symbol.return_type ==
                &r.program->types[SEMA_DUB]);
    ASSERT_EQ_INT(0, strcmp("dbl", r.program->types[SEMA_DUB].name));
    invalid(&a, "main() { let dbl = 1 }", "Expected a binding name.");
  }
  TEST("pub on a struct type blocks lowering, not field visibility") {
    Result r = check(&a, "pub P { pub x: int }\nmain() { P { x = 1 } }");
    ASSERT_EQ_INT(1, r.errors.count);
    ASSERT_TRUE(!r.program->resolved && !r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    valid(&a, "P { pub x: int }\nmain() { P { x = 1 } }");
  }
  TEST("indices remain unsupported") {
    invalid(&a, "main() { 0.(0) }", "This expression is not implemented yet.");
    invalid(&a, "main() { 0.(0) = 1 }",
            "Index assignments are not implemented yet.");
    invalid(&a, "main() { 0.(0)++ }", "Index updates are not implemented yet.");
  }
  TEST("declaration casing errors disable lowering without changing lookup") {
    const char *sources[] = {
        "date { day: int }\nmain() {}", "Read() {}\nmain() {}",
        "P { Field: int }\nmain() {}",  "f(Param: int) {}\nmain() {}",
        "main() { let Count = 0 }",     "main() { for Index in 0..2 {} }"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      if (r.errors.count)
        ASSERT_TRUE(r.errors.items[0]->fixes != NULL);
    }
    valid(&a, "P { value: int }\nread(p: P) -> int { p.value }\nmain() { "
              "read(P { value = 1 }) }");
  }
  TEST("invalid tag names block lowering without folding their identities") {
    const char *sources[] = {"A = :Bad\nmain() {}", "main() { :Bad }",
                             "main() { :ok[1] |> { :Bad => ()\n_ => () } }"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_TRUE(r.errors.count > 0);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      if (r.errors.count) {
        ASSERT_EQ_INT(0,
                      strcmp("Symbol tags must start with a lowercase letter.",
                             r.errors.items[0]->message));
        ASSERT_TRUE(r.errors.items[0]->fixes != NULL);
      }
    }
    valid(&a, "A = :value_HTTP[int]\nmain() { :value_HTTP[1] |> { "
              ":value_HTTP[_] => () } }");
  }
  TEST("imports are a scope barrier, not a module loader or name binding") {
    const char *sources[] = {"import doesNotExist.User\nmain() {}",
                             ("import models.User\nf(x: User) -> User { "
                              "User.new() }\nmain() { missing() }"),
                             "import std.Json\n"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      ASSERT_EQ_INT(0, r.program->count);
      if (r.errors.count)
        ASSERT_EQ_INT(0, strcmp("Imports are not implemented yet.",
                                r.errors.items[0]->message));
    }
    Result r = check(&a, "import models.User\nimport std.Json\nmain() {}\n");
    ASSERT_EQ_INT(2, r.errors.count);
    for (size_t i = 0; i < r.errors.count; i++)
      ASSERT_EQ_INT(0, strcmp("Imports are not implemented yet.",
                              r.errors.items[i]->message));
  }
  TEST("collection literal syntax does not enable collection lowering") {
    const char *sources[] = {
        "main() { Arr[int] { 1, 2 } }", "main() { Vec[str] {} }",
        "main() { Vec[Unknown] { missing() } }",
        "main() { Vec[Arr[int]] { Arr[int] { missing() } } }"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      if (r.errors.count)
        ASSERT_EQ_INT(0, strcmp("Collection literals are not implemented yet.",
                                r.errors.items[0]->message));
    }
  }
  TEST("legacy alias modifiers cannot reach lowering") {
    const char *sources[] = {"type A = :a\nmain() {}", "pub A = :a\nmain() {}"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    }
    valid(&a, "A = :a\nmain() { :a |> { A => () } }");
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
    invalid(&a, "main() { object.(0)++ }", "Index updates");
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
        "P { unit: ()\n make(value: int) {}\nread(p: P, n: int) {} }\n"
        "f(value: shrt) {}\ncheck(value: int) {}\n";
    struct {
      const char *body, *site;
    } cases[] = {
        {"f[int]()", "f[int]()"},
        {"f[int](true)", "f[int](true)"},
        {"int[int](\"1\")", "int[int](\"1\")"},
        {"print[int](false)", "print[int](false)"},
        {"missing[int]()", "missing[int]()"},
        {"let f = 0\nf[int]()", "f[int]()"},
        {"let a = 1\nlet d = 2\ncheck(a[b, c](d))", "a[b, c](d)"},
        {"P.make[int]()", "P.make[int]()"},
        {"P.missing[int]()", "P.missing[int]()"},
        {"let p = P { unit = () }\np.read[int](true)", "p.read[int](true)"},
        {"let p = P { unit = () }\np.missing[int]()", "p.missing[int]()"},
        {"let p = 1\np.read[int]()", "p.read[int]()"},
        {"1 |> missing[int]()", "missing[int]()"},
        {"let f = 0\n1 |> f[int]()", "f[int]()"},
        {"P { unit = () } |> P.make[int]()", "P.make[int]()"},
        {"f[:ok[:inner[int]]](1)", "f[:ok[:inner[int]]](1)"},
        {"f[int]()?", "f[int]()"},
        {"f[int]()!", "f[int]()"},
        {"f[int]() |> { _ => () }", "f[int]()"},
        {"let value = f[int]()\nvalue.field", "f[int]()"}};
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
                             "callee(value: int) -> int { value }\nmain() { "
                             "callee[int](callee(1)) }",
                             "callee[int](callee(1))", NULL, 0);
    SemaFunction *caller = &r.program->functions[1];
    SemaExpr *call = caller->body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_EXPR_CALL, call->kind);
    ASSERT_TRUE(call->as.call.target == NULL);
    ASSERT_TRUE(call->as.call.method == NULL);
    ASSERT_TRUE(call->as.call.fallback == NULL);
    ASSERT_EQ_INT(SEMA_ERROR, call->type->kind);
    ASSERT_EQ_INT(1, call->as.call.count);
    ASSERT_TRUE(call->flow & SEMA_NEXT);
    ASSERT_TRUE(call->as.call.args[0]->as.call.target ==
                &r.program->functions[0].symbol);
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
    Result nested = check(&a, "main() { missing[int](other[int]()) }");
    ASSERT_EQ_INT(2, nested.errors.count);
    ASSERT_EQ_INT(2, message_count(&nested.errors,
                                   "Generic calls are not implemented yet."));
    ASSERT_TRUE(!nested.program->checked);
    ASSERT_TRUE(ir_lower(&a, nested.program) == NULL);
    valid(&a, "check(left: bool, right: bool) {}\n"
              "main() { let a = 1\nlet b = 2\nlet c = 3\nlet d = 1\n"
              "check(a < b, c > d)\ncheck((a < b), (c > (d)))\ncheck(a < b, c "
              "> (d)) }");
    invalid(&a, "main() { let f = 0\nf() }",
            "A local binding is not callable.");
    invalid(&a, "main() { missing() }", "Unknown function.");
    invalid(&a, "P { unit: () }\nmain() { P.missing() }",
            "Unknown associated function.");
    invalid(&a, "P { unit: () }\nmain() { P { unit = () }.missing() }",
            "Unknown method for this type.");
  }
  TEST("generic recovery still checks receiver, argument and surrounding "
       "syntax") {
    struct {
      const char *text, *site, *other;
    } cases[] = {
        {"main() { missing[int](unknown) }", "missing[int](unknown)",
         "Unknown binding."},
        {"main() { missing.read[int]() }", "missing.read[int]()",
         "Unknown binding."},
        {"P { unit: () }\nmake(n: int) { P { unit = () } }\nmain() { "
         "make(false).read[int]() }",
         "make(false).read[int]()", "Function argument has the wrong type."},
        {"main() { missing[int](if true { 1 } else { false }) }",
         "missing[int](if true { 1 } else { false })",
         "Value-producing if branches must have compatible types."},
        {"main() { missing[int](if true {}) }", "missing[int](if true {})",
         "An if without else cannot be used as a value."},
        {"take(n: int) {}\nmain() { missing[int](take(false)) }",
         "missing[int](take(false))", "Function argument has the wrong type."},
        {"fail() -> :ok[int] | :!bad { 1 }\nmain() { missing[int](fail()) }",
         "missing[int](fail())",
         "A call that can fail must be handled with ?, !, or a match."},
        {"P { unit: () }\nmut(p: &P) {}\nf(p: P) { missing[int](mut(p)) "
         "}\nmain() {}",
         "missing[int](mut(p))",
         "A read-only value cannot be passed to an & parameter; "
         "take the parameter it comes from as &."},
        {"main() { missing[int]()\nlet bad: int = false }", "missing[int]()",
         "Binding value does not match its annotation."},
        {"main() { let local = 0\nmissing[int]()\nlocal() }", "missing[int]()",
         "A local binding is not callable."},
        {"check(left: bool, right: bool) {}\nmain() { let a = 1\nlet d = 2\n"
         "check(a[b, c](d)) }",
         "a[b, c](d)", "Wrong number of function arguments."},
        {"bad(flag: bool) { if flag { ret 1 }\nfalse }\nmain() { "
         "missing[int]() }",
         "missing[int]()", "Incompatible return types."},
        {"bad() -> () { missing[int]()\nret 1 }\nmain() {}", "missing[int]()",
         "Return value does not match the written return type."},
        {"main() { missing[int](1 + true) }", "missing[int](1 + true)",
         "Operator operands must have the same type; numeric conversions are "
         "explicit."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      generic_error(&a, cases[i].text, cases[i].site, cases[i].other, 1);
    generic_error(
        &a,
        "f() { if true { ret 1 }\nmissing[int](g()) }\ng() { f() }\nmain() {}",
        "missing[int](g())", NULL, 0);
    Result r = generic_error(&a,
                             "f() { missing[int](if true { ret 1 } else { ret "
                             "2 })\nret false }\nmain() {}",
                             "missing[int](if true { ret 1 } else { ret 2 })",
                             NULL, 0);
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    SemaExpr *call = r.program->functions[0].body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_NEVER, call->type->kind);
    ASSERT_TRUE(!(call->flow & SEMA_NEXT));
    ASSERT_TRUE(call->flow & SEMA_RETURN);
  }
  TEST("global const still parses but is rejected as an unimplemented "
       "declaration") {
    const char *sources[] = {"const value = 1\nmain() {}",
                             "pub const value = 1\nmain() {}"};
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
  arena_destroy(&a);
  RUN_TESTS();
}
