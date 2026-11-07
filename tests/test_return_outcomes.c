#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "returned.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  size_t count = r.program->count;
  const SemaType **types = arena_calloc(a, count, sizeof(*types));
  for (size_t i = 0; i < count; i++)
    types[i] = r.program->functions[i].symbol.return_type;
  for (unsigned repeat = 0; repeat < 3; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count)
      diagnostics_print(&r.errors, stderr);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_TRUE(!strcmp(text, r.program->module->text));
    for (size_t i = 0; i < count; i++) {
      ASSERT_TRUE(types[i] == r.program->functions[i].symbol.return_type);
      ASSERT_TRUE(types[i] == r.program->functions[i].symbol.outcome_type);
    }
  }
  IrProgram *ir = ir_lower(a, r.program);
  ASSERT_TRUE(ir != NULL);
  if (ir) {
    ir_insert_rc(a, ir);
    size_t *counts = arena_calloc(a, count, sizeof(*counts));
    for (size_t i = 0; i < count; i++)
      counts[i] = ir->functions[i].count;
    ir_insert_rc(a, ir);
    for (size_t i = 0; i < count; i++)
      ASSERT_EQ_INT(counts[i], ir->functions[i].count);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static void rejects(Arena *a, const char *text, const char *message,
                    const char *site) {
  for (unsigned crlf = 0; crlf < 2; crlf++) {
    char source[4096];
    size_t length = 0;
    for (const char *s = text; *s; s++) {
      if (crlf && *s == '\n')
        source[length++] = '\r';
      source[length++] = *s;
    }
    source[length] = 0;
    char location[4096];
    size_t n = 0;
    for (const char *s = site ? site : ""; *s; s++) {
      if (crlf && *s == '\n')
        location[n++] = '\r';
      location[n++] = *s;
    }
    location[n] = 0;
    const char *at = site ? strstr(source, location) : strrchr(source, '}');
    ASSERT_TRUE(at != NULL);
    Result r = check(a, source);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        if (at)
          ASSERT_EQ_INT((size_t)(at - source), d->span.start);
        ASSERT_TRUE(!d->fixes);
        if (!site)
          ASSERT_TRUE(d->notes && !d->notes->next);
        else
          ASSERT_TRUE(!d->notes);
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("TAIL and ret forward complete outcomes without tests or repacking") {
    const char *forms[] = {
        "source(b)",
        "ret source(b)",
        "(source(b))",
        "if b { source(true) } else { source(false) }",
        "b |> { true => source(true)\nfalse => source(false) }",
        "if b { ret source(true) }\nsource(false)",
        "ret if b { source(true) } else { source(false) }",
        "ret b |> { true => source(true)\nfalse => source(false) }",
        "b |> source()"};
    const char *annotations[] = {"-> Result", "", "-> :ok[str] | _"};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); i++)
      for (size_t j = 0; j < sizeof(annotations) / sizeof(*annotations); j++)
        for (unsigned crlf = 0; crlf < 2; crlf++) {
          char lf[2048], text[4096];
          snprintf(lf, sizeof(lf),
                   "Result = :ok[str] | :!bad[str]\n"
                   "source(b: bool) -> Result { if b { ret :!bad[\"owned\"] }\n"
                   "\"success\" }\nforward(b: bool) %s { %s }\nmain() {}",
                   annotations[j], forms[i]);
          size_t length = 0;
          for (const char *s = lf; *s; s++) {
            if (crlf && *s == '\n')
              text[length++] = '\r';
            text[length++] = *s;
          }
          text[length] = 0;
          Result r = valid(&a, text);
          const SemaType *result = r.program->aliases[0].type;
          ASSERT_TRUE(result == r.program->functions[1].symbol.return_type);
          IrProgram *ir = ir_lower(&a, r.program);
          if (!ir)
            continue;
          IrFunction *f = &ir->functions[1];
          size_t calls = 0, returns = 0;
          for (size_t k = 0; k < f->count; k++) {
            IrInstruction *in = &f->instructions[k];
            ASSERT_TRUE(in->op != IR_SYMBOL && in->op != IR_SYMBOL_TAG &&
                        in->op != IR_SYMBOL_PAYLOAD);
            if (in->op == IR_CALL) {
              calls++;
              ASSERT_EQ_INT(0, in->as.call.function);
              ASSERT_TRUE(f->temporary_types[in->result] == result);
            }
            returns += in->op == IR_RETURN;
          }
          ASSERT_TRUE(calls > 0 && returns > 0);
        }
  }
  TEST("all outer kinds and payload layouts retain exact return identity") {
    const char *schemas[] = {":!bad[str, (), int] | :!other", ":ok | :!bad",
                             ":created[int] | :updated[int] | :!bad[int]",
                             ":ok[str] | :none | :!bad",
                             ":ok[Box] | :!bad[str]"};
    const char *values[] = {":!bad[\"owned\", (), 9]", ":ok", ":updated[9]",
                            ":none", "Box { text = \"owned\" }"};
    for (size_t i = 0; i < sizeof(schemas) / sizeof(*schemas); i++) {
      char text[2048];
      snprintf(text, sizeof(text),
               "Box { text: str }\nResult = %s\nsource() -> Result { %s }\n"
               "forward() { source() }\nmain() { ret forward() }",
               schemas[i], values[i]);
      Result r = valid(&a, text);
      const SemaType *result = r.program->aliases[0].type;
      for (size_t j = 0; j < r.program->count; j++)
        ASSERT_TRUE(result == r.program->functions[j].symbol.return_type);
    }
  }
  TEST("recursive results accumulate returned error sets in either order") {
    const char *one = "one(n: int) { if n == 0 { ret :!first[\"owned\"] }\n"
                      "if n == 1 { ret 7 }\ntwo(n - 1) }\n";
    const char *two =
        "two(n: int) { if n == 0 { ret :!second[2] }\none(n - 1) }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%smain() {}", reverse ? two : one,
               reverse ? one : two);
      Result r = valid(&a, text);
      const SemaType *x = r.program->functions[0].symbol.return_type;
      const SemaType *y = r.program->functions[1].symbol.return_type;
      ASSERT_TRUE(x == y && sema_has_errors(x));
      ASSERT_EQ_INT(3, x->symbols->count);
      ASSERT_EQ_INT(SEMA_INT, sema_single_payload(sema_ok(x))->kind);
    }
    valid(&a, "source() -> :ok[int] | :!bad { 7 }\n"
              "forward() { ret source()\n:!unreachable }\nmain() {}");
    rejects(&a, "cycle() { cycle() }\nmain() {}",
            "Cannot infer recursive return type; write a return type.",
            "cycle()");
  }
  TEST("returned errors join plain successes and wider written results") {
    Result r =
        valid(&a, "R = :ok[str] | :!bad\n"
                  "source() -> R { :!bad }\n"
                  "forward(b: bool) { if b { source() } else { \"plain\" } }\n"
                  "wide() -> R | :!other { source() }\nmain() {}");
    ASSERT_TRUE(r.program->functions[1].symbol.return_type ==
                r.program->aliases[0].type);
    ASSERT_EQ_INT(3,
                  r.program->functions[2].symbol.return_type->symbols->count);
    rejects(&a,
            "Box { text: str }\n"
            "source(box: Box) -> :ok[Box] | :!bad { box }\n"
            "forward(box: Box) { source(box) }\n"
            "bad(box: Box) { forward(box)!.text = \"changed\" }\nmain() {}",
            "Cannot mutate through an ordinary parameter; use &.",
            "forward(box)!.text");
  }
  TEST("written contracts reject dropped errors and payload mismatches") {
    const char *prefix = "source() -> :ok[str] | :!bad[str] { \"owned\" }\n";
    const char *bodies[] = {"f() -> str { source() }",
                            "f() -> str { ret source() }",
                            "f() -> :ok[int] | _ { source() }"};
    const char *messages[] = {
        "Function body does not match the written return type.",
        "Return value does not match the written return type.",
        "Function body does not match the written return type."};
    const char *sites[] = {"{ source() }", "ret source()", "{ source() }"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, bodies[i]);
      rejects(&a, text, messages[i], sites[i]);
    }
  }
  TEST("returned outcomes cannot become implicit nested successes") {
    rejects(&a,
            "Inner = :!bad[str]\nOuter = :ok[Inner] | :!outer\n"
            "source() -> Inner { :!bad[\"owned\"] }\n"
            "f() -> Outer { source() }\nmain() {}",
            "Function body does not match the written return type.",
            "{ source() }");
    rejects(&a,
            "Inner = :!bad[str]\nOuter = :ok[Inner] | :!outer\n"
            "source() -> Inner { :!bad[\"owned\"] }\n"
            "f() -> Outer { ret source() }\nmain() {}",
            "Return value does not match the written return type.",
            "ret source()");
    rejects(&a,
            "Inner = :!bad[str]\nOuter = :ok[Inner] | :!outer\n"
            "f() -> Outer { :!bad[\"owned\"] }\nmain() {}",
            "Function body does not match the written return type.",
            "{ :!bad[\"owned\"] }");
    rejects(&a,
            "Inner = :!bad[str]\nOuter = :ok[Inner] | :!outer\n"
            "source() -> Inner { :!bad[\"owned\"] }\n"
            "f() -> Outer { source() |> { other => other } }\nmain() {}",
            "Function body does not match the written return type.",
            "{ source() |>");
    valid(&a,
          "Inner = :!bad[str]\nOuter = :ok[Inner] | :!outer\n"
          "source() -> Inner { :!bad[\"owned\"] }\n"
          "f() -> Outer { :ok[source() |> { other => other }] }\nmain() {}");
  }
  TEST(
      "TAIL never leaks into bindings, arguments, payloads or discarded code") {
    const char *bodies[] = {"source()\n()",
                            "let value = source()\n()",
                            "take(source())",
                            ":outer[source()]",
                            "Box { value = source() }",
                            "if false { source() }",
                            "while false { source() }",
                            "for i in 0..1 { source() }",
                            "source() |> take()",
                            "ret take(source())"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      char text[2048];
      snprintf(text, sizeof(text),
               "Result = :ok[int] | :!bad\nsource() -> Result { 7 }\n"
               "take(value: Result) {}\nBox { value: Result }\n"
               "main() { %s }",
               bodies[i]);
      rejects(
          &a, text,
          i == 1
              ? "Outcome binding is not handled before leaving its scope."
              : "A call that can fail must be handled with ?, !, or a match.",
          i == 1 ? NULL : strstr(bodies[i], "source()"));
    }
  }
  arena_destroy(&a);
  RUN_TESTS();
}
