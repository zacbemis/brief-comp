#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  const char *message, *site, *declaration;
} Expected;
typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static const char *read_message = "Binding must be assigned before it is read.";
static const char *once_message =
    "A declared let binding can only be assigned once on every path.";
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "assignment-paths.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static char *lines(Arena *a, const char *source, int crlf) {
  char *out = arena_alloc(a, 2 * strlen(source) + 1);
  size_t n = 0;
  for (; *source; source++) {
    if (crlf && *source == '\n')
      out[n++] = '\r';
    out[n++] = *source;
  }
  out[n] = 0;
  return out;
}
static void at(const char *text, const char *site, Span span, int exact) {
  const char *p = strstr(text, site);
  ASSERT_TRUE(p != NULL);
  if (p) {
    ASSERT_EQ_INT((size_t)(p - text), span.start);
    if (exact)
      ASSERT_EQ_INT((size_t)(p - text) + strlen(site), span.end);
  }
}
static void rejects(Arena *a, const char *source, const Expected *expected,
                    size_t count) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = lines(a, source, crlf);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != count) {
        fprintf(stderr, "Unexpected errors:\n%s\n", text);
        diagnostics_print(&r.errors, stderr);
      }
      ASSERT_EQ_INT(count, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      for (size_t i = 0; i < r.errors.count && i < count; i++) {
        Diagnostic *d = r.errors.items[i];
        const Expected *e = &expected[i];
        ASSERT_EQ_INT(0, strcmp(e->message, d->message));
        at(text, lines(a, e->site, crlf), d->span, 0);
        ASSERT_TRUE(!d->fixes);
        if (!e->declaration) {
          ASSERT_TRUE(!d->notes);
          continue;
        }
        DiagnosticNote *note = d->notes;
        ASSERT_TRUE(note && !note->next);
        if (note) {
          ASSERT_EQ_INT(0,
                        strcmp("The binding is declared here.", note->message));
          at(text, e->declaration, note->span, 1);
        }
      }
    }
  }
}
static void rejects_one(Arena *a, const char *source, const char *message,
                        const char *site, const char *declaration) {
  Expected expected = {message, site, declaration};
  rejects(a, source, &expected, 1);
}
static void valid(Arena *a, const char *source) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = lines(a, source, crlf);
    Result r = check(a, text);
    size_t count = r.program->count;
    for (size_t i = 0; i < r.program->count; i++)
      count += r.program->functions[i].variable_count;
    const SemaType **types = arena_calloc(a, count, sizeof(*types));
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count) {
        fprintf(stderr, "Unexpected errors:\n%s\n", text);
        diagnostics_print(&r.errors, stderr);
      }
      ASSERT_EQ_INT(0, r.errors.count);
      ASSERT_TRUE(r.program->checked);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      size_t n = 0;
      for (size_t i = 0; i < r.program->count; i++) {
        SemaFunction *f = &r.program->functions[i];
        if (!repeat)
          types[n] = f->symbol.return_type;
        ASSERT_TRUE(types[n++] == f->symbol.return_type);
        for (size_t j = 0; j < f->variable_count; j++) {
          if (!repeat)
            types[n] = f->variables[j]->type;
          ASSERT_TRUE(types[n++] == f->variables[j]->type);
        }
      }
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
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("continuing nested if and match paths intersect both must properties") {
    valid(&a, "f(a: bool, b: bool) -> int { let n: int\n"
              "if a { if b { n = 1 } else { n = 2 } } else { n = 3 }\nn "
              "}\nmain() {}");
    valid(&a, "f(flag: bool) -> int { let n: int\n"
              "flag |> { true => n = 1\nfalse => ret 2 }\nn }\nmain() {}");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\n"
                "flag |> { true => n = 1\nfalse => () }\nprint(str(n)) }",
                read_message, "n))", "let n: int");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\n"
                "if flag { if flag { n = 1 } } else { n = 2 }\nn = 3 }",
                once_message, "n = 3", "let n: int");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\n"
                "let b = flag or if flag { n = 1\ntrue } else { false }\n"
                "print(str(n)) }",
                read_message, "n))", "let n: int");
  }
  TEST("return propagation break and continue discard only departing paths") {
    valid(&a,
          "f(flag: bool) -> int { let n: int\nwhile true { if flag { n = 1\n"
          "break } else { ret 2 } }\nn }\nmain() {}");
    valid(&a,
          "main() { let n: int\nvar flag = true\nwhile true { if flag { n = 1\n"
          "break } else { n = 2\nbreak } }\nprint(str(n)) }");
    valid(&a, "f(flag: bool) -> :ok[int] | :!bad { let n: int\n"
              "if flag { ret :!bad } else { n = 2 }\nn }\nmain() {}");
    valid(&a, "f() -> :ok[int] | :!bad { let n: int\n"
              "n = :!bad?\nn }\nmain() {}");
    rejects_one(&a, "f() -> int { let n: int\nret n }\nmain() {}", read_message,
                "n }", "let n: int");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\nwhile true { "
                "if flag { n = 1\nbreak } else { break } }\nprint(str(n)) }",
                read_message, "n))", "let n: int");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\n"
                "while flag { n = 1\nbreak }\nprint(str(n)) }",
                read_message, "n))", "let n: int");
  }
  TEST(
      "back edges reject outer let rewrites but allow fresh loop generations") {
    rejects_one(&a, "main() { let n: int\nwhile true { n = 1 } }", once_message,
                "n = 1", "let n: int");
    rejects_one(
        &a, "main() { let n: int\nfor i in 0..3 { n = i\ncontinue }\nn = 4 }",
        once_message, "n = i", "let n: int");
    rejects_one(&a,
                "main() { let n: int\nfor outer in 0..2 { while true { n = 1\n"
                "break } } }",
                once_message, "n = 1", "let n: int");
    valid(&a, "main() { for outer in 0..2 { let n: int\n"
              "while true { n = outer\nbreak }\nprint(str(n))\ncontinue } }");
    valid(&a,
          "main() { var n: int\nn = 0\nfor outer in 0..2 { for inner in 0..2 "
          "{ n += outer + inner\ncontinue } }\nprint(str(n)) }");
    rejects_one(&a,
                "main() { var n: int\nfor i in 0..1 { n = i }\nprint(str(n)) }",
                read_message, "n))", "var n: int");
  }
  TEST("conditions repeat while range bounds run once outside their jump "
       "region") {
    valid(&a,
          "main() { var flag = true\nlet n: int\nfor i in "
          "(if flag { n = 1\n0 } else { n = 2\n0 })..3 {}\nprint(str(n)) }");
    valid(&a, "main() { var flag = true\nlet n: int\nwhile true { for i in "
              "(if flag { n = 1\nbreak } else { 0 })..2 {}\nn = 2\nbreak }\n"
              "print(str(n)) }");
    valid(&a, "main() { var flag = true\nfor outer in 0..2 { let n: int\n"
              "while (if flag { n = 1\ncontinue } else { false }) {}\n"
              "n = 2\nprint(str(n)) } }");
    valid(&a, "main() { var flag = true\nfor outer in 0..2 { let n: int\n"
              "for inner in 0..(if flag { n = 1\ncontinue } else { 2 }) {}\n"
              "n = 2\nprint(str(n)) } }");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\n"
                "while (if flag { n = 1\ntrue } else { false }) {} }",
                once_message, "n = 1", "let n: int");
    rejects_one(
        &a,
        "main() { var flag = true\nlet n: int\nfor outer in 0..2 { "
        "for inner in (if flag { n = 1\ncontinue } else { 0 })..2 {} } }",
        once_message, "n = 1", "let n: int");
    rejects_one(&a,
                "main() { var flag = true\nlet n: int\nfor i in "
                "(if flag { n = 1\n0 } else { n = 2\n0 }).."
                "(if flag { n = 3\n2 } else { 2 }) {} }",
                once_message, "n = 3", "let n: int");
  }
  TEST("RHS evaluation precedes writes without rewriting earlier read facts") {
    valid(&a, "f(flag: bool) -> int { let n: int\nn = if flag { n = 1\n"
              "ret n } else { 2 }\nn }\nmain() {}");
    rejects_one(&a,
                "main() { var n: int\nn += if true { n = 1\n2 } else { 3 } }",
                read_message, "n +=", "var n: int");
    const Expected order[] = {{read_message, "n, if", "let n: int"}};
    rejects(&a,
            "take(a: int, b: int) {}\nmain() { let n: int\n"
            "take(n, if true { n = 1\n2 } else { n = 2\n3 }) }",
            order, 1);
    valid(&a, "take(a: int, b: int) {}\nmain() { let n: int\n"
              "take(if true { n = 1\n2 } else { n = 2\n3 }, n) }");
  }
  TEST("invalid-use recovery cannot silently admit immutable outcome updates") {
    const char *ops[] = {"+= :!bad", "-= :!bad", "*= :!bad", "/= :!bad",
                         "%= :!bad", "++",       "--"};
    const char *phases[] = {"", "r = :ok[1]\n", "r = :ok[1]\nr!\n"};
    for (size_t i = 0; i < 7; i++)
      for (size_t phase = 0; phase < 3; phase++) {
        char source[512], site[128];
        snprintf(site, sizeof(site), "r%s%s", i < 5 ? " " : "", ops[i]);
        snprintf(source, sizeof(source),
                 "R = :ok[int] | :!bad\n"
                 "main() { let r: R\n%s%s }",
                 phases[phase], site);
        rejects_one(&a, source, "Cannot reassign an immutable binding.", site,
                    NULL);
      }
    rejects_one(&a, "R = :ok[int] | :!bad\nmain() { var r: R\nr++ }",
                read_message, "r++", "var r: R");
    const Expected immutable = {"Outcome binding cannot be reassigned.", "r++",
                                NULL};
    // Existing initialized-outcome recovery retains its original primary.
    Result r = check(&a, "main() { let r = :!bad\nr++ }");
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count == 1)
      ASSERT_EQ_INT(0, strcmp(immutable.message, r.errors.items[0]->message));
    ASSERT_TRUE(!r.program->checked && ir_lower(&a, r.program) == NULL);
  }
  TEST("independent type and assignment errors survive recovery and dead "
       "syntax") {
    const Expected independent[] = {
        {"Function argument has the wrong type.", "true)", NULL},
        {read_message, "n))", "let n: int"},
        {read_message, "other))", "let other: int"}};
    rejects(&a,
            "take(n: int) {}\nmain() { let n: int\nlet other: int\n"
            "take(true)\nprint(str(n))\nprint(str(n))\nprint(str(other)) }",
            independent, 3);
    const Expected both[] = {{read_message, "n))", "let n: int"},
                             {once_message, "n = 2", "let n: int"}};
    rejects(&a, "main() { let n: int\nprint(str(n))\nn = 1\nn = 2 }", both, 2);
    const Expected invalid_rhs[] = {
        {"Assignment value has the wrong type.", "true", NULL},
        {read_message, "n))", "let n: int"}};
    rejects(&a, "main() { let n: int\nn = true\nprint(str(n)) }", invalid_rhs,
            2);
    valid(&a, "main() { ret\nlet n: int\nprint(str(n))\nn = 1\nn = 2 }");
    rejects_one(&a, "main() { ret\nlet n: int\nn = true }",
                "Assignment value has the wrong type.", "true", NULL);
    rejects_one(&a,
                "main() { let n: int\ntrue |> { true => ()\nfalse => ()\n"
                "_ => print(str(n)) } }",
                "Unreachable match arm.", "_ =>", NULL);
  }
  TEST("nested assignment and outcome ownership preserve access permissions") {
    valid(&a, "Box { n: int }\nf(box: &Box, flag: bool) { let alias: Box\n"
              "while true { if flag { alias = box\nbreak } else { alias = "
              "box\nbreak } }\n"
              "alias.n = 2 }\nmain() {}");
    rejects_one(&a,
                "Box { n: int }\nf(box: Box, flag: bool) { let alias: Box\n"
                "if flag { alias = box } else { alias = Box { n = 1 } }\n"
                "alias.n = 2 }\nmain() {}",
                "Cannot mutate through an ordinary parameter; use &.",
                "alias.n", NULL);
    rejects_one(
        &a,
        "Box { n: int }\nf(box: Box) { var alias: Box\nalias = box\n"
        "for i in 0..2 { alias = Box { n = i } }\nalias.n = 2 }\nmain() {}",
        "Cannot mutate through an ordinary parameter; use &.", "alias.n", NULL);
    valid(&a, "Box { n: int }\nf(box: Box, flag: bool) { var alias: Box\n"
              "while true { if flag { alias = Box { n = 1 }\nbreak } else { "
              "alias = Box { n = 2 }\nbreak } }\nalias.n = 3 }\nmain() {}");
    valid(&a, "R = :ok[str] | :!bad[str]\nf() -> R { \"owned\" }\n"
              "main() { for i in 0..2 { let r: R\nif true { r = f() } "
              "else { r = :!bad[\"error\"] }\nr |> { _ => () }\ncontinue } }");
    rejects_one(&a,
                "R = :ok[int] | :!bad\nmain() { var flag = true\nlet r: R\n"
                "if flag { r = :ok[1]\nr! }\nr! // read\n}",
                read_message, "r! // read", "let r: R");
    rejects_one(&a,
                "R = :ok[int] | :!bad\nmain() { var flag = true\nlet r: R\n"
                "if flag { r = :ok[1] }\nr! // read\n}",
                read_message, "r! // read", "let r: R");
  }
  TEST("deep loop fixed points converge and rechecks reset all facts") {
    char source[4096];
    size_t n =
        (size_t)snprintf(source, sizeof(source), "main() { let n: int\n");
    for (unsigned i = 0; i < 24; i++)
      n += (size_t)snprintf(source + n, sizeof(source) - n,
                            "for level%u in 0..2 {\n", i);
    n += (size_t)snprintf(source + n, sizeof(source) - n, "n = 1\n");
    for (unsigned i = 0; i < 24; i++)
      n += (size_t)snprintf(source + n, sizeof(source) - n, "}\n");
    snprintf(source + n, sizeof(source) - n, "}\n");
    rejects_one(&a, source, once_message, "n = 1", "let n: int");
    valid(&a,
          "main() { for outer in 0..2 { let n: int\nfor inner in "
          "(if true { n = 1\n0 } else { n = 2\n0 })..2 {}\nprint(str(n)) } }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
