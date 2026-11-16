#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST(
      "invalid underscore names suppress dependent parser errors and recover") {
    const char *cases[] = {"let _binding = 1",
                           "let value: _Type = 1",
                           "item._field",
                           ":_tag",
                           "for _item in 0..2 {}",
                           "f(_argument)",
                           "_call()",
                           "let value = _object"};
    const char *message =
        "Names must start with a letter; '_' alone is a wildcard.";
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *nl = ending ? "\r\n" : "\n";
        char text[512];
        snprintf(text, sizeof(text),
                 "main() {%s%s%slet kept = 1%s}%safter() {}%s", nl, cases[i],
                 nl, nl, nl, nl);
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count) {
          const Diagnostic *d = v->diagnostics.items[0];
          size_t start = (size_t)(strchr(text, '_') - text), end = start;
          while (text[end] == '_' || (text[end] >= 'a' && text[end] <= 'z') ||
                 (text[end] >= 'A' && text[end] <= 'Z'))
            end++;
          ASSERT_EQ_STRN(message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(end, d->span.end);
          ASSERT_TRUE(d->fixes == NULL);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          const Expr *body = v->program->items[0]->as.function.body;
          ASSERT_TRUE(body->as.block.count > 0);
          if (body->as.block.count) {
            BlockItem last = body->as.block.items[body->as.block.count - 1];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, last.kind);
            if (last.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", last.as.decl->name,
                             strlen(last.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
      }
    }
  }
  TEST("source-exponent lexer errors suppress parser cascades and recover") {
    const char *message =
        "Exponent notation is not supported in source number literals.";
    struct {
      const char *code, *lexeme;
    } cases[] = {{"let value = 1e2", "1e2"},
                 {"let value = 0.5E-2", "0.5E-2"},
                 {"let value = -1e-2", "1e-2"},
                 {"f(1E+2)", "1E+2"},
                 {"ret -1.25e2", "1.25e2"},
                 {"if 1e2 > 0 {}", "1e2"},
                 {"while 1E2 > 0 {}", "1E2"},
                 {"for i in 0..1e2 {}", "1e2"},
                 {"P { n = 1E2 }", "1E2"},
                 {"1 |> {\n1e2 => 0\n_ => 1\n}", "1e2"},
                 {"1 |> {\n-1E2 => 0\n_ => 1\n}", "1E2"},
                 {"let value: Arr[1e2] = ()", "1e2"},
                 {"let value = 1e", "1e"},
                 {"let value = 1E+", "1E+"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[512], crlf[1024];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n", cases[i].code);
      size_t n = 0;
      for (size_t j = 0; text[j]; j++) {
        if (text[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = text[j];
      }
      crlf[n] = '\0';
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *source = ending ? crlf : text;
        Parsed *v = parse(&arena, source);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count) {
          size_t start = (size_t)(strstr(source, cases[i].lexeme) - source);
          ASSERT_EQ_STRN(message, v->diagnostics.items[0]->message,
                         strlen(v->diagnostics.items[0]->message));
          ASSERT_EQ_INT(start, v->diagnostics.items[0]->span.start);
          ASSERT_EQ_INT(start + strlen(cases[i].lexeme),
                        v->diagnostics.items[0]->span.end);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          Expr *body = v->program->items[0]->as.function.body;
          ASSERT_TRUE(body->as.block.count > 0);
          if (body->as.block.count) {
            BlockItem last = body->as.block.items[body->as.block.count - 1];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, last.kind);
            if (last.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", last.as.decl->name,
                             strlen(last.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
      }
    }
    Parsed *v = parse(&arena, "main() { let value = 1e2 + 2E3\n"
                              "let = 1\nlet kept = 1\n}\nafter() {}\n");
    ASSERT_EQ_INT(3,
                  v->diagnostics.count); // Two lexemes and one binding error.
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strcmp(message, v->diagnostics.items[i]->message) == 0 ||
                  strcmp("Expected a binding name.",
                         v->diagnostics.items[i]->message) == 0);
    expression_dump(&arena, "dbl(\"1E+2\")!",
                    "(panic (call (name dbl) (str \"1E+2\")))");
    expression_dump(&arena, "0.25 + e2", "(+ (float 0.25) (name e2))");
    expression_dump(&arena, "1 // gap\n", "(int 1)");
  }
  TEST("semicolon syntax, malformed types and incomplete expressions are "
       "rejected") {
    invalid(&arena, "main() { x; y }", "newline");
    invalid(&arena, "main() { x;; }", "newline");
    invalid(&arena, "main() { let x = }", "expression");
    invalid(&arena, "f(x: int { x }", "')'");
    invalid(&arena, "E = :ok[int] |\n", "Expected a type.");
    program_dump(&arena, "f() -> _ {}",
                 "(program (fn f (params) _ (block)))\n");
    invalid(&arena, "print(\"top-level statement\")", "inside a function");
  }
  TEST("top-level statement hints are exact and preserve later declarations") {
    const char *statements[] = {"let value = 1",
                                "var value = 1",
                                "print(\"Hello\")",
                                "tick()",
                                "print(value)",
                                "User.new(\"Bob\", 20)",
                                "str(1)",
                                "print(User { name = \"Bob\", age = 20 })",
                                "print(nested(1, (2)))",
                                "value = 1",
                                "value += 1",
                                "value++",
                                "value.(0) = 1",
                                "value.(0)",
                                "call(value.(0))",
                                "call(value.(other.(0)))",
                                "if true { print(\"if\") }",
                                "while false { tick() }",
                                "for i in 0..2 { print(str(i)) }",
                                "ret 1",
                                "break",
                                "continue",
                                "42",
                                "0.5",
                                "true",
                                "'x'",
                                "\"text\"",
                                ":ok[1]",
                                "()",
                                "-1",
                                "not true",
                                "~1",
                                "value",
                                "tick()?",
                                "tick()!",
                                "tick().field",
                                "print(\n\"multiline\"\n)"};
    for (size_t i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
      char text[512];
      snprintf(text, sizeof(text), "%s\nmain() {}\n", statements[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        ASSERT_EQ_STRN("Statements are only allowed inside a function.",
                       v->diagnostics.items[0]->message,
                       strlen(v->diagnostics.items[0]->message));
        ASSERT_EQ_INT(0, v->diagnostics.items[0]->span.start);
        ASSERT_EQ_INT(
            source_position(v->source, 0).line,
            source_position(v->source, v->diagnostics.items[0]->span.start)
                .line);
      }
      ASSERT_EQ_INT(1, v->program->count);
      if (v->program->count)
        ASSERT_EQ_STRN("main", v->program->items[0]->name,
                       strlen(v->program->items[0]->name));
    }
    Parsed *v =
        parse(&arena, "\r\n  print(\r\n\"Hello\"\r\n)\r\nmain() {}\r\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_INT(4, v->diagnostics.items[0]->span.start);
    SourcePosition pos =
        source_position(v->source, v->diagnostics.items[0]->span.start);
    ASSERT_EQ_INT(2, pos.line);
    ASSERT_EQ_INT(3, pos.column);
    ASSERT_EQ_INT(1, v->program->count);
  }
  TEST("declaration evidence keeps specific header errors, not statement "
       "hints") {
    invalid(&arena, "f(x) {}", "Expected ':' before type.");
    invalid(&arena, "f(1) {}", "Expected a parameter name.");
    invalid(&arena, "f(x: int)", "Expected '{' to start a block.");
    invalid(&arena, "f() -> int", "Expected '{' to start a block.");
    invalid(&arena, "f()\n{}", "Expected '{' to start a block.");
    invalid(&arena, "f()\n-> int {}", "Expected '{' to start a block.");
    invalid(&arena, "pub f(1)", "Expected a parameter name.");
    invalid(&arena, "f(", "Expected a parameter name.");
    invalid(&arena, "User { name: }", "Expected a type.");
    program_dump(&arena, "import User", "(program (import User))\n");
    invalid(&arena, "int() {}",
            "Expected a declaration (function, struct, or named type).");
    invalid(&arena, "str(x: int) {}",
            "Expected a declaration (function, struct, or named type).");
    Parsed *v = parse(&arena, "broken(@)\nmain() {}\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_STRN("Unexpected character.", v->diagnostics.items[0]->message,
                   strlen(v->diagnostics.items[0]->message));
    ASSERT_EQ_INT(1, v->program->count);
    v = parse(&arena, "pub f(x: &Self) -> :ok[int] { :ok[1] }\n"
                      "main() {}\nconst value = 1\n");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(3, v->program->count);
  }
  TEST("statement lookahead leaves filtered tokens unchanged and is "
       "repeatable") {
    Parsed *v =
        parse(&arena, "tick(make[Vec[Vec[int]]](User { name: \"Bob\" }))\n"
                      "main() {}\n");
    TokenArray filtered = newline_filter(&arena, v->raw);
    Token *saved = arena_alloc(&arena, filtered.count * sizeof(*saved));
    memcpy(saved, filtered.items, filtered.count * sizeof(*saved));
    for (unsigned i = 0; i < 2; i++) {
      Diagnostics d;
      diagnostics_init(&d, &arena);
      Program *program = parse_program(&arena, &v->names, filtered, &d);
      ASSERT_EQ_INT(1, d.count);
      ASSERT_EQ_INT(1, program->count);
      ASSERT_TRUE(
          memcmp(saved, filtered.items, filtered.count * sizeof(*saved)) == 0);
    }
  }
  TEST("recovery retains later statements, arms, fields, methods and "
       "declarations") {
    Parsed *v = parse(
        &arena, "main() {\nlet = 1\nlet valid = 2\nbad(1}\nnext() { 3 }\n");
    ASSERT_TRUE(v->diagnostics.count >= 2);
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
    ASSERT_EQ_STRN("next", v->program->items[1]->name,
                   strlen(v->program->items[1]->name));
    v = parse(&arena, "User {\n: bad\ngood: int\n\n\nbad(x) {}\ngood() {}\n}\n"
                      "main() {\nx |> {\n:ok[x] x\n:none => 0\n}\n}\n");
    ASSERT_TRUE(v->diagnostics.count >= 3);
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.structure.count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.structure.function_count);
    Expr *pipe =
        v->program->items[1]->as.function.body->as.block.items[0].as.expr;
    ASSERT_EQ_INT(1, pipe->as.pipe.stage->as.match.count);
  }
  TEST("lexer errors do not hide syntax errors or create duplicate "
       "diagnostics") {
    Parsed *v = parse(&arena, "main() {\nlet a = @\nlet = 1\nlet good = 2\n}\n"
                              "next(x) {}\nlast() { 4 }\n");
    ASSERT_EQ_INT(3,
                  v->diagnostics.count); // One lexer error, two syntax errors.
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
    ASSERT_EQ_STRN("last", v->program->items[1]->name,
                   strlen(v->program->items[1]->name));
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strstr(v->diagnostics.items[i]->message,
                         "Expected an expression") == NULL);
    v = parse(&arena, "@\nbad(x) {}\ngood() {}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->count);
    v = parse(&arena, "main() { f(@)\nlet a = @\nlet good = 1\n}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
  }
  TEST("large lists grow and deep syntax fails with a diagnostic, not stack "
       "overflow") {
    char text[20000];
    size_t n = (size_t)sprintf(text, "main() {\n");
    for (size_t i = 0; i < 300; i++)
      n += (size_t)sprintf(text + n, "let x%zu = %zu\n", i, i);
    strcpy(text + n, "}\n");
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(300, v->program->items[0]->as.function.body->as.block.count);
    n = (size_t)sprintf(text, "main() { ");
    memset(text + n, '(', 1000);
    n += 1000;
    text[n++] = '1';
    memset(text + n, ')', 1000);
    n += 1000;
    strcpy(text + n, " }");
    invalid(&arena, text, "nesting limit");
    // Flat source can still create a deeply left-associated tree. Guard that
    // too, so dump-ast and future recursive passes cannot overflow the stack.
    n = (size_t)sprintf(text, "main() { 1");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, " + 1");
    strcpy(text + n, " }");
    invalid(&arena, text, "nesting limit");
    n = (size_t)sprintf(text, "main() { value");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, ".(0)");
    strcpy(text + n, " }\nafter() {}\n");
    invalid(&arena, text, "nesting limit");
    n = (size_t)sprintf(text, "main() { Vec[int] {");
    for (size_t i = 0; i < 300; i++)
      n += (size_t)sprintf(text + n, "%zu,", i);
    strcpy(text + n, "} }\n");
    v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    if (v->program->count) {
      Expr *e =
          v->program->items[0]->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(EXPR_COLLECTION, e->kind);
      ASSERT_EQ_INT(300, e->as.collection.count);
    }
    n = (size_t)sprintf(text, "main() { ");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, "Vec[int] {");
    text[n++] = '0';
    memset(text + n, '}', 1000);
    n += 1000;
    strcpy(text + n, " }\nafter() {}\n");
    invalid(&arena, text, "nesting limit");
    n = (size_t)sprintf(text, "Big = ");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, "Arr[");
    n += (size_t)sprintf(text + n, "int");
    memset(text + n, ']', 1000);
    strcpy(text + n + 1000, "\nmain() {}\n");
    invalid(&arena, text, "nesting limit");
  }
  TEST("malformed and arbitrary token streams terminate under sanitizers") {
    uint32_t state = 7;
    const char alphabet[] = "abc012{}()[]\n<>=:,.!?+-*/|&\"'";
    for (size_t sample = 0; sample < 1000; sample++) {
      Arena temporary;
      arena_init(&temporary);
      char text[128];
      for (size_t i = 0; i < sizeof(text) - 1; i++) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        text[i] = alphabet[(state >> 24) % (sizeof(alphabet) - 1)];
      }
      text[sizeof(text) - 1] = '\0';
      parse(&temporary, text);
      arena_destroy(&temporary);
    }
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
