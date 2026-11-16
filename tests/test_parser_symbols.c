#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST(
      "bracketed symbol payload lists retain zero, one and several positions") {
    program_dump(
        &arena,
        "E = :!pair[int, str,] | :empty[] | :unit[()]\n"
        "main() { :!pair[1, \"x\",] |> { :!pair[number, text,] => number }\n"
        ":empty[]\n:unit[()] }",
        "(program (type E (union (:!pair int str) (:empty) (:unit ()))) "
        "(fn main (params) _ (block (|> (symbol !pair (int 1) (str \"x\")) "
        "(match (arm (tag !pair (bind number) (bind text)) (name number)))) "
        "(symbol empty) (symbol unit (unit)))))\n");
    expression_dump(&arena, ":outer[:inner[make[int](1)], P { x = 2 }].field",
                    "(field (symbol outer (symbol inner (call (name make) "
                    "(types int) (int 1))) "
                    "(struct-value (name P) (x (int 2)))) field)");
    const char *code = "E = :many[\nint,\nVec[Other],\n]\n"
                       "main() { :many[\n1,\nArr[int] { 2, 3 },\n] |> {\n"
                       ":many[first, second,] => first\n} }\nafter() {}\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[1024];
      size_t n = 0;
      for (size_t i = 0; code[i]; i++) {
        if (crlf && code[i] == '\n')
          text[n++] = '\r';
        text[n++] = code[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      ASSERT_EQ_INT(3, v->program->count);
      UnionEntry entry = v->program->items[0]->as.alias->as.union_type.items[0];
      ASSERT_TRUE(entry.type == NULL);
      ASSERT_EQ_INT(2, entry.count);
      ASSERT_EQ_INT(TYPE_NAME, entry.payloads[0]->kind);
      ASSERT_EQ_INT(TYPE_APPLY, entry.payloads[1]->kind);
      Expr *pipe =
          v->program->items[1]->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(2, pipe->as.pipe.value->as.symbol.count);
      ASSERT_EQ_INT(
          2, pipe->as.pipe.stage->as.match.arms[0].pattern->as.symbol.count);
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
  }
  TEST("union types are contextual and flat; payload unions stay nested") {
    program_dump(&arena,
                 "A = :a | Other | :!bad[int]\n"
                 "f(x: :a | :b) -> :ok[:left | :right] | _ {\n"
                 "let bits = 1 | 2\nlet typed: :a | :b = :a\nf[:a | :b]()\n}",
                 "(program (type A (union (:a) (set Other) (:!bad int))) "
                 "(fn f (params (x (union (:a) (:b)))) "
                 "(union (:ok (union (:left) (:right))) _) (block "
                 "(let bits _ (| (int 1) (int 2))) (let typed (union (:a) "
                 "(:b)) (symbol a)) "
                 "(call (name f) (types (union (:a) (:b)))))))\n");
    const char *code = "A = :a |\n:b[int] |\nOther\n"
                       "B { value: :a | :b[int]\nnext: int }\nmain() {}\n";
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
      ASSERT_EQ_INT(3, v->program->count);
      Type *t = v->program->items[0]->as.alias;
      ASSERT_EQ_INT(TYPE_UNION, t->kind);
      ASSERT_EQ_INT(3, t->as.union_type.count);
      ASSERT_EQ_INT(4, t->span.start);
      ASSERT_EQ_INT(3, source_position(v->source, t->span.end).line);
      ASSERT_EQ_INT(2, v->program->items[1]->as.structure.count);
      ASSERT_EQ_INT(TYPE_NAME, t->as.union_type.items[2].type->kind);
    }
    program_dump(&arena, "Sym = :ordinary\nmain() { Sym.value }",
                 "(program (type Sym (union (:ordinary))) (fn main (params) _ "
                 "(block (field (name Sym) value))))\n");
    Parsed *v = parse(&arena, "f() -> _ {}\nA = _\n");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(TYPE_INFER,
                  v->program->items[0]->as.function.return_type->kind);
    ASSERT_EQ_INT(TYPE_INFER, v->program->items[1]->as.alias->kind);
  }
  TEST("payload list width and nesting limits are independent") {
    char *text = arena_alloc(&arena, 20000);
    size_t n = 0;
    n += (size_t)sprintf(text + n, "main() { :wide[");
    for (size_t i = 0; i < 512; i++)
      n += (size_t)sprintf(text + n, "%s1", i ? "," : "");
    strcpy(text + n, "] }");
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(512, v->program->items[0]
                           ->as.function.body->as.block.items[0]
                           .as.expr->as.symbol.count);
    n = (size_t)sprintf(text, "main() { ");
    for (size_t i = 0; i < 140; i++)
      n += (size_t)sprintf(text + n, ":deep[");
    text[n++] = '1';
    for (size_t i = 0; i < 140; i++)
      text[n++] = ']';
    strcpy(text + n, " }");
    invalid(&arena, text, "Syntax nesting limit exceeded.");
  }
  TEST("qualified symbol syntax retains paths, kinds and distinct prefix "
       "spans") {
    expression_dump(&arena, "models.Db.:!bad[1]",
                    "(symbol models.Db.:!bad (int 1))");
    expression_dump(&arena, "x |> { models.Db.:!bad[p: str] => p }",
                    "(|> (name x) (match (arm (tag models.Db.:!bad (typed-bind "
                    "p str)) (name p))))");
    program_dump(&arena, "A = Db.:!bad[str] | :ok[int]\n",
                 "(program (type A (union (Db.:!bad str) (:ok int))))\n");
    const char *lf =
        "A = models\n . Db\n . :!bad[str]\nmain() { models.Db.:value[1] }\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[512];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      UnionEntry entry = v->program->items[0]->as.alias->as.union_type.items[0];
      ASSERT_EQ_INT(0, strcmp("models.Db", entry.module));
      ASSERT_TRUE(entry.module == intern(&v->names, "models.Db"));
      ASSERT_EQ_INT(SYMBOL_ERROR, entry.kind);
      ASSERT_EQ_INT((size_t)(strstr(text, "models") - text),
                    entry.module_span.start);
      ASSERT_EQ_INT((size_t)(strstr(text, "Db") - text) + 2,
                    entry.module_span.end);
      ASSERT_EQ_INT((size_t)(strstr(text, ":!bad") - text),
                    entry.tag_span.start);
      ASSERT_EQ_INT(5, entry.tag_span.end - entry.tag_span.start);
      Expr *value =
          v->program->items[1]->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(0, strcmp("models.Db", value->as.symbol.module));
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
    const char *bad[] = {"main() { get().:bad }",
                         "main() { _. :bad }",
                         "main() { Db.:! }",
                         "A = Db.:none\n",
                         "main() { Db.:ok }",
                         "main() { x |> { Db.:none => () } }",
                         "main() { x |> { Db.Error.: => () } }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(!v->diagnostics.items[j]->fixes);
    }
  }
  TEST("qualified symbol name fixes preserve comments and line breaks") {
    const char *lf = "A = db . :/* keep */ !Bad[Vec[str]]\nmain() {}\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[512];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(3, v->diagnostics.count);
      Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
      ASSERT_EQ_INT(0, fixed->diagnostics.count);
      ASSERT_TRUE(strstr(fixed->source->text,
                         "Db . :!/* keep */ bad[Vec[str]]") != NULL);
      ASSERT_EQ_INT(v->source->line_count, fixed->source->line_count);
      Parsed *reserved = parse(&arena, "A = fn.:bad\n");
      ASSERT_EQ_INT(1, reserved->diagnostics.count);
      ASSERT_TRUE(!reserved->diagnostics.items[0]->fixes);
    }
  }
  TEST("qualified symbol payload width remains iterative") {
    char *text = arena_alloc(&arena, 16000);
    size_t n = (size_t)sprintf(text, "main() { pkg.Db.:wide[");
    for (size_t i = 0; i < 512; i++)
      n += (size_t)sprintf(text + n, "%s%zu", i ? "," : "", i);
    strcpy(text + n, "] }");
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    Expr *value =
        v->program->items[0]->as.function.body->as.block.items[0].as.expr;
    ASSERT_EQ_INT(EXPR_SYMBOL, value->kind);
    ASSERT_EQ_INT(512, value->as.symbol.count);
    ASSERT_EQ_INT(0, strcmp("pkg.Db", value->as.symbol.module));
  }
  TEST("error tag kinds are retained in type, value and pattern syntax") {
    program_dump(&arena,
                 "E = :!bad[int] | :ok[int]\nmain() { :!bad[1] |> { "
                 ":!bad[value] => value } }",
                 "(program (type E (union (:!bad int) (:ok int))) (fn main "
                 "(params) _ (block "
                 "(|> (symbol !bad (int 1)) (match (arm (tag !bad (bind "
                 "value)) (name value)))))))\n");
    expression_dump(&arena, ":!bad!", "(panic (symbol !bad))");
    expression_dump(&arena, ":bad!", "(panic (symbol bad))");
    expression_dump(&arena, ":!bad != :!other",
                    "(!= (symbol !bad) (symbol !other))");
    Parsed *v = parse(
        &arena, "E = :!bad[int]\nmain() { :!bad[1] |> { :!bad[_] => () } }");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    if (v->program->count == 2) {
      UnionEntry entry = v->program->items[0]->as.alias->as.union_type.items[0];
      ASSERT_EQ_INT(SYMBOL_ERROR, entry.kind);
      ASSERT_EQ_INT(5, entry.tag_span.end - entry.tag_span.start);
      Expr *pipe =
          v->program->items[1]->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(SYMBOL_ERROR, pipe->as.pipe.value->as.symbol.kind);
      ASSERT_EQ_INT(
          SYMBOL_ERROR,
          pipe->as.pipe.stage->as.match.arms[0].pattern->as.symbol.kind);
    }
  }
  TEST("error tag prefix fixes preserve trivia and never mutate tokens") {
    const char *cases[] = {"main() { : !bad }",
                           "main() { :/* keep */!bad }",
                           "main() { :\n!bad }",
                           "E = : /* keep */ !bad[int]\n",
                           "main() { x |> { : !bad[_] => () } }",
                           "main() { : !Bad }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[512];
        size_t n = 0;
        for (size_t j = 0; cases[i][j]; j++) {
          if (crlf && cases[i][j] == '\n')
            text[n++] = '\r';
          text[n++] = cases[i][j];
        }
        strcpy(text + n, "\nafter() {}\n");
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(i == 5 ? 2 : 1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        if (v->diagnostics.count) {
          const Diagnostic *d = v->diagnostics.items[0];
          ASSERT_EQ_INT(0,
                        strcmp("The ':!' error-symbol prefix must be adjacent.",
                               d->message));
          ASSERT_TRUE(d->span.file == v->source);
          if (strstr(text, "/* keep */")) {
            // A comment stays put; the bang moves before it.
            ASSERT_TRUE(d->fixes && d->fixes->next && !d->fixes->next->next);
            if (d->fixes && d->fixes->next) {
              ASSERT_EQ_INT(0, strcmp(":!", d->fixes->replacement));
              ASSERT_EQ_INT(0, strcmp("", d->fixes->next->replacement));
              ASSERT_EQ_INT(1, d->fixes->span.end - d->fixes->span.start);
              ASSERT_EQ_INT(1, d->fixes->next->span.end -
                                   d->fixes->next->span.start);
            }
          } else {
            // A whitespace-only gap closes into one ':!' spelling.
            ASSERT_TRUE(d->fixes && !d->fixes->next);
            if (d->fixes) {
              ASSERT_EQ_INT(0, strcmp("!", d->fixes->replacement));
              ASSERT_EQ_INT(':', text[d->fixes->span.start - 1]);
              ASSERT_EQ_INT('!', text[d->fixes->span.end - 1]);
            }
          }
        }
        Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        if (strstr(text, "/* keep */"))
          ASSERT_TRUE(strstr(fixed->source->text, "/* keep */") != NULL);
        else
          ASSERT_TRUE(strstr(fixed->source->text, ":!bad") ||
                      strstr(fixed->source->text, ":!Bad"));
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(v->diagnostics.count, again.count);
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
      }
    }
    const char *bad[] = {"main() { :! }", "main() { : ! }", "E = :![]\n",
                         "main() { x |> { :! => () } }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(v->diagnostics.items[j]->fixes == NULL);
    }
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
