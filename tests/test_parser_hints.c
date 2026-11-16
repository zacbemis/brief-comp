#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("C-style logical and prefix update hints have exact spans and recover") {
    const char *prefix = "Increment and decrement must be postfix standalone "
                         "statements (x++ or x--).";
    const char *and_hint = "Use 'and' instead of '&&'.";
    const char *or_hint = "Use 'or' instead of '||'.";
    struct {
      const char *expression, *operator, *message;
    } cases[] = {{"++x", "++", prefix},
                 {"--x", "--", prefix},
                 {"++ x", "++", prefix},
                 {"-- user.age", "--", prefix},
                 {"f(++x)", "++", prefix},
                 {"let y = --x", "--", prefix},
                 {"ret ++x", "++", prefix},
                 {"if ++x {}", "++", prefix},
                 {"while --x {}", "--", prefix},
                 {"for i in 0..++x {}", "++", prefix},
                 {"x + --y", "--", prefix},
                 {"(++x)", "++", prefix},
                 {":ok[--x]", "--", prefix},
                 {"items.(++x)", "++", prefix},
                 {"x |> { _ => ++x }", "++", prefix},
                 {"true && false", "&&", and_hint},
                 {"true&&false", "&&", and_hint},
                 {"true||false", "||", or_hint},
                 {"true ||false", "||", or_hint},
                 {"true|| false", "||", or_hint},
                 {"true\t&&\tfalse", "&&", and_hint},
                 {"true/*left*/&&/*right*/false", "&&", and_hint},
                 {"(true)&&(false)", "&&", and_hint},
                 {"f(true && false)", "&&", and_hint},
                 {"let y = true || false", "||", or_hint},
                 {"ret true && false", "&&", and_hint},
                 {"if true || false {}", "||", or_hint},
                 {"while true && false {}", "&&", and_hint},
                 {"(true || false)", "||", or_hint},
                 {":ok[true && false]", "&&", and_hint},
                 {"x |> { _ => true || false }", "||", or_hint},
                 {"a < b && c > d", "&&", and_hint},
                 {"a + b||c", "||", or_hint}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[512], crlf[1024];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n",
               cases[i].expression);
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
          const Diagnostic *d = v->diagnostics.items[0];
          size_t start = (size_t)(strstr(source, cases[i].operator) - source);
          ASSERT_EQ_STRN(cases[i].message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(start + 2, d->span.end);
          ASSERT_EQ_INT(2, source_position(v->source, d->span.start).line);
          if (cases[i].message == prefix) {
            // Moving an update is not safe in an arbitrary expression.
            ASSERT_TRUE(d->fixes == NULL);
          } else {
            ASSERT_TRUE(d->fixes != NULL && d->fixes == d->last_fix);
            if (d->fixes) {
              const DiagnosticFix *fix = d->fixes;
              ASSERT_TRUE(fix->span.file == v->source);
              ASSERT_EQ_INT(start, fix->span.start);
              ASSERT_EQ_INT(start + 2, fix->span.end);
              ASSERT_TRUE(fix->next == NULL);
              char rewritten[1100];
              snprintf(rewritten, sizeof(rewritten), "%.*s%s%s", (int)start,
                       source, fix->replacement, source + start + 2);
              Parsed *fixed = parse(&arena, rewritten);
              if (fixed->diagnostics.count)
                diagnostics_print(&fixed->diagnostics, stderr);
              ASSERT_EQ_INT(0, fixed->diagnostics.count);
            }
          }
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
  }
  TEST("logical hints require adjacent bytes and preserve valid operators") {
    const char *separated[] = {"a & &b",  "a | |b",         "a &\n&b",
                               "a |\n|b", "a & // gap\n&b", "a | // gap\n|b",
                               "+ +x",    "a + +b"};
    for (size_t i = 0; i < sizeof(separated) / sizeof(separated[0]); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() { %s }", separated[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        ASSERT_EQ_STRN("Expected an expression.",
                       v->diagnostics.items[0]->message,
                       strlen(v->diagnostics.items[0]->message));
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
      }
    }
    expression_dump(&arena, "a & b | c", "(| (& (name a) (name b)) (name c))");
    expression_dump(&arena, "a & // gap\nb", "(& (name a) (name b))");
    expression_dump(&arena, "a | // gap\nb", "(| (name a) (name b))");
    expression_dump(&arena, "a or b and not c",
                    "(or (name a) (and (name b) (not (name c))))");
    expression_dump(&arena, "- -x", "(- (- (name x)))");
    program_dump(&arena, "P { unit: () }\nf(p: &P) {}",
                 "(program (struct P (field unit ())) (fn f (params (p & P)) _ "
                 "(block)))\n");
    Parsed *v = parse(
        &arena, "main() { true && false\ntrue || false\n++x\nlet kept = 1 }\n");
    TokenArray filtered = newline_filter(&arena, v->raw);
    Token *saved = arena_alloc(&arena, filtered.count * sizeof(*saved));
    memcpy(saved, filtered.items, filtered.count * sizeof(*saved));
    size_t ampersands = 0, bars = 0;
    for (size_t i = 0; i < v->raw.count; i++)
      if (v->raw.items[i].type == TOK_BIT_AND ||
          v->raw.items[i].type == TOK_BIT_OR) {
        ASSERT_EQ_INT(1, v->raw.items[i].length);
        ampersands += v->raw.items[i].type == TOK_BIT_AND;
        bars += v->raw.items[i].type == TOK_BIT_OR;
      }
    ASSERT_EQ_INT(2, ampersands); // Lexer still emits ordinary bitwise tokens.
    ASSERT_EQ_INT(2, bars);
    for (unsigned i = 0; i < 2; i++) {
      Diagnostics d;
      diagnostics_init(&d, &arena);
      parse_program(&arena, &v->names, filtered, &d);
      ASSERT_EQ_INT(3, d.count);
      ASSERT_TRUE(
          memcmp(saved, filtered.items, filtered.count * sizeof(*saved)) == 0);
    }
    v = parse(&arena,
              "main() { let value = @\ntrue && @\n--x\nlet kept = 1 }\n");
    ASSERT_EQ_INT(4,
                  v->diagnostics.count); // Two lexer and two operator errors.
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strcmp(v->diagnostics.items[i]->message,
                         "Expected an expression.") != 0);
  }
  TEST("postfix chains distinguish panic, logical not, and !=") {
    expression_dump(
        &arena, "items.(0).get()?!",
        "(panic (? (call (field (index (name items) (int 0)) get))))");
    expression_dump(&arena, "not x != y!",
                    "(!= (not (name x)) (panic (name y)))");
    expression_dump(&arena, "json |> parse()? |> validate()!",
                    "(|> (|> (name json) (? (call (name parse)))) (panic (call "
                    "(name validate))))");
  }
  TEST("struct header hints have exact spans and recover without token "
       "changes") {
    const char *message =
        "Wrap struct literals in conditions or iterables in parentheses.";
    const char *cases[] = {
        "if Guest { name = name }.isAllowed() {}",
        "while Guest { name = name }.ok {}",
        "for x in Guest { name = name }.items {}",
        "for i in 0..Limit { n = 2 }.n {}",
        "if ready and Guest { name = name }.ok {}",
        "if not Guest { name = name }.ok {}",
        "if module.Guest { name = name }.ok {}",
        "if (Guest) { name = name }.ok {}",
        "if Guest { /* comment */ name = name }.ok {}",
        "if Guest {\n// comment\nname = name, other = P { n = 1 }\n}.ok {}",
        "let value = if Guest { name = name }.ok {} else {}",
        "ret if Guest { name = name }.ok {} else {}",
        "sink(if Guest { name = name }.ok {})",
        "if outer(if Guest { name = name }.ok {}) {}",
        // Parenthesized: one case, intentionally split across two literals.
        ("if test[:ok[:inner[int]]](0) and "
         "Guest { name = name }.ok {}")};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[1024], crlf[2048];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n", cases[i]);
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
        const char *brace = strchr(strchr(source, '\n') + 1, '{');
        size_t start = (size_t)(brace - source);
        if (v->diagnostics.count) {
          Diagnostic *d = v->diagnostics.items[0];
          ASSERT_EQ_STRN(message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(start + 1, d->span.end);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          Expr *body = v->program->items[0]->as.function.body;
          ASSERT_EQ_INT(1, body->as.block.count);
          if (body->as.block.count == 1) {
            BlockItem kept = body->as.block.items[0];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, kept.kind);
            if (kept.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", kept.as.decl->name,
                             strlen(kept.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        size_t raw_bytes = v->raw.count * sizeof(Token);
        Token *raw_saved = arena_alloc(&arena, raw_bytes);
        memcpy(raw_saved, v->raw.items, raw_bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        Program *program = parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(1, again.count);
        if (again.count) {
          ASSERT_EQ_STRN(message, again.items[0]->message,
                         strlen(again.items[0]->message));
          ASSERT_EQ_INT(start, again.items[0]->span.start);
          ASSERT_EQ_INT(start + 1, again.items[0]->span.end);
        }
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
        ASSERT_TRUE(memcmp(raw_saved, v->raw.items, raw_bytes) == 0);
        char *first = dump(&arena, v->program), *second = dump(&arena, program);
        ASSERT_TRUE(first && second);
        if (first && second)
          ASSERT_EQ_STRN(first, second, strlen(second));
      }
    }
    Parsed *v =
        parse(&arena, "main() {\nif Guest { name = name }.ok {}\nlet broken 1\n"
                      "let kept = 1\n}\nafter() {}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    if (v->diagnostics.count == 2) {
      ASSERT_EQ_STRN(message, v->diagnostics.items[0]->message,
                     strlen(v->diagnostics.items[0]->message));
      ASSERT_EQ_STRN("Expected '=' in binding.",
                     v->diagnostics.items[1]->message,
                     strlen(v->diagnostics.items[1]->message));
    }
    v = parse(&arena, "main() {\nif Guest { name = @ }.ok {}\nlet kept = 1\n}");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    if (v->diagnostics.count == 2) {
      ASSERT_EQ_STRN("Unexpected character.", v->diagnostics.items[0]->message,
                     strlen(v->diagnostics.items[0]->message));
      ASSERT_EQ_STRN(message, v->diagnostics.items[1]->message,
                     strlen(v->diagnostics.items[1]->message));
    }
  }
  TEST("header hints preserve ordinary blocks and literal escape contexts") {
    expression_dump(&arena, "if ready {}.field",
                    "(field (if (name ready) (block) none) field)");
    expression_dump(&arena, "if Empty {}.ok {}",
                    "(struct-value (field (if (name Empty) (block) none) ok))");
    expression_dump(&arena, "if ready {}", "(if (name ready) (block) none)");
    expression_dump(&arena, "while ready {}", "(while (name ready) (block))");
    expression_dump(&arena, "for x in values {}",
                    "(for x (name values) (block))");
    const char *valid[] = {
        "if ready { field }",
        "if ready { :tag }",
        "if ready { let field: int = 1\nfield }",
        "if ready { Guest { name = name } }",
        "if (Guest { name = name }).ok {}",
        "while (Guest { name = name }).ok {}",
        "for i in 0..(Limit { n = 2 }).n {}",
        "if check(Guest { name = name }) {}",
        "while check(Guest { name = name }) {}",
        "for i in bounds(Limit { n = 2 }) {}",
        "if items.(Guest { name = name }) {}",
        "while items.(Guest { name = name }) {}",
        "for x in items.(Guest { name = name }) {}",
        "if check[:ok[:inner[int]]](Guest { name = name }) {}"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
      char source[512];
      snprintf(source, sizeof(source), "main() { %s }", valid[i]);
      Parsed *v = parse(&arena, source);
      if (v->diagnostics.count)
        diagnostics_print(&v->diagnostics, stderr);
      ASSERT_EQ_INT(0, v->diagnostics.count);
    }
    const char *other_errors[] = {"main() { if true { field: value } }",
                                  "main() { if check() { field: value } }",
                                  "main() { if ready { field() -> value } }",
                                  "main() { if ready { true: value } }",
                                  "main() { if ready",
                                  "main() { if ready {",
                                  "main() { if ready {\n",
                                  "main() { if ready { field"};
    for (size_t i = 0; i < sizeof(other_errors) / sizeof(other_errors[0]);
         i++) {
      Parsed *v = parse(&arena, other_errors[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(strstr(v->diagnostics.items[j]->message,
                           "Wrap struct literals") == NULL);
    }
    Parsed *v = parse(&arena, "main() { if @ {} }");
    ASSERT_EQ_INT(1, v->diagnostics.count);
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
