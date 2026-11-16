#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("typed payload binders retain names, annotations and grouping spans") {
    expression_dump(&arena, "value |> { :!notFound[path: str] => path }",
                    "(|> (name value) (match (arm (tag !notFound (typed-bind "
                    "path str)) (name path))))");
    expression_dump(
        &arena,
        "value |> { :pair[left: :a | :b, right: Vec[models.User]] => left }",
        "(|> (name value) (match (arm (tag pair (typed-bind left (union (:a) "
        "(:b))) "
        "(typed-bind right (apply Vec models.User))) (name left))))");
    const char *code =
        "main() { value |> {\n:item[(payload:\nVec[models.User])] => "
        "payload\n} }\nafter() {}\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[512];
      size_t n = 0;
      for (size_t i = 0; code[i]; i++) {
        if (crlf && code[i] == '\n')
          text[n++] = '\r';
        text[n++] = code[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      ASSERT_EQ_INT(2, v->program->count);
      Expr *pipe =
          v->program->items[0]->as.function.body->as.block.items[0].as.expr;
      Pattern *typed =
          pipe->as.pipe.stage->as.match.arms[0].pattern->as.symbol.payloads[0];
      ASSERT_EQ_INT(PAT_TYPED_BIND, typed->kind);
      ASSERT_EQ_INT(0, strcmp("payload", typed->as.typed_binding.name));
      ASSERT_TRUE(typed->as.typed_binding.name == intern(&v->names, "payload"));
      Span name_span = typed->as.typed_binding.name_span;
      ASSERT_TRUE(name_span.file == v->source);
      ASSERT_EQ_INT((size_t)(strstr(text, "payload:") - text), name_span.start);
      ASSERT_EQ_INT(7, name_span.end - name_span.start);
      ASSERT_EQ_INT('(', text[typed->span.start]);
      ASSERT_EQ_INT(')', text[typed->span.end - 1]);
      ASSERT_EQ_INT(TYPE_APPLY, typed->as.typed_binding.annotation->kind);
      ASSERT_EQ_INT(
          0,
          strcmp(
              "models.User",
              typed->as.typed_binding.annotation->as.apply.args[0]->as.name));
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_EQ_INT(0, strcmp(dump(&arena, v->program), dump(&arena, next)));
      ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
    }
    const char *bad[] = {"main() { value |> { whole: int => whole } }",
                         "main() { value |> { :item[_: int] => () } }",
                         "main() { value |> { :item[pkg.name: int] => () } }",
                         "main() { value |> { :item[Value:] => () } }",
                         "main() { value |> { :item[Value: int junk] => () } }",
                         "main() { value |> { :item[Value: int",
                         "main() { :item[value: int] }",
                         "E = :item[value: int]\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(v->diagnostics.items[j]->fixes == NULL);
    }
    // In a value or a type entry, the colon gets the pattern-only message.
    const char *elsewhere[] = {"main() { :item[value: int] }",
                               "E = :item[value: int]\n"};
    for (size_t i = 0; i < sizeof(elsewhere) / sizeof(*elsewhere); i++) {
      Parsed *v = parse(&arena, elsewhere[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        const Diagnostic *d = v->diagnostics.items[0];
        ASSERT_EQ_INT(
            0, strcmp("Typed bindings are only allowed in symbol payload "
                      "patterns.",
                      d->message));
        ASSERT_EQ_INT(':', elsewhere[i][d->span.start]);
      }
    }
  }
  TEST("typed binder casing fixes compose safely") {
    struct {
      const char *name, *fixed;
    } cases[] = {{"Value", "value"}, {"HTTP_CODE", "httpCode"}, {"Not", NULL}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[256];
      snprintf(text, sizeof(text),
               "main() { value |> { :item[%s: int] => () } }", cases[i].name);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count == 1) {
        Diagnostic *d = v->diagnostics.items[0];
        ASSERT_EQ_INT(
            0,
            strcmp("Payload binding names must start with a lowercase letter.",
                   d->message));
        ASSERT_EQ_INT((size_t)(strstr(text, cases[i].name) - text),
                      d->span.start);
        ASSERT_EQ_INT(strlen(cases[i].name), d->span.end - d->span.start);
        if (cases[i].fixed) {
          ASSERT_TRUE(d->fixes && !d->fixes->next);
          if (d->fixes)
            ASSERT_EQ_INT(0, strcmp(cases[i].fixed, d->fixes->replacement));
          Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
          ASSERT_EQ_INT(0, fixed->diagnostics.count);
        } else
          ASSERT_TRUE(d->fixes == NULL);
      }
    }
    const char *code =
        "main() { value |> {\n:item[Value: Vec[str]] => ()\n} }\nafter() {}\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[512];
      size_t n = 0;
      for (size_t i = 0; code[i]; i++) {
        if (crlf && code[i] == '\n')
          text[n++] = '\r';
        text[n++] = code[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
      ASSERT_EQ_INT(0, fixed->diagnostics.count);
      ASSERT_EQ_INT(2, fixed->program->count);
      ASSERT_TRUE(strstr(fixed->source->text, ":item[value: Vec[str]]") !=
                  NULL);
      ASSERT_EQ_INT(v->source->line_count, fixed->source->line_count);
    }
  }
  TEST("typed payload width is iterative and annotation depth stays limited") {
    char *text = arena_alloc(&arena, 20000);
    size_t n = (size_t)sprintf(text, "main() { value |> { :wide[");
    for (size_t i = 0; i < 512; i++)
      n += (size_t)sprintf(text + n, "%sx%zu: int", i ? "," : "", i);
    strcpy(text + n, "] => () } }");
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    Expr *pipe =
        v->program->items[0]->as.function.body->as.block.items[0].as.expr;
    ASSERT_EQ_INT(
        512, pipe->as.pipe.stage->as.match.arms[0].pattern->as.symbol.count);
    n = (size_t)sprintf(text, "main() { value |> { :item[x: ");
    for (size_t i = 0; i < 140; i++)
      n += (size_t)sprintf(text + n, "Vec[");
    n += (size_t)sprintf(text + n, "int");
    for (size_t i = 0; i < 140; i++)
      text[n++] = ']';
    strcpy(text + n, "] => () } }");
    invalid(&arena, text, "Syntax nesting limit exceeded.");
  }
  TEST("match patterns include literals, ranges, symbols, sets, and "
       "catch-alls") {
    expression_dump(
        &arena,
        "age |> {\n< 18 => \"Minor\"\n18..100 => \"Adult\"\n>= 100 => "
        "\"Senior\"\n}",
        "(|> (name age) (match (arm (compare < (int 18)) (str \"Minor\")) (arm "
        "(range (int 18) (int 100)) (str \"Adult\")) (arm (compare >= (int "
        "100)) (str \"Senior\"))))");
    expression_dump(
        &arena,
        "parse(text) |> {\n:ok[value] => value\nJson.Error => ret "
        ":!invalid\nother => ret other\n}",
        "(|> (call (name parse) (name text)) (match (arm (tag ok (bind value)) "
        "(name value)) (arm (set Json.Error) (block (ret (symbol !invalid)))) "
        "(arm (bind other) (block (ret (name other))))))");
    expression_dump(
        &arena, "x |> {\n:ok[:some[user]] => user\n:none => ()\n_ => 0\n}",
        "(|> (name x) (match (arm (tag ok (tag some (bind user))) (name user)) "
        "(arm (tag none) (unit)) (arm (wildcard) (int 0))))");
    expression_dump(
        &arena,
        "x |> {\n-10..0 => 1\ntrue => 2\n\"s\" => 3\n'c' => 4\n() => 5\n(x) => "
        "x\n}",
        "(|> (name x) (match (arm (range (- (int 10)) (int 0)) (int 1)) (arm "
        "(literal (bool true)) (int 2)) (arm (literal (str \"s\")) (int 3)) "
        "(arm (literal (chr 'c')) (int 4)) (arm (literal (unit)) (int 5)) (arm "
        "(bind x) (name x))))");
  }
  TEST("pattern name classes determine binding versus named set without "
       "lookup") {
    expression_dump(&arena, "x |> { Error => 0\nerror => 1\n_ => 2 }",
                    "(|> (name x) (match (arm (set Error) (int 0)) (arm (bind "
                    "error) (int 1)) (arm (wildcard) (int 2))))");
    expression_dump(&arena, "x |> { :ok[Value] => 0\n:ok[value] => value }",
                    "(|> (name x) (match (arm (tag ok (set Value)) (int 0)) "
                    "(arm (tag ok (bind value)) (name value))))");
    expression_dump(&arena,
                    "x |> { (Error) => 0\n(error) => error\nJson.Error => 1 }",
                    "(|> (name x) (match (arm (set Error) (int 0)) (arm (bind "
                    "error) (name error)) (arm (set Json.Error) (int 1))))");
    // Range endpoints remain value references, not named-set/catch-all
    // patterns.
    expression_dump(&arena, "x |> { MIN..MAX => 0\nmin..max => 1 }",
                    "(|> (name x) (match (arm (range (name MIN) (name MAX)) "
                    "(int 0)) (arm (range (name min) (name max)) (int 1))))");
    const char *text = "Known = :a\nmain() { value |> { Known => "
                       "0\nUnknown => 1\nknown => 2\n_ => 3 } }";
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(2, v->program->count);
    if (v->program->count == 2) {
      Expr *pipe =
          v->program->items[1]->as.function.body->as.block.items[0].as.expr;
      const PatternKind kinds[] = {PAT_SET, PAT_SET, PAT_BIND, PAT_WILDCARD};
      ASSERT_EQ_INT(4, pipe->as.pipe.stage->as.match.count);
      for (size_t i = 0; i < pipe->as.pipe.stage->as.match.count; i++) {
        const Pattern *p = pipe->as.pipe.stage->as.match.arms[i].pattern;
        if (i < sizeof(kinds) / sizeof(*kinds))
          ASSERT_EQ_INT(kinds[i], p->kind);
        ASSERT_TRUE(p->span.file == v->source);
        ASSERT_EQ_INT(strlen(p->as.name), p->span.end - p->span.start);
      }
    }
    TokenArray filtered = newline_filter(&arena, v->raw);
    size_t bytes = filtered.count * sizeof(Token);
    Token *saved = arena_alloc(&arena, bytes);
    memcpy(saved, filtered.items, bytes);
    Diagnostics again;
    diagnostics_init(&again, &arena);
    Program *repeated = parse_program(&arena, &v->names, filtered, &again);
    ASSERT_EQ_INT(0, again.count);
    ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
    char *first = dump(&arena, v->program), *second = dump(&arena, repeated);
    ASSERT_TRUE(first && second);
    if (first && second)
      ASSERT_EQ_INT(0, strcmp(first, second));
  }
  TEST("match blocks nested in call arguments retain arm separators") {
    expression_dump(&arena, "f(\nx |> {\n:ok[x] => x\n:none => 0\n}\n)",
                    "(call (name f) (|> (name x) (match (arm (tag ok (bind x)) "
                    "(name x)) (arm (tag none) (int 0)))))");
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
