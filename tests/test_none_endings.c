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
  SourceFile *s = source_create(a, "none.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
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
  }
  IrProgram *ir = ir_lower(a, r.program);
  ASSERT_TRUE(ir != NULL);
  if (ir) {
    ir_insert_rc(a, ir);
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
    char source[2048];
    size_t length = 0;
    for (const char *s = text; *s; s++) {
      if (crlf && *s == '\n')
        source[length++] = '\r';
      source[length++] = *s;
    }
    source[length] = 0;
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      Result r = check(a, source);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        ASSERT_EQ_INT((size_t)(strstr(source, site) - source), d->span.start);
        ASSERT_TRUE(!d->fixes && !d->notes);
      } else
        diagnostics_print(&r.errors, stderr);
      if (r.program->resolved) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
        ASSERT_EQ_INT(1, r.errors.count);
        if (r.errors.count == 1)
          ASSERT_EQ_INT(0, strcmp(message, r.errors.items[0]->message));
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("ambiguous raw sets and unions reject before interning") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SemaTag *none = sema_tag(p, p->module, "none");
    SemaTag *idle = sema_tag(p, p->module, "idle");
    SemaVariant variants[] = {
        {none, NULL, 0}, {p->ok, NULL, 0}, {idle, NULL, 0}, {none, NULL, 0}};
    SemaVariant saved[4];
    memcpy(saved, variants, sizeof(saved));
    size_t sets = p->symbol_set_count;
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      ASSERT_TRUE(sema_ambiguous_endings(variants, 4));
      ASSERT_TRUE(sema_symbol_set(p, variants, 4) == NULL);
      ASSERT_TRUE(sema_symbol_set(p, variants, 2) == NULL);
      ASSERT_TRUE(!memcmp(saved, variants, sizeof(saved)));
      ASSERT_EQ_INT(sets, p->symbol_set_count);
    }
    SemaVariant health[] = {{p->ok, NULL, 0}, {idle, NULL, 0}};
    const SemaType *h = sema_symbol_set(p, health, 2);
    const SemaType *n = sema_symbol_type(p, none, NULL);
    ASSERT_TRUE(sema_none(n)->tag == none);
    ASSERT_TRUE(!sema_none(h) && !sema_none(NULL));
    ASSERT_TRUE(sema_symbol_union(p, h, n) == NULL);
    ASSERT_TRUE(sema_symbol_union(p, n, h) == NULL);
    const SemaType *unit = &p->types[SEMA_UNIT];
    SemaVariant allowed[] = {{p->ok, &unit, 1}, {none, NULL, 0}};
    ASSERT_TRUE(!sema_ambiguous_endings(allowed, 2));
    ASSERT_TRUE(sema_symbol_set(p, allowed, 2) != NULL);
  }
  TEST("written optional endings cover all valueless tail shapes") {
    const char *bodies[] = {"",
                            "let text = \"owned\" + \" local\"",
                            "var n = 0\nn = 1",
                            "var n = 0\nn++",
                            "while false {}",
                            "for i in 0..2 {}",
                            "if true { print(\"effect\") }",
                            "if true {} else {}",
                            "true |> { true => ()\nfalse => () }",
                            "print(\"effect\")",
                            "ret",
                            "()"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[1024], source[2048];
        snprintf(lf, sizeof(lf),
                 "Maybe = :ok[str] | :none\nf() -> Maybe { %s }\n"
                 "main() { f() }",
                 bodies[i]);
        size_t length = 0;
        for (const char *s = lf; *s; s++) {
          if (crlf && *s == '\n')
            source[length++] = '\r';
          source[length++] = *s;
        }
        source[length] = 0;
        Result r = valid(&a, source);
        ASSERT_TRUE(r.program->functions[0].symbol.return_type ==
                    r.program->aliases[0].type);
      }
    valid(&a, "f() -> :none {}\nmain() { f() }");
    valid(&a, "f() -> :more[str] | :none {}\nmain() { f() }");
    valid(&a, "Maybe = :ok[str] | :none\nAgain = :ok[Maybe]\n"
              "f() -> Again {}\nmain() { f() }");
    valid(&a, "f() -> :ok[()] | :none { :ok[()] }\n"
              "g() -> :ok[()] | :none {}\nmain() { f()\ng() }");
  }
  TEST("later none returns determine earlier empty paths and recursive SCCs") {
    valid(&a, "f(n: int) { if n == 0 { ret }\n"
              "if n == 1 { ret 7 }\n:none }\nmain() { f(0) }");
    valid(&a, "f(n: int) -> :ok[lng] | _ { if n == 0 { ret }\n"
              "if n == 1 { ret :none }\n4294967296 }\nmain() { f(0) }");
    valid(&a, "f(b: bool) { if b { () } else { 7 }\n"
              "if b { ret :none }\n7 }\nmain() { f(true) }");
    valid(&a, "f(a: bool, b: bool) { if a { if b { () } else { 7 } }\n"
              "else { ret :none } }\nmain() { f(true, false) }");
    Result r = valid(&a, "f(n: int) { if n == 0 { ret }\n"
                         "if n == 1 { ret :none }\ng(n - 1) }\n"
                         "g(n: int) { if n == 0 { ret \"owned\" }\nf(n - 1) }\n"
                         "main() { f(0) }");
    ASSERT_TRUE(r.program->functions[0].symbol.return_type ==
                r.program->functions[1].symbol.return_type);
    ASSERT_TRUE(sema_none(r.program->functions[0].symbol.return_type));
    r = valid(&a, "f() { ret 7\nret :none }\nmain() { f() }");
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    r = valid(&a, "f() -> :ok[str] | :none { while true {} }\nmain() {}");
    IrProgram *ir = ir_lower(&a, r.program);
    size_t returns = 0;
    for (size_t i = 0; ir && i < ir->functions[0].count; i++)
      returns += ir->functions[0].instructions[i].op == IR_RETURN;
    ASSERT_EQ_INT(0, returns);
  }
  TEST("provisional unit edges do not poison later SCC absence anchors") {
    const char *f = "f(n: int) { if n == 0 { ret }\ng(n - 1) }\n";
    const char *g = "g(n: int) { if n == 0 { ret \"owned\" }\nh(n - 1) }\n";
    const char *h = "h(n: int) { if n == 0 { ret :none }\nf(n - 1) }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char source[2048];
      snprintf(source, sizeof(source), "%s%s%smain() {}", reverse ? h : f, g,
               reverse ? f : h);
      Result r = valid(&a, source);
      for (size_t i = 0; i < 3; i++) {
        ASSERT_TRUE(sema_none(r.program->functions[i].symbol.return_type));
        ASSERT_TRUE(r.program->functions[i].symbol.return_type ==
                    r.program->functions[0].symbol.return_type);
      }
    }
    f = "f(n: int) { if n == 0 { ret }\nif n == 1 { ret :!bad }\n"
        "g(n - 1) |> { other => other } }\n";
    g = "g(n: int) { f(n)?\n:none }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char source[2048];
      snprintf(source, sizeof(source), "%s%smain() {}", reverse ? g : f,
               reverse ? f : g);
      Result r = valid(&a, source);
      for (size_t i = 0; i < 2; i++) {
        const SemaType *type = r.program->functions[i].symbol.return_type;
        ASSERT_TRUE(sema_none(type) && sema_has_errors(type));
        ASSERT_TRUE(!sema_ok(type));
      }
    }
    rejects(&a, "f(b: bool) { if b { ret }\n7 }\nmain() {}",
            "Incompatible return types.", "{ if");
  }
  TEST("ambiguity diagnostics name the type and do not guess a fix") {
    const char *message =
        "A union cannot contain both payload-less ':ok' and ':none'.";
    struct {
      const char *source, *site;
    } cases[] = {
        {"Bad = :ok | :none\nmain() {}", ":ok | :none"},
        {"Bad = :none[] | :ok[]\nmain() {}", ":none[] | :ok[]"},
        {"f() -> :ok | :none { :none }\nmain() {}", ":ok | :none"},
        {"Box { value: :ok | :none }\nmain() {}", ":ok | :none"},
        {"f(value: :ok | :none) {}\nmain() {}", ":ok | :none"},
        {"main() { let value: :ok | :none = :none }", ":ok | :none"},
        {"Bad = :outer[:ok | :none]\nmain() {}", ":ok | :none"},
        {"Health = :ok | :idle\nAbsent = :none | :missing\n"
         "Bad = Health | Absent\nmain() {}",
         "Health | Absent"},
        {"f() -> :ok | _ { :none }\nmain() {}", ":ok | _"},
        {"f(b: bool) -> :ok | _ { if b { ret }\n:none }\nmain() {}", ":ok | _"},
        {"Health = :ok | :idle\nget() -> Health { :idle }\n"
         "f(b: bool) { if b { ret get() }\n:none }\nmain() {}",
         "{ if"},
        {"Health = :ok | :idle\nf() -> Health | _ { :none }\nmain() {}",
         "Health | _"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      rejects(&a, cases[i].source, message, cases[i].site);
  }
  TEST("absence is only an ending conversion, not an arbitrary value cast") {
    rejects(&a, "Maybe = :ok[str] | :none\nmain() { let x: Maybe = () }",
            "Binding value does not match its annotation.", "() }");
    rejects(&a, "f() -> :box[:none] {}\nmain() {}",
            "Function body does not match the written return type.", "{}");
    valid(&a, "f() -> :ok | :!bad {}\nmain() { f()! }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
