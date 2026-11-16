#include "compile_support.h"

static void reject(Arena *a, const char *source, const char *message,
                   const char *site) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = lines(a, source, crlf);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        if (!r.program->resolved)
          r = check(a, text);
        else {
          diagnostics_init(&r.errors, a);
          sema_check(r.program, &r.errors);
        }
      }
      if (r.errors.count != 1) {
        fprintf(stderr, "Unexpected errors:\n%s\n", text);
        diagnostics_print(&r.errors, stderr);
      }
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        const char *at = strstr(text, lines(a, site, crlf));
        ASSERT_TRUE(at != NULL);
        if (at)
          ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_TRUE(!d->fixes);
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  const char *prefix = "Maybe = :ok[int] | :none | :!bad\nf() -> Maybe { 7 }\n";
  TEST("let else syntax retains blocks and resolves a checked two-arm match") {
    for (int crlf = 0; crlf < 2; crlf++) {
      char source[1024];
      snprintf(source, sizeof(source),
               "%smain() { let n: int = f()\nelse { ret }\nprint(str(n)) }",
               prefix);
      Result r = valid(&a, lines(&a, source, crlf));
      Decl *decl = r.ast->items[2]->as.function.body->as.block.items[0].as.decl;
      ASSERT_TRUE(decl->as.binding.else_body != NULL);
      SemaExpr *bind = r.program->functions[1].body->as.block.items[0];
      SemaExpr *match = bind->as.binding.value;
      ASSERT_EQ_INT(SEMA_EXPR_MATCH, match->kind);
      ASSERT_EQ_INT(2, match->as.match.count);
      ASSERT_TRUE(match->as.match.remaining != NULL);
      ASSERT_EQ_INT(1, match->as.match.arms[0].case_count);
      ASSERT_EQ_INT(2, match->as.match.arms[1].case_count);
      ASSERT_EQ_INT(SEMA_INT, bind->as.binding.variable->type->kind);
      ASSERT_EQ_INT(PAT_SYMBOL, match->as.match.arms[0].kind);
      ASSERT_TRUE(match->as.match.arms[1].body->returns);
      FILE *out = tmpfile();
      ASSERT_TRUE(out != NULL);
      if (out) {
        ast_print(out, r.ast);
        rewind(out);
        char printed[2048];
        size_t count = fread(printed, 1, sizeof(printed) - 1, out);
        printed[count] = 0;
        ASSERT_TRUE(strstr(printed, "(else (block (ret)))") != NULL);
        fclose(out);
      }
    }
    reject(&a, "main() { var n = :none else { ret } }",
           "Only let bindings can have an else block.", "else");
    reject(&a, "main() { const n = :none else { ret } }",
           "Only let bindings can have an else block.", "else");
    reject(&a, "main() { let n = :none else ret }",
           "Expected '{' to start a block.", "ret");
  }
  TEST("absence and errors are removed and a single remaining tag unwraps") {
    const char *sources[] = {
        ("f() -> :ok[str] | :none { \"owned\" }\nmain() { let text = f() else "
         "{ "
         "ret }\nprint(text) }"),
        ("f() -> :created[int] | :none | :!bad { :created[7] }\nmain() { let n "
         "= f() else { ret }\nprint(str(n)) }"),
        ("f() -> :ready | :none { :ready }\nmain() { let empty: () = f() else "
         "{ "
         "ret } }"),
        ("f() -> :ok[()] | :none { :ok[()] }\nmain() { let empty: () = f() "
         "else "
         "{ ret } }"),
        ("f() -> :ok[int] | :!bad { 7 }\nmain() { let n = f() else { ret "
         "}\nprint(str(n)) }")};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++)
      accepts(&a, sources[i]);
    reject(&a, "main() { let n = 7 else { ret } }",
           "A let-else value must contain :none or an error tag.", "7 else");
    reject(&a, "f() -> :ok[int] { 7 }\nmain() { let n = f() else { ret } }",
           "A let-else value must contain :none or an error tag.", "f() else");
    reject(&a, "f() -> :a | :b { :a }\nmain() { let n = f() else { ret } }",
           "A let-else value must contain :none or an error tag.", "f() else");
    reject(&a,
           "f() -> :pair[int, int] | :none { :pair[1, 2] }\nmain() { let n = "
           "f() else { ret } }",
           "Extracting several success payloads is not implemented yet.",
           "f() else");
  }
  TEST("several surviving tags remain a narrower union rather than a payload") {
    const char *source =
        "R = :created[str] | :updated[str] | :none | :!bad\n"
        "f() -> R { :created[\"owned\"] }\nmain() { let r = f() else { ret }\n"
        "r |> { :created[s] => print(s)\n:updated[s] => print(s) } }";
    Result r = valid(&a, source);
    SemaExpr *bind = r.program->functions[1].body->as.block.items[0];
    const SemaType *type = bind->as.binding.variable->type;
    ASSERT_EQ_INT(SEMA_SYM, type->kind);
    ASSERT_EQ_INT(2, type->symbols->count);
    ASSERT_TRUE(!sema_has_errors(type) && !sema_none(type));
    ASSERT_EQ_INT(PAT_SET, bind->as.binding.value->as.match.arms[0].kind);
    accepts(&a, source);
    reject(&a,
           "f() -> :ok[int] | :none { 7 }\nmain() { let n: str = f() else { "
           "ret } }",
           "Binding value does not match its annotation.", "f() else");
  }
  TEST("else blocks must diverge on every path but need not return a value") {
    char source[2048];
    const char *blocks[] = {"{}", "{ print(\"fallthrough\") }",
                            "{ if true { ret } }"};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(*blocks); i++) {
      snprintf(source, sizeof(source),
               "%smain() { let n = f() else %s\nprint(str(n)) }", prefix,
               blocks[i]);
      reject(&a, source,
             "The else block of a let binding must not fall through.",
             blocks[i]);
    }
    snprintf(source, sizeof(source),
             "%smain() { let n = f() else { if true { ret } else { ret } "
             "}\nprint(str(n)) }",
             prefix);
    accepts(&a, source);
    snprintf(source, sizeof(source),
             "%smain() { let n = f() else { while true {} }\nprint(str(n)) }",
             prefix);
    accepts(&a, source);
    accepts(&a, "main() { let value = :none else { ret }\nprint(\"dead\") }");
    Result terminal = valid(&a, "main() { let value = :!bad else { ret } }");
    SemaExpr *bind = terminal.program->functions[0].body->as.block.items[0];
    ASSERT_TRUE(bind->returns && bind->as.binding.value->returns);
    ASSERT_EQ_INT(SEMA_NEVER, bind->as.binding.variable->type->kind);
  }
  TEST("checked matching preserves return contracts break continue and "
       "assignment") {
    accepts(&a, "f() -> :ok[int] | :!bad { :!bad }\n"
                "g() -> :ok[int] | :!replacement { let n = f() else { ret "
                ":!replacement }\nn }\nmain() {}");
    accepts(&a, "f() -> :ok[int] | :none { :none }\nmain() { for i in 0..3 {\n"
                "let n = f() else { continue }\nprint(str(n)) } }");
    accepts(
        &a,
        "f() -> :ok[int] | :none { :none }\nmain() { let n: int\nwhile true {\n"
        "let x = f() else { n = 1\nbreak }\nn = x\nbreak }\nprint(str(n)) }");
    accepts(&a, "f() -> :ok[int] | :none | :!bad { 1 }\nmain() { let n = f()? "
                "else { ret }\nprint(str(n)) }");
    reject(
        &a,
        "f() -> :ok[int] | :none { 1 }\nmain() { let x = f() else { break } }",
        "break and continue are only allowed inside loops.", "break");
    reject(&a,
           "f() -> :ok[int] | :none { 1 }\ng() -> int { let x = f() else { ret "
           "\"wrong\" }\nx }\nmain() {}",
           "Return value does not match the written return type.",
           "ret \"wrong\"");
  }
  TEST("initializers and else bodies see outer bindings not the new binding") {
    accepts(
        &a,
        "f() -> :ok[int] | :none { :none }\nmain() { let n = 3\n"
        "if true { let n = f() else { print(str(n))\nret }\nprint(str(n)) } }");
    reject(&a, "main() { let n = :none else { print(str(n))\nret } }",
           "Unknown binding.", "n))");
    reject(&a,
           "f() -> :ok[int] | :!bad { 7 }\nmain() -> () { let r = f()\n"
           "let n = r else { ret }\nr! }",
           "Outcome binding has already been handled.", "r! }");
    accepts(&a, "Box { n: int\nget() { let x = :none else { ret self.n }\n0 } "
                "}\nmain() {}");
  }
  TEST("owned and borrowed projections retain permissions and obligations") {
    const char *source = "Box { n: int }\nR = :ok[Box] | :none | :!bad\n"
                         "f(box: Box) -> R { box }\n"
                         "g(box: &Box) { let alias = f(box) else { ret "
                         "}\nalias.n = 2 }\nmain() {}";
    accepts(&a, source);
    reject(&a,
           "Box { n: int }\nR = :ok[Box] | :none | :!bad\nf(box: Box) -> R { "
           "box }\n"
           "g(box: Box) { let alias = f(box) else { ret }\nalias.n = 2 "
           "}\nmain() {}",
           "Cannot mutate through an ordinary parameter; use &.", "alias.n");
    reject(&a,
           "Box { n: int }\nHolder { box: Box }\nf(box: Box) -> :ok[Box] | "
           ":none { box }\n"
           "g(holder: &Holder, box: Box) { let alias = f(box) else { ret }\n"
           "holder.box = alias }\nmain() {}",
           "A read-only value cannot be stored in a field; take the parameter "
           "it comes from as &.",
           "alias }");
    reject(
        &a,
        "Box { n: int }\nf(box: Box) -> :ok[Box] | :none { box }\n"
        "touch(box: &Box) {}\ng(box: Box) { let alias = f(box) else { ret }\n"
        "touch(alias) }\nmain() {}",
        "A read-only value cannot be passed to an & parameter; take the "
        "parameter it comes from as &.",
        "alias) }");
    accepts(&a, "f() -> :item[:none] | :none { :item[:none] }\n"
                "main() { let value = f() else { ret }\n"
                "value |> { :none => print(\"nested absence\") } }");
    accepts(&a, "Inner = :!inner[str]\nf() -> :item[Inner] | :none { "
                ":item[:!inner[\"owned\"]] }\n"
                "main() { let inner = f() else { ret }\ninner |> { :!inner[s] "
                "=> print(s) } }");
    reject(&a,
           "Inner = :!inner[str]\nf() -> :item[Inner] | :none { "
           ":item[:!inner[\"owned\"]] }\n"
           "main() { let inner = f() else { ret }\n} // scope",
           "Outcome binding is not handled before leaving its scope.",
           "} // scope");
  }
  TEST(
      "nested syntax and recursive inference recheck without source mutation") {
    accepts(
        &a,
        "main() { let a = :none else { let b = :none else { ret }\nret } }");
    const char *one = "one(n: int) { if n == 0 { ret :none }\nlet x = two(n - "
                      "1) else { ret :none }\n:ok[x] }\n";
    const char *two = "two(n: int) { if n == 0 { ret :ok[7] }\none(n - 1) }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char source[2048];
      snprintf(source, sizeof(source), "%s%smain() {}", reverse ? two : one,
               reverse ? one : two);
      Result r = valid(&a, source);
      const SemaType *type = r.program->functions[0].symbol.return_type;
      ASSERT_TRUE(type == r.program->functions[1].symbol.return_type);
      ASSERT_EQ_INT(SEMA_SYM, type->kind);
      if (type->kind == SEMA_SYM) {
        ASSERT_EQ_INT(2, type->symbols->count);
        ASSERT_TRUE(sema_none(type) && sema_ok(type));
        ASSERT_EQ_INT(SEMA_INT, sema_single_payload(sema_ok(type))->kind);
      }
    }
    reject(&a, "f() { let n = f() else { ret 0 }\nn }\nmain() {}",
           "Cannot infer recursive return type; write a return type.", "f()");
    char deep[8192];
    size_t n = (size_t)snprintf(deep, sizeof(deep), "main() {");
    for (unsigned i = 0; i < 140; i++)
      n += (size_t)snprintf(deep + n, sizeof(deep) - n,
                            "let item%u = :none else {", i);
    n += (size_t)snprintf(deep + n, sizeof(deep) - n, "ret\n");
    for (unsigned i = 0; i < 140; i++)
      n += (size_t)snprintf(deep + n, sizeof(deep) - n, "}\n");
    snprintf(deep + n, sizeof(deep) - n, "}\n");
    Result depth = check(&a, deep);
    ASSERT_TRUE(depth.errors.count > 0 && !depth.program->checked &&
                ir_lower(&a, depth.program) == NULL);
    int found = 0;
    for (size_t i = 0; i < depth.errors.count; i++)
      found |= !strcmp("Syntax nesting limit exceeded.",
                       depth.errors.items[i]->message);
    ASSERT_TRUE(found);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
