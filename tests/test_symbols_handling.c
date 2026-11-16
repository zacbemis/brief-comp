#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("error-only calls are handled immediately or returned unchanged") {
    const char *prefix =
        "Error = :!problem[str]\n"
        "fail() -> Error { :!problem[\"owned\"] }\n"
        "take(value: Error) {}\n"
        "Box { value: Error }\n"
        "Source { n: int\nfail() -> Error { :!problem[str(self.n)] }\n"
        "create() -> Error { :!problem[\"owned\"] } }\n"
        "main() { let source = Source { n = 7 }\n";
    const char *bodies[] = {"fail()",
                            "let result = fail()",
                            "take(fail())",
                            "Box { value = fail() }",
                            ":box[fail()]",
                            "ret fail()",
                            "if false { fail() }",
                            "fail() |> take()",
                            "source.fail()",
                            "Source.create()",
                            "source |> fail()"};
    const char *required =
        "A call that can fail must be handled with ?, !, or a match.";
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[2048], source[4096];
        snprintf(lf, sizeof(lf), "%s%s\n}", prefix, bodies[i]);
        size_t length = 0;
        for (const char *s = lf; *s; s++) {
          if (crlf && *s == '\n')
            source[length++] = '\r';
          source[length++] = *s;
        }
        source[length] = 0;
        int returned = i == 0 || i == 5 || i >= 8;
        Result r = returned ? valid(&a, source) : check(&a, source);
        const char *site = strstr(strstr(source, "main() {"), bodies[i]);
        ASSERT_TRUE(site != NULL);
        for (unsigned repeat = 0; repeat < 3; repeat++) {
          if (repeat) {
            diagnostics_init(&r.errors, &a);
            sema_check(r.program, &r.errors);
          }
          if (r.errors.count != (size_t)!returned)
            diagnostics_print(&r.errors, stderr);
          ASSERT_EQ_INT(!returned, r.errors.count);
          ASSERT_TRUE(r.program->resolved);
          ASSERT_EQ_INT(returned, r.program->checked);
          ASSERT_EQ_INT(returned, ir_lower(&a, r.program) != NULL);
          if (returned)
            ASSERT_TRUE(sema_only_errors(r.program->entry->return_type));
          ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
          if (r.errors.count) {
            Diagnostic *d = r.errors.items[0];
            const char *message =
                i == 1
                    ? "Outcome binding is not handled before leaving its scope."
                    : required;
            ASSERT_EQ_INT(0, strcmp(message, d->message));
            ASSERT_TRUE(d->span.start >= (size_t)(site - source));
            if (i == 1) {
              ASSERT_EQ_INT(length - 1, d->span.start);
              ASSERT_TRUE(d->notes && !d->notes->next);
            } else {
              ASSERT_TRUE(d->span.end <=
                          (size_t)(site - source) + strlen(bodies[i]));
              ASSERT_TRUE(!d->notes);
            }
            ASSERT_TRUE(!d->fixes);
          }
        }
      }
    valid(&a, "f() -> :!problem[str] { :!problem[\"owned\"] }\n"
              "main() { f() |> { :!problem[text] => print(text) } }");
    Result boundary =
        valid(&a, "f() -> :!problem { :!problem }\nmain() { f()? }");
    ASSERT_TRUE(
        sema_only_errors(boundary.program->functions[1].symbol.return_type));
    ASSERT_TRUE(!sema_ok(boundary.program->functions[1].symbol.outcome_type));
    boundary =
        valid(&a, "f() -> :ok[:!problem] { :ok[:!problem] }\nmain() { f() }");
    ASSERT_TRUE(
        sema_only_errors(boundary.program->functions[1].symbol.return_type));
  }
  TEST("kind-based call diagnostics survive SCC inference and recovery") {
    const char *source =
        "f(n: int) { if n == 0 { ret :!problem[\"owned\"] }\n"
        "g(n - 1) |> { other => other } }\n"
        "g(n: int) { f(n) |> { other => other } }\nmain() { f(1) }";
    Result r = check(&a, source);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, &a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(0, r.errors.count);
      ASSERT_TRUE(r.program->resolved && r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) != NULL);
      for (size_t i = 0; i < r.program->count; i++) {
        ASSERT_TRUE(!r.program->functions[i].inference_conflict);
        ASSERT_TRUE(
            sema_only_errors(r.program->functions[i].symbol.return_type));
      }
    }
    valid(&a, "Health = :ok | :degraded\n"
              "f(b: bool) -> Health { if b { ret :ok }\ng(true) }\n"
              "g(b: bool) -> Health { f(b) }\nmain() { f(false) }");
    r = check(&a, "fail() -> :!problem { :!problem }\n"
                  "main() { missing()\nfail() }");
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count)
      ASSERT_EQ_INT(0, strcmp("Unknown function.", r.errors.items[0]->message));
    ASSERT_TRUE(!r.program->checked && ir_lower(&a, r.program) == NULL);
  }
  TEST("neutral-only results including ok and none are ordinary call values") {
    valid(&a, "Health = :ok | :degraded[str] | :down\n"
              "Maybe = :ok[str] | :none\nNested = :box[:!problem[str]]\n"
              "Box { value: Nested }\n"
              "health() -> Health { :degraded[\"owned\"] }\n"
              "find() -> Maybe { :none }\n"
              "nested() -> Nested { :box[:!problem[\"owned\"]] }\n"
              "object() -> Box { Box { value = nested() } }\n"
              "take(value: Health) {}\n"
              "main() { health()\nlet h = health()\ntake(h)\ntake(health())\n"
              "find()\nlet missing = find()\nnested()\nobject() }");
  }
  TEST("no-error postfixes reject without inventing propagation or a fix") {
    const char *required =
        "? and ! cannot handle this value: it has no error tags.";
    struct {
      const char *setup, *operand;
    } cases[] = {
        {"", "1"},
        {"", "true"},
        {"", "()"},
        {"", "(\"owned\" + \" text\")"},
        {"", ":adult"},
        {"", ":none"},
        {"", ":ok"},
        {"", ":ok[7]"},
        {"", ":box[:!hidden]"},
        {"Box { value: :!hidden }\n", "(Box { value = :!hidden })"},
        {"f() -> int { 7 }\n", "f()"},
        {"f() -> :ok[str] { :ok[\"owned\"] }\n", "f()"},
        {"f() -> :ok { :ok }\n", "f()"},
        {"Data = :adult | :senior\nf() -> Data { :adult }\n", "f()"},
        {"Data = :ok | :degraded\nf() -> Data { :degraded }\n", "f()"},
        {"Maybe = :ok[str] | :none\nf() -> Maybe { :none }\n", "f()"},
        {"Maybe = :ok[str] | :none\nf() -> Maybe { \"owned\" }\n", "f()"},
        {"f() -> :box[:!hidden] { :box[:!hidden] }\n", "f()"},
        {"", "lng(1)"},
        {"", "int('a')"},
        {"", "(1 |> flt())"},
        {"f() -> :ok[int] | :!bad { 7 }\n", "f()!"},
        {"f() -> :adult | :senior | :!bad { :adult }\n", "f()!"},
        {"f() -> :ok[str] | :none | :!bad { :none }\n", "f()!"},
        {"f() -> :ok[str] | :none { :none }\n"
         "g(value: :ok[str] | :none) { value }\n",
         "(f() |> g())"},
        {"f() { ret 0\n", "1"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      for (unsigned panic = 0; panic < 2; panic++)
        for (unsigned crlf = 0; crlf < 2; crlf++) {
          char lf[1024], source[2048];
          int unreachable = i == sizeof(cases) / sizeof(*cases) - 1;
          snprintf(lf, sizeof(lf), "%s%s%s%c\nprint(\"after\") }%s",
                   cases[i].setup, unreachable ? "" : "main() { ",
                   cases[i].operand, panic ? '!' : '?',
                   unreachable ? "\nmain() {}" : "");
          size_t length = 0;
          for (const char *s = lf; *s; s++) {
            if (crlf && *s == '\n')
              source[length++] = '\r';
            source[length++] = *s;
          }
          source[length] = 0;
          Result r = check(&a, source);
          SemaExpr *postfix =
              r.program->functions[unreachable ? 0 : r.program->count - 1]
                  .body->as.block.items[unreachable ? 1 : 0];
          for (unsigned repeat = 0; repeat < 3; repeat++) {
            if (repeat) {
              diagnostics_init(&r.errors, &a);
              sema_check(r.program, &r.errors);
            }
            if (r.errors.count != 1)
              diagnostics_print(&r.errors, stderr);
            ASSERT_EQ_INT(1, r.errors.count);
            ASSERT_TRUE(r.program->resolved && !r.program->checked);
            ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
            ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
            ASSERT_TRUE(!postfix->failures);
            ASSERT_EQ_INT(SEMA_ERROR, postfix->type->kind);
            if (r.errors.count == 1) {
              Diagnostic *d = r.errors.items[0];
              ASSERT_EQ_INT(0, strcmp(required, d->message));
              ASSERT_EQ_INT(postfix->span.start, d->span.start);
              ASSERT_EQ_INT(postfix->span.end, d->span.end);
              ASSERT_TRUE(!d->fixes && !d->notes);
            }
          }
        }
    // A prior error/unknown/never operand must not acquire a second primary.
    Result r = invalid(&a, "main() { missing()! }", "Unknown function");
    ASSERT_EQ_INT(1, r.errors.count);
    r = invalid(&a, "main() { missing? }", "Unknown binding");
    ASSERT_EQ_INT(1, r.errors.count);
    r = invalid(&a, "main() { 1?! }", required);
    ASSERT_EQ_INT(1, r.errors.count);
    r = invalid(&a, "main() { 1?\nmissing() }", required);
    ASSERT_EQ_INT(2, r.errors.count);
    r = invalid(
        &a,
        "Bad = :overflow\nf(n: lng) { if n == 0 { ret 1 }\nint(g(n)?)? }\n"
        "g(n: lng) { if n == 0 { ret lng(1) }\nlng(f(n)?) }\nmain() {}",
        required);
    ASSERT_EQ_INT(2, r.errors.count);
    for (size_t i = 0; i < r.errors.count; i++) {
      ASSERT_EQ_INT(0, strcmp(required, r.errors.items[i]->message));
      ASSERT_TRUE(!r.errors.items[i]->fixes && !r.errors.items[i]->notes);
    }
    valid(&a, "f() -> :!bad { :!bad }\nmain() { f()!? }");
  }
  TEST("outcome handling extends data symbols without changing their rules") {
    valid(&a, "main() { :ok[1] }");
    // Retain neutral data: the legacy no-error helper still uses :ok.
    Result legacy = valid(&a, "f() -> :ok | :bad { :bad }\nmain() {}");
    ASSERT_EQ_INT(
        SEMA_TAG_NEUTRAL,
        sema_tag(legacy.program, legacy.program->module, "bad")->kind);
    ASSERT_TRUE(sema_failures(
        legacy.program, legacy.program->functions[0].symbol.outcome_type));
    // The legacy no-error helper no longer classifies fallible calls.
    ASSERT_TRUE(
        !sema_has_errors(legacy.program->functions[0].symbol.return_type));
    invalid(&a, "f() -> :a | _ { :a }\nmain() {}", "must declare :ok");
    valid(&a, "main() { :a }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
