#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("declaration roles diagnose exact name spans and safe case fixes") {
    struct {
      const char *code, *bad, *fixed, *message;
    } cases[] = {{"date { day: int }", "date", "Date",
                  "Type names must start with an uppercase letter."},
                 {"Read() {}", "Read", "read",
                  "Function names must start with a lowercase letter."},
                 {"Date { Read() {} }", "Read", "read",
                  "Function names must start with a lowercase letter."},
                 {"Date { pub Year: int }", "Year", "year",
                  "Field names must start with a lowercase letter."},
                 {"f(Count: int) {}", "Count", "count",
                  "Parameter names must start with a lowercase letter."},
                 {"main() { let Count = 0 }", "Count", "count",
                  "Binding names must start with a lowercase letter."},
                 {"main() { var Total: int = 0 }", "Total", "total",
                  "Binding names must start with a lowercase letter."},
                 {"const MAX_USERS = 100", "MAX_USERS", "maxUsers",
                  "Binding names must start with a lowercase letter."},
                 {"pub const URL = 1", "URL", "url",
                  "Binding names must start with a lowercase letter."},
                 {"main() { for Index in 0..2 {} }", "Index", "index",
                  "Loop variables must start with a lowercase letter."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        const char *nl = crlf ? "\r\n" : "\n";
        char text[512];
        snprintf(text, sizeof(text), "\t%s%s// preserved%safter() {}%s",
                 cases[i].code, nl, nl, nl);
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        size_t start = (size_t)(strstr(text, cases[i].bad) - text);
        if (v->diagnostics.count != 1)
          continue;
        Diagnostic *d = v->diagnostics.items[0];
        ASSERT_EQ_INT(0, strcmp(cases[i].message, d->message));
        ASSERT_TRUE(d->span.file == v->source);
        ASSERT_EQ_INT(start, d->span.start);
        ASSERT_EQ_INT(start + strlen(cases[i].bad), d->span.end);
        ASSERT_TRUE(d->fixes && d->fixes == d->last_fix && !d->fixes->next);
        if (d->fixes) {
          ASSERT_TRUE(d->fixes->span.file == v->source);
          ASSERT_EQ_INT(start, d->fixes->span.start);
          ASSERT_EQ_INT(d->span.end, d->fixes->span.end);
          ASSERT_EQ_INT(0, strcmp(cases[i].fixed, d->fixes->replacement));
          Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
          ASSERT_EQ_INT(0, fixed->diagnostics.count);
          ASSERT_EQ_INT(2, fixed->program->count);
          ASSERT_TRUE(strstr(fixed->source->text, "// preserved") != NULL);
        }
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(1, again.count);
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
      }
    }
  }
  TEST("Self is ordinary but keyword self is not a casing-repair name") {
    const char *sources[] = {"self { value: int }\n", "type self = :a\n",
                             "import models.self\n"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Parsed *v = parse(&arena, sources[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(!v->diagnostics.items[j]->fixes);
    }
    Parsed *v = parse(&arena, "Self = int\nf(n: Self) -> Self { n }\n");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(TYPE_NAME,
                  v->program->items[1]->as.function.params[0].type->kind);
    ASSERT_EQ_INT(TYPE_NAME,
                  v->program->items[1]->as.function.return_type->kind);
    ASSERT_TRUE(v->program->items[0]->name ==
                v->program->items[1]->as.function.return_type->as.name);
    expression_dump(&arena, "Self { value = 1 }",
                    "(struct-value (name Self) (value (int 1)))");
  }
  TEST("case fixes do not turn names into keywords or invent wildcard names") {
    const char *sources[] = {"main() { let Int = 0 }",
                             "Not() {}",
                             "P { Bool: int }",
                             "f(True: int) {}",
                             "main() { for For in 0..2 {} }",
                             "fn { value: int }",
                             "main() { let _ = 0 }",
                             "_() {}",
                             "_ { value: int }",
                             "P { _: int }",
                             "f(_: int) {}",
                             "main() { for _ in 0..2 {} }"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Parsed *v = parse(&arena, sources[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
    }
    // Missing syntax retains its primary error instead of a misleading name
    // fix.
    const char *bad[] = {"Bad() -> {}", "f(Bad: ) {}", "main() { let Bad = }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
    }
  }
  TEST("tag spelling roles require lowercase names with exact fixes") {
    struct {
      const char *code, *bad, *fixed;
    } cases[] = {
        {"A = :Bad\n", "Bad", "bad"},
        {"A = :Bad[int] | :other\n", "Bad", "bad"},
        {"A = Vec[:BAD_VALUE[int]]\n", "BAD_VALUE", "badValue"},
        {"main() { :Bad }\n", "Bad", "bad"},
        {"main() { :Bad[1] }\n", "Bad", "bad"},
        {"main() { : /* keep */ BAD_VALUE[1] }\n", "BAD_VALUE", "badValue"},
        {"main() { value |> { :Bad => 1\n_ => 0 } }\n", "Bad", "bad"},
        {"main() { value |> { :Bad[payload] => payload } }\n", "Bad", "bad"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[1024];
        size_t n = 0;
        for (size_t j = 0; cases[i].code[j]; j++) {
          if (crlf && cases[i].code[j] == '\n')
            text[n++] = '\r';
          text[n++] = cases[i].code[j];
        }
        strcpy(text + n, "after() {}\n");
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        if (v->diagnostics.count != 1)
          continue;
        Diagnostic *d = v->diagnostics.items[0];
        size_t at = (size_t)(strstr(text, cases[i].bad) - text);
        ASSERT_EQ_INT(0,
                      strcmp("Symbol tags must start with a lowercase letter.",
                             d->message));
        ASSERT_TRUE(d->span.file == v->source);
        ASSERT_EQ_INT(at, d->span.start);
        ASSERT_EQ_INT(at + strlen(cases[i].bad), d->span.end);
        ASSERT_TRUE(d->fixes && d->fixes == d->last_fix && !d->fixes->next);
        if (d->fixes) {
          ASSERT_TRUE(d->fixes->span.file == v->source);
          ASSERT_EQ_INT(at, d->fixes->span.start);
          ASSERT_EQ_INT(d->span.end, d->fixes->span.end);
          ASSERT_EQ_INT(0, strcmp(cases[i].fixed, d->fixes->replacement));
          Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
          ASSERT_EQ_INT(0, fixed->diagnostics.count);
          ASSERT_EQ_INT(2, fixed->program->count);
        }
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(1, again.count);
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
      }
    }
  }
  TEST("tag fixes avoid keywords and wildcards; payload errors stay primary") {
    const char *no_fix[] = {
        "A = :Int\n", "main() { :Not }", "main() { x |> { :True => 0 } }",
        "A = :_\n",   "main() { :_ }",   "main() { x |> { :_ => 0 } }"};
    for (size_t i = 0; i < sizeof(no_fix) / sizeof(*no_fix); i++) {
      Parsed *v = parse(&arena, no_fix[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
    }
    const char *bad[] = {"A = :Bad[,]\n", "main() { :Bad[,] }",
                         "main() { x |> { :Bad[,] => 0 } }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(v->diagnostics.items[j]->fixes == NULL);
    }
    program_dump(
        &arena,
        "A = :bad_HTTP[int] | _\nmain() { :bad_HTTP[1] |> { :bad_HTTP[_] "
        "=> () } }",
        "(program (type A (union (:bad_HTTP int) _)) (fn main (params) _ "
        "(block (|> (symbol bad_HTTP (int 1)) (match (arm (tag bad_HTTP "
        "(wildcard)) (unit)))))))\n");
  }
  TEST("casing errors keep source order around nested declarations") {
    // Each name is checked after its syntax completes but reported where it
    // was read, before the errors from its parameters, body or payload.
    const char *text = "Foo(X: int, y: :Bad) -> int {\n"
                       "  let Y = X\n"
                       "  for I in 0..3 {\n"
                       "    let Z = :Bad\n"
                       "  }\n"
                       "  Y\n"
                       "}\n"
                       "box { Field: int }\n"
                       "main() {}\n";
    const char *names[] = {"Foo", "X",   "Bad", "Y",    "I",
                           "Z",   "Bad", "box", "Field"};
    Parsed *v = parse(&arena, text);
    size_t count = sizeof(names) / sizeof(*names);
    ASSERT_EQ_INT(count, v->diagnostics.count);
    size_t from = 0;
    for (size_t i = 0; i < count && i < v->diagnostics.count; i++) {
      const Diagnostic *d = v->diagnostics.items[i];
      const char *at = strstr(text + from, names[i]);
      ASSERT_TRUE(at != NULL);
      if (!at)
        break;
      ASSERT_EQ_INT((size_t)(at - text), d->span.start);
      ASSERT_EQ_INT((size_t)(at - text) + strlen(names[i]), d->span.end);
      from = d->span.end;
    }
  }
  TEST("casing is checked once at declarations, not at every use") {
    Parsed *v = parse(
        &arena,
        "date { day: int }\nmain() { date { day = 1 }\ndate { day = 2 } }");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_INT(2, v->program->count);
    // Recovery retains the written struct name; no hidden reference renaming.
    ASSERT_EQ_INT(0, strcmp("date", v->program->items[0]->name));
    v = parse(&arena, "main() { let Count = 0\nCount\nCount }");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    program_dump(&arena,
                 "Data_HTTP { field_HTTP: int }\nread_HTTP(arg_HTTP: "
                 "Data_HTTP) { arg_HTTP.field_HTTP }",
                 "(program (struct Data_HTTP (field field_HTTP int)) (fn "
                 "read_HTTP (params (arg_HTTP Data_HTTP)) _ "
                 "(block (field (name arg_HTTP) field_HTTP))))\n");
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
