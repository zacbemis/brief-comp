#include "errors_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("success-only functions return plain values, not symbol envelopes") {
    Result r = invalid(&a,
                       "double(n: int) -> int { n * 2 }\n"
                       "only() -> :ok[str] { :ok[\"a\" + \"b\"] }\n"
                       "main() { let n = double(2) + 1\n"
                       "double(n) |> { :ok[value] => print(str(value)) }\n"
                       "only() |> print()\nprint(only()!)\nprint(only()?) }",
                       "it has no error tags");
    ASSERT_EQ_INT(3, r.errors.count);
    valid(&a, "double(n: int) -> int { n * 2 }\n"
              "only() -> :ok[str] { :ok[\"a\" + \"b\"] }\n"
              "main() { let n = double(2) + 1\n"
              "double(n) |> { value => print(str(value)) }\n"
              "only() |> print()\nprint(only())\nprint(only()) }");
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    ASSERT_TRUE(r.program->functions[0].symbol.outcome_type ==
                r.program->functions[0].symbol.return_type);
    ASSERT_EQ_INT(SEMA_STR, r.program->functions[1].symbol.return_type->kind);
    invalid(&a,
            "main() { let n = :ok[1]\nprint(str(n))\n"
            "let x: :ok[int] = :ok[2]\nprint(str(x))\n"
            "x |> { :ok[v] => print(str(v)) } }",
            "Symbol patterns require a symbol input.");
    invalid(&a,
            "f() -> :ok[()] { :ok[()] }\n"
            "main() { f() |> { :ok[x] => x } }",
            "Symbol patterns require a symbol input.");
    valid(&a, "f() -> lng { ret :ok[4294967296] }\n"
              "g() -> flt { :ok[0.1] }\nmain() { f()\ng() }");
    invalid(&a,
            "Data = :a | :b\n"
            "f() -> :ok[Data] { :ok[:a] }\n"
            "main() { f() |> { :ok[data] => data |> { :a => ()\n:b => () } } }",
            "Symbol pattern tag is not in the input set.");
    valid(&a, "Data = :a | :b\nf() -> :ok[Data] { :ok[:a] }\n"
              "main() { f() |> { :a => ()\n:b => () } }");
    invalid(&a, "main() { 1? }", "it has no error tags");
    invalid(&a, "main() { :data! }", "it has no error tags");
    invalid(&a, "f() {}\nmain() { f() |> { :ok[x] => x } }",
            "Symbol patterns require a symbol input.");
  }
  TEST("error-only postfixes terminate in every enclosing value position") {
    const char *prefix = "Error = :!bad[str, (), :!inner] | :!other\n"
                         "source() -> Error { :!bad[\"owned\", (), :!inner] }\n"
                         "Box { value: int }\ng() { ";
    const char *bodies[] = {"source()%c",
                            "let value = source()%c",
                            "print(str(source()%c))",
                            "Box { value = source()%c }",
                            ":box[source()%c]",
                            "ret source()%c",
                            "not source()%c",
                            "source()%c |> print()",
                            "while true { source()%c }",
                            "1 + source()%c"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      for (unsigned panic = 0; panic < 2; panic++)
        for (unsigned crlf = 0; crlf < 2; crlf++) {
          char body[256], lf[1024], source[2048];
          snprintf(body, sizeof(body), bodies[i], panic ? '!' : '?');
          snprintf(lf, sizeof(lf), "%s%s\nprint(\"unreachable\") }\nmain() {}",
                   prefix, body);
          size_t length = 0;
          for (const char *s = lf; *s; s++) {
            if (crlf && *s == '\n')
              source[length++] = '\r';
            source[length++] = *s;
          }
          source[length] = 0;
          Result r = valid(&a, source);
          for (unsigned repeat = 0; repeat < 3; repeat++) {
            if (repeat) {
              diagnostics_init(&r.errors, &a);
              sema_check(r.program, &r.errors);
            }
            ASSERT_EQ_INT(0, r.errors.count);
            ASSERT_TRUE(r.program->checked);
            ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
            const SemaFunction *g = &r.program->functions[1];
            ASSERT_TRUE(!(g->body->flow & SEMA_NEXT));
            ASSERT_TRUE(!g->body->as.block.items[g->body->as.block.count - 1]
                             ->reachable);
            if (panic) {
              ASSERT_EQ_INT(SEMA_UNIT, g->symbol.return_type->kind);
              ASSERT_EQ_INT(0, g->body->flow);
            } else {
              ASSERT_TRUE(g->symbol.return_type == r.program->aliases[0].type);
              ASSERT_TRUE(sema_only_errors(g->symbol.outcome_type));
              ASSERT_EQ_INT(SEMA_RETURN, g->body->flow);
            }
          }
          if (i == 0) {
            const SemaExpr *postfix =
                r.program->functions[1].body->as.block.items[0];
            ASSERT_EQ_INT(SEMA_NEVER, postfix->type->kind);
            ASSERT_TRUE(postfix->failures == postfix->as.value->type);
            IrProgram *ir = ir_lower(&a, r.program);
            ASSERT_TRUE(ir != NULL);
            if (ir) {
              size_t exits = 0;
              for (size_t j = 0; j < ir->functions[1].count; j++) {
                IrOp op = ir->functions[1].instructions[j].op;
                exits += op == (panic ? IR_PANIC_SYMBOL : IR_RETURN);
                ASSERT_TRUE(op != IR_SYMBOL_TAG && op != IR_SYMBOL_PAYLOAD &&
                            op != IR_BRANCH_FALSE);
              }
              ASSERT_EQ_INT(1, exits);
            }
          }
        }
    Result anchored =
        valid(&a, "source() -> :!bad { :!bad }\n"
                  "g(b: bool) { if b { source()? } else { 7 } }\nmain() {}");
    const SemaType *type = anchored.program->functions[1].symbol.outcome_type;
    ASSERT_EQ_INT(2, type->symbols->count);
    ASSERT_EQ_INT(SEMA_INT, sema_single_payload(sema_ok(type))->kind);
    const char *bad = "source() -> :!bad { :!bad }\n"
                      "g() -> int { source()? }\nmain() {}";
    Result r = invalid(
        &a, bad, "Propagated variants are not in the written return type.");
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, &a);
        sema_check(r.program, &r.errors);
      }
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(&a, r.program) == NULL);
      if (r.errors.count) {
        ASSERT_EQ_INT(
            0, strcmp("Propagated variants are not in the written return type.",
                      r.errors.items[0]->message));
        ASSERT_TRUE(!r.errors.items[0]->fixes && !r.errors.items[0]->notes);
      }
    }
    invalid(&a, "main() { :none? }", "it has no error tags");
    invalid(&a, "main() { :box[:!bad]! }", "it has no error tags");
    invalid(&a, "f() -> :ok[int] { :ok[1] }\nmain() { f()! }",
            "it has no error tags");
  }
  TEST("typed errors propagate and panic, with explicit or implicit success") {
    valid(&a,
          "f(b: bool) -> :ok[lng] | :!bad[str] { if b { ret :!bad[\"bad\"] }\n"
          "4294967296 }\n"
          "g() -> :ok[lng] | :!bad[str] { f(false)? }\n"
          "h() -> :ok[lng] | :!bad[str] { :ok[f(false)!] }\n"
          "main() { print(str(g()?))\nprint(str(h()!)) }");
    valid(&a, "f() -> :ok | :!bad { :ok }\nmain() { f()?\nf()! }");
    invalid(&a,
            "f() -> :ok[int] | :!bad { :!bad }\ng() -> int { f()? }\nmain() {}",
            "Propagated variants");
    invalid(&a,
            "f() -> :ok[lng] | :!bad { 1 + 4294967296 }\n"
            "g() -> :ok[int] | :!bad { f()? }\nmain() {}",
            "body does not match");
    invalid(&a, "f() -> :ok[int] { ret :!bad }\nmain() {}", "Return value");
  }
  TEST("fallible calls are handled immediately or returned unchanged") {
    const char *prefix = "f() -> :ok[int] | :!bad { 1 }\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%smain() { %s }", prefix, body), text)
    valid(&a, CASE("f()"));
    invalid(&a, CASE("let x = f()"),
            "Outcome binding is not handled before leaving its scope.");
    valid(&a, CASE("ret f()"));
    invalid(&a, CASE("print(str(f()))"), "must be handled");
    invalid(&a, CASE(":ok[f()]"), "must be handled");
    invalid(&a, CASE("f() |> str()"), "must be handled");
    valid(&a, CASE("f() |> { :ok[n] => print(str(n))\n:!bad => () }"));
    valid(&a, CASE("print(str(f()!))\nf()?"));
#undef CASE
    valid(&a, "f() { :a }\nmain() -> () { let x = f()\nf() }");
    invalid(&a,
            "f() -> :ok[int] | :!bad { 1 }\nmain() { true |> { true => "
            "()\nfalse => f() } }",
            "Symbol tag has conflicting payload types.");
  }
  TEST("stored outcomes retain their type but forbid non-handler transfers") {
    const char *prefix =
        "P { value: int\n read() -> int { self.value } }\n"
        "Pair { number: int\nflag: bool }\nResult = :ok[int] | :!bad\n"
        "source() -> Result { 1 }\n"
        "text() -> :ok[str] | :!bad { \"owned\" + \" text\" }\n"
        "object() -> :ok[P] | :!bad { P { value = 1 } }\n"
        "effect() -> :ok | :!bad { :ok }\n"
        "take(value: int, flag: bool) {}\ntakeFloat(value: flt) {}\n"
        "takeData(value: Result) {}\n";
    struct {
      const char *bad, *good;
      enum {
        CALL_REJECT,
        RETURN_REJECT,
        FLOW_REJECT,
        ANNOTATION_REJECT,
        ARGUMENT_REJECT,
        RETURNS
      } result;
    } cases[] = {
        {"f() { let value = source()\ntake(value, true) }",
         "f() { let value = source()!\ntake(value, true) }", FLOW_REJECT},
        {"f() { take(source(), true) }", "f() { take(source()!, true) }",
         CALL_REJECT},
        {"f() { take((source()), true) }", "f() { take((source())!, true) }",
         CALL_REJECT},
        {"f() { let value: int = source()\ntake(value, true) }",
         "f() { let value: int = source()!\ntake(value, true) }",
         ANNOTATION_REJECT},
        {"f() { let value = object()\nvalue.value }",
         "f() { let value = object()!\nvalue.value }", FLOW_REJECT},
        {"f() { object().value }", "f() { object()!.value }", CALL_REJECT},
        {"f() { object().read() }", "f() { object()!.read() }", CALL_REJECT},
        {"f() { print(text()) }", "f() { print(text()!) }", CALL_REJECT},
        {"f() { str(source()) }", "f() { str(source()!) }", CALL_REJECT},
        {"f() { int(source()) }", "f() { int(source()!) }", CALL_REJECT},
        {"f() { source() |> str() }", "f() { source()! |> str() }",
         CALL_REJECT},
        {"f() { :outer[source()] }", "f() { :outer[source()!] }", CALL_REJECT},
        {"f() { takeData(source()) }",
         "f() { takeData(source() |> { other => other }) }", CALL_REJECT},
        {"f() { let value = source()\nvalue? }",
         "f() { let value = source() |> { other => other }\nvalue? }", RETURNS},
        {"f() { let value = source()\nvalue! }",
         "f() { let value = source() |> { other => other }\nvalue! }", RETURNS},
        {"f() { let value = source()\nvalue |> { :ok[n] => take(n, true)\n"
         ":!bad => () } }",
         "f() { let value = source() |> { other => other }\n"
         "value |> { :ok[n] => take(n, true)\n:!bad => () } }",
         RETURNS},
        {"f() -> int { ret source() }", "f() -> int { ret source()! }",
         RETURN_REJECT},
        {"f() -> Result { source() }", "f() -> Result { source()? }", RETURNS},
        {"f() -> :ok[int] | _ { source() }",
         "f() -> :ok[int] | _ { source()? }", RETURNS},
        {"f() { effect() }", "f() { effect()! }", RETURNS},
        {"f() { ret 0\nlet value = source()\ntake(value, true) }",
         "f() { ret 0\nlet value = source()!\ntake(value, true) }",
         ARGUMENT_REJECT},
        {"f() { let value = flt(\"1\")\ntakeFloat(value) }",
         "f() { let value = flt(\"1\")!\ntakeFloat(value) }", FLOW_REJECT}};
    char code[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "%s%s\nmain() {}", prefix, cases[i].bad);
      if (cases[i].result == RETURNS)
        valid(&a, code);
      else if (cases[i].result == RETURN_REJECT)
        handling_errors(&a, code, 0,
                        "Return value does not match the written return type.",
                        1);
      else if (cases[i].result == FLOW_REJECT)
        diagnostic_errors(
            &a, code,
            "Outcome binding must be handled with ?, !, a match, or return.", 1,
            NULL, 0);
      else if (cases[i].result == ANNOTATION_REJECT)
        diagnostic_errors(&a, code,
                          "Binding value does not match its annotation.", 1,
                          NULL, 0);
      else if (cases[i].result == ARGUMENT_REJECT)
        diagnostic_errors(&a, code, "Function argument has the wrong type.", 1,
                          NULL, 0);
      else
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
      ASSERT_EQ_INT(SEMA_SYM, call->type->kind);
      ASSERT_TRUE(call->raw_type == call->type);
      ASSERT_TRUE(binding->as.binding.variable->type == call->type);
      ASSERT_TRUE(binding->as.binding.variable->outcome_binding);
      ASSERT_TRUE(call->flow &
                  SEMA_NEXT); // Forbidden reads do not hide later syntax.
      ASSERT_TRUE(!f->inference_conflict);
      const char *site = strstr(code, "take(value, true)");
      ASSERT_TRUE(site != NULL);
      if (site && r.errors.count) {
        size_t start = (size_t)(site - code) + strlen("take(");
        ASSERT_EQ_INT(start, r.errors.items[0]->span.start);
        ASSERT_EQ_INT(start + strlen("value"), r.errors.items[0]->span.end);
        ASSERT_TRUE(r.errors.items[0]->notes != NULL);
      }
      ASSERT_TRUE(sema_ok(call->as.call.target->outcome_type) != NULL);
      ASSERT_EQ_INT(2, call->as.call.target->outcome_type->symbols->count);
    }
    valid(&a, "data() -> :left | :right { :left }\n"
              "only() -> :ok[str] { :ok[\"owned\" + \" text\"] }\n"
              "main() { let data = data()\nlet text = only()\nprint(text)\n"
              "data |> { :left => ()\n:right => () } }");
  }
  TEST("handling recovery preserves unrelated errors and SCC fallibility") {
    const char *prefix = "P { value: int }\nPair { number: int\nflag: bool }\n"
                         "source() -> :ok[int] | :!bad { 1 }\n"
                         "take(value: int, flag: bool) {}\n"
                         "writable(value: &P) -> :ok[P] | :!bad { value }\n";
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
        {"f() { Pair { number = source(), flag = \"wrong\" } }",
         "Struct field initializer has the wrong type.", 1},
        {"f() -> int { source()\nfalse }",
         "Function body does not match the written return type.", 1},
        {"f() -> :ok[int] | _ { source()\nfalse }",
         "Function body does not match the written return type.", 1},
        {"f(flag: bool) { if flag { ret 1 }\nif flag { ret true }\nsource() }",
         "Incompatible return types.", 0},
        {"f() { take(source(), missing) }", "Unknown binding.", 1},
        {"f(value: P) { writable(value) }",
         "A read-only value cannot be passed to an & parameter; take the "
         "parameter it comes from as &.",
         0}};
    char code[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "%s%s\nmain() {}", prefix, cases[i].body);
      const char *required =
          !strstr(cases[i].body, "let value = source()")
              ? "A call that can fail must be handled with ?, !, or a match."
          : strstr(cases[i].body, "let wrong")
              ? "Outcome binding is not handled before leaving its scope."
              : "Outcome binding must be handled with ?, !, a match, or "
                "return.";
      diagnostic_errors(&a, code, required, cases[i].handling, cases[i].message,
                        1);
    }
    snprintf(code, sizeof(code), "%s%s", prefix,
             "both(a: int, b: int) -> :ok | :!other { :ok }\n"
             "f() { both(source(), source()) }\nmain() {}");
    handling_errors(&a, code, 2, NULL, 0);
    handling_errors(&a,
                    "data() -> :left | :right { :left }\n"
                    "take(value: int) {}\nf() { take(data()) }\nmain() {}",
                    0, "Function argument has the wrong type.", 1);
    // Returned recursive calls retain the anchored component's errors.
    valid(&a,
          "a(n: int) { if n == 0 { ret :!bad }\nif n == 1 { ret 1 }\nb(n) }\n"
          "b(n: int) { a(n) }\nmain() {}");
    valid(&a,
          "a(n: int) { if n == 0 { ret :!bad }\nif n == 1 { ret 1 }\nb(n) }\n"
          "b(n: int) { if n == 0 { ret :!bad }\nif n == 1 { ret 1 }\na(n) }\n"
          "main() {}");
    valid(&a, "a() { b() }\nb() { source() }\n"
              "source() -> :ok[int] | :!bad { 1 }\nmain() {}");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
