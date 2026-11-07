#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

static const char *source_prefix =
    "Result = :ok[int] | :!bad[str]\n"
    "source() -> Result { 7 }\n"
    "take(value: Result) {}\nBox { value: Result }\n";
typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "bindings.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  const SemaType **types = arena_calloc(a, r.program->count, sizeof(*types));
  for (size_t i = 0; i < r.program->count; i++)
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
    for (size_t i = 0; i < r.program->count; i++)
      ASSERT_TRUE(types[i] == r.program->functions[i].symbol.return_type);
  }
  IrProgram *ir = ir_lower(a, r.program);
  ASSERT_TRUE(ir != NULL);
  if (ir) {
    ir_insert_rc(a, ir);
    size_t count = ir->functions[ir->entry].count;
    ir_insert_rc(a, ir);
    ASSERT_EQ_INT(count, ir->functions[ir->entry].count);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static void reject(Arena *a, const char *body, const char *message,
                   const char *site) {
  char lf[4096];
  snprintf(lf, sizeof(lf), "%smain() { %s }", source_prefix, body);
  for (unsigned crlf = 0; crlf < 2; crlf++) {
    char text[8192], location[2048];
    size_t length = 0, n = 0;
    for (const char *s = lf; *s; s++) {
      if (crlf && *s == '\n')
        text[length++] = '\r';
      text[length++] = *s;
    }
    text[length] = 0;
    for (const char *s = site ? site : ""; *s; s++) {
      if (crlf && *s == '\n')
        location[n++] = '\r';
      location[n++] = *s;
    }
    location[n] = 0;
    const char *scope = strstr(text, "main()");
    const char *after = strchr(scope, '\n');
    const char *at =
        site ? strstr(after ? after : scope, location) : strrchr(text, '}');
    if (site && !strncmp(site, "// again", strlen("// again")) && at)
      at = strstr(at, "result");
    ASSERT_TRUE(at != NULL);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!memcmp(text, r.program->module->text, length));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        if (at)
          ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_TRUE(!d->fixes);
        ASSERT_TRUE(d->notes && !d->notes->next);
        if (d->notes) {
          ASSERT_EQ_INT(0, strcmp("The outcome binding is declared here.",
                                  d->notes->message));
          const char *decl = strstr(strstr(text, "main()"), "result =");
          ASSERT_TRUE(decl != NULL);
          if (decl)
            ASSERT_EQ_INT((size_t)(decl - text) - 4, d->notes->span.start);
        }
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("let and var retain complete results until one explicit handler") {
    const char *bindings[] = {"let result = source()", "var result = source()",
                              "let result: Result = source()"};
    const char *handlers[] = {"result?",
                              "result!",
                              ("result |> { :ok[n] => print(str(n))\n"
                               ":!bad[text] => print(text) }"),
                              "result |> { other => () }",
                              "ret result",
                              "result"};
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); i++)
      for (size_t j = 0; j < sizeof(handlers) / sizeof(*handlers); j++) {
        char text[2048];
        snprintf(text, sizeof(text), "%smain() { %s\nprint(\"later\")\n%s }",
                 source_prefix, bindings[i], handlers[j]);
        Result r = valid(&a, text);
        SemaExpr *binding = r.program->functions[2].body->as.block.items[0];
        ASSERT_TRUE(binding->as.binding.variable->outcome_binding);
        ASSERT_TRUE(binding->as.binding.variable->type ==
                    r.program->aliases[0].type);
        ASSERT_TRUE(binding->as.binding.value->type ==
                    r.program->aliases[0].type);
        char crlf[4096];
        size_t length = 0;
        for (const char *s = text; *s; s++) {
          if (*s == '\n')
            crlf[length++] = '\r';
          crlf[length++] = *s;
        }
        crlf[length] = 0;
        valid(&a, crlf);
      }
  }
  TEST("pending reads cannot transfer, discard or reassign the obligation") {
    const char *uses[] = {
        "take(result)",   "let copy = result", "Box { value = result }",
        ":outer[result]", "result\n()",        "result |> take()"};
    for (size_t i = 0; i < sizeof(uses) / sizeof(*uses); i++) {
      char body[2048];
      snprintf(body, sizeof(body), "let result = source()\n%s", uses[i]);
      reject(&a, body,
             "Outcome binding must be handled with ?, !, a match, or return.",
             strstr(uses[i], "result"));
    }
    reject(&a, "var result = source()\nresult = :!bad[\"other\"]",
           "Outcome binding cannot be reassigned.", "result = :!bad");
    reject(&a, "let result = source()\nresult = :!bad[\"other\"]",
           "Outcome binding cannot be reassigned.", "result = :!bad");
    reject(&a, "var result = source()\nresult!\nresult = source()",
           "Outcome binding cannot be reassigned.", "result = source() }");
  }
  TEST("handlers consume once and second reads cannot reuse the binding") {
    const char *first[] = {"result?", "result!", "result |> { _ => () }"};
    const char *second[] = {"result?", "result!", "result |> { _ => () }",
                            "ret result", "take(result)"};
    for (size_t i = 0; i < sizeof(first) / sizeof(*first); i++)
      for (size_t j = 0; j < sizeof(second) / sizeof(*second); j++) {
        char body[2048];
        // Search the second occurrence via a unique separating comment.
        snprintf(body, sizeof(body), "let result = source()\n%s\n// again\n%s",
                 first[i], second[j]);
        char site[512];
        snprintf(site, sizeof(site), "// again\n%s", second[j]);
        reject(&a, body, "Outcome binding has already been handled.", site);
      }
  }
  TEST(
      "scope and control-flow guards prevent admission from dropping results") {
    reject(&a, "let result = source()",
           "Outcome binding is not handled before leaving its scope.", NULL);
    reject(&a, "let result = source()\nret",
           "Outcome binding is not handled before leaving its scope.", "ret");
    reject(&a, "let result = source()\nsource()?\nresult!",
           "Outcome binding is not handled before leaving its scope.",
           "source()?\n");
    reject(&a, "let result = source()\nif true { result! }",
           "Outcome binding is handled on some paths but not others.", NULL);
    reject(&a, "let result = source()\nwhile true { result! }",
           "Outcome binding is handled on some paths but not others.",
           "result! }");
    valid(&a, "Result = :ok[int] | :!bad\nsource() -> Result { 7 }\n"
              "main() { let result = source()\nwhile false {}\nresult! }");
    valid(&a,
          "Result = :ok[int] | :!bad\nsource() -> Result { 7 }\n"
          "main() { let result = source()\nif true { let result = source()\n"
          "result! }\nresult! }");
    valid(&a,
          "Result = :ok[int] | :!bad\nsource() -> Result { 7 }\n"
          "main() { while true { let result = source()\nresult!\nbreak } }");
    valid(&a, "Result = :ok[int] | :!bad\nsource() -> Result { 7 }\n"
              "main() { ret 0\nlet result = source() }");
    Result dead = check(&a, "main() { ret 0\nlet result = :!bad\n"
                            "result = :!bad }");
    ASSERT_EQ_INT(1, dead.errors.count);
    if (dead.errors.count == 1)
      ASSERT_EQ_INT(0, strcmp("Cannot reassign an immutable binding.",
                              dead.errors.items[0]->message));
    ASSERT_TRUE(!dead.program->checked && ir_lower(&a, dead.program) == NULL);
  }
  TEST("only actual outer error kinds create local obligations") {
    valid(&a, "Data = :ok | :idle\nMaybe = :ok[str] | :none\n"
              "plain() -> :ok[int] { 7 }\n"
              "main() { let a: Data = :idle\nlet b = a\n"
              "let c: Maybe = :none\nlet d = c\nplain()\n"
              "let nested = :box[:!inner[\"owned\"]]\nlet copy = nested }");
    valid(&a, "source() -> :created[:!inner[str]] | :!outer {"
              ":created[:!inner[\"owned\"]] }\n"
              "main() { let inner = source()!\ninner |> { :!inner[s] => "
              "print(s) } }");
    valid(&a, "main() { let result = :!literal[\"owned\"]\n"
              "result |> { :!literal[s] => print(s) } }");
  }
  TEST("returns and hidden match owners preserve the exact type and "
       "permissions") {
    valid(&a, "Result = :ok[int] | :!bad\n"
              "source(n: int) -> Result { if n == 0 { ret :!bad }\n7 }\n"
              "forward(n: int) { let result = n |> source()\nret result }\n"
              "partial() -> :ok[int] | _ { let result = forward(1)\nresult }\n"
              "main() { let result = partial()\nresult |> { _ => () } }");
    const char *one = "one(n: int) { if n == 0 { ret :!bad }\n"
                      "let result = two(n - 1)\nresult }\n";
    const char *two = "two(n: int) { if n == 0 { ret 7 }\n"
                      "let result = one(n - 1)\nresult }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%smain() {}", reverse ? two : one,
               reverse ? one : two);
      Result recursive = valid(&a, text);
      const SemaType *type = recursive.program->functions[0].symbol.return_type;
      ASSERT_TRUE(type == recursive.program->functions[1].symbol.return_type);
      ASSERT_TRUE(sema_has_errors(type));
      ASSERT_EQ_INT(2, type->symbols->count);
    }
    Result bad = check(
        &a, "Box { text: str }\nsource(box: Box) -> :ok[Box] | :!bad { box }\n"
            "bad(box: Box) { let result = source(box)\n"
            "let alias = result!\nalias.text = \"wrong\" }\nmain() {}");
    ASSERT_EQ_INT(1, bad.errors.count);
    if (bad.errors.count == 1)
      ASSERT_EQ_INT(
          0, strcmp("Cannot mutate through an ordinary parameter; use &.",
                    bad.errors.items[0]->message));
    ASSERT_TRUE(!bad.program->checked && ir_lower(&a, bad.program) == NULL);
    valid(&a, "Box { text: str }\n"
              "source(box: Box) -> :ok[Box] | :!bad { box }\n"
              "forward(box: Box) { let result = source(box)\nresult }\n"
              "main() { var box = Box { text = \"owned\" }\n"
              "let result = forward(box)\nlet alias = result!\n"
              "alias.text = \"shared\"\nprint(box.text) }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
