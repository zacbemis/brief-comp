#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  Program *ast;
  SemaProgram *program;
  Diagnostics errors;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "assignment.brf", text, strlen(text));
  r.ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, r.ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static char *lines(Arena *a, const char *source, int crlf) {
  char *text = arena_alloc(a, 2 * strlen(source) + 1);
  size_t n = 0;
  for (; *source; source++) {
    if (crlf && *source == '\n')
      text[n++] = '\r';
    text[n++] = *source;
  }
  text[n] = 0;
  return text;
}
static char *dump(Arena *a, const Result *r) {
  FILE *out = tmpfile();
  ASSERT_TRUE(out != NULL);
  if (!out)
    return NULL;
  ast_print(out, r->ast);
  long n = ftell(out);
  rewind(out);
  char *text = arena_alloc(a, (size_t)n + 1);
  ASSERT_EQ_INT((size_t)n, fread(text, 1, (size_t)n, out));
  text[n] = 0;
  fclose(out);
  return text;
}
static Result valid(Arena *a, const char *source) {
  Result r = check(a, source);
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
    ASSERT_TRUE(!strcmp(source, r.program->module->text));
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
static void accepts(Arena *a, const char *source) {
  for (int crlf = 0; crlf < 2; crlf++)
    (void)valid(a, lines(a, source, crlf));
}
static void reject(Arena *a, const char *source, const char *message,
                   const char *site, const char *declaration) {
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
      if (r.errors.count != 1)
        continue;
      Diagnostic *d = r.errors.items[0];
      ASSERT_EQ_INT(0, strcmp(message, d->message));
      const char *at = strstr(text, site);
      ASSERT_TRUE(at != NULL);
      if (at)
        ASSERT_EQ_INT((size_t)(at - text), d->span.start);
      ASSERT_TRUE(!d->fixes);
      if (declaration) {
        DiagnosticNote *note = d->notes;
        ASSERT_TRUE(note && !note->next);
        if (note) {
          const char *note_message =
              strstr(message, "Outcome binding") == message
                  ? "The outcome binding is declared here."
                  : "The binding is declared here.";
          ASSERT_EQ_INT(0, strcmp(note_message, note->message));
          const char *decl = strstr(text, declaration);
          ASSERT_TRUE(decl != NULL);
          if (decl)
            ASSERT_EQ_INT((size_t)(decl - text), note->span.start);
        }
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  const char *read = "Binding must be assigned before it is read.";
  const char *once =
      "A declared let binding can only be assigned once on every path.";
  TEST(
      "typed declarations have no synthetic initializer and stable AST spans") {
    for (int crlf = 0; crlf < 2; crlf++) {
      const char *source = "main() { let n: int\nvar text: str\n"
                           "n = 7\ntext = \"ready\"\nprint(text) }\n";
      char *text = lines(&a, source, crlf);
      Result r = valid(&a, text);
      Decl *n = r.ast->items[0]->as.function.body->as.block.items[0].as.decl;
      ASSERT_TRUE(n->as.binding.annotation && !n->as.binding.value);
      const char *end = strstr(text, "int") + 3;
      ASSERT_EQ_INT((size_t)(end - text), n->span.end);
      SemaExpr *bind = r.program->functions[0].body->as.block.items[0];
      ASSERT_TRUE(bind->as.binding.variable->deferred &&
                  !bind->as.binding.value);
      char *printed = dump(&a, &r);
      ASSERT_TRUE(printed && strstr(printed, "(let n int none)"));
    }
    accepts(&a, "main() { let unused: str\nvar empty: ()\nempty = () }");
    reject(&a, "main() { let n\n}", "Expected '=' in binding.", "\n}", NULL);
    reject(&a, "main() { const n: int\n}", "Expected '=' in binding.", "\n}",
           NULL);
    reject(&a, "main() { let n: int 7 }", "Expected '=' in binding.", "7",
           NULL);
    reject(&a, "main() { let n: Missing }",
           "This type is unknown or not implemented yet.", "Missing", NULL);
    Result casing = check(&a, "main() { let Value: int }");
    ASSERT_EQ_INT(1, casing.errors.count);
    ASSERT_TRUE(!casing.program->checked &&
                ir_lower(&a, casing.program) == NULL);
    if (casing.errors.count == 1) {
      Diagnostic *d = casing.errors.items[0];
      ASSERT_TRUE(d->fixes && !d->fixes->next);
      if (d->fixes)
        ASSERT_EQ_INT(0, strcmp("value", d->fixes->replacement));
    }
  }
  TEST("straight line let and var reads require whole initialization") {
    accepts(&a, "main() { let n: int\nn = 7\nprint(str(n))\n"
                "var value: lng\nvalue = 1\nvalue += 2\nvalue++\nvalue-- }");
    accepts(&a, "Box { n: int }\nmain() { let box: Box\n"
                "box = Box { n = 1 }\nbox.n = 2 }");
    accepts(&a, "Data = :left | :right\nMaybe = :ok[str] | :none\n"
                "main() { let data: Data\ndata = :left\nlet copy = data\n"
                "var maybe: Maybe\nmaybe = :none\nmaybe = :ok[\"owned\"]\n"
                "maybe |> { :none => ()\n:ok[text] => print(text) } }");
    reject(&a, "main() { let n: int\nprint(str(n)) }", read, "n))", "let n:");
    reject(&a, "main() { var n: int\nn = n + 1 }", read, "n +", "var n:");
    reject(&a, "main() { var n: int\nn += 1 }", read, "n +=", "var n:");
    reject(&a, "main() { var n: int\nn++ }", read, "n++", "var n:");
    reject(&a, "Box { n: int }\nmain() { var box: Box\nbox.n = 1 }", read,
           "box.n", "var box:");
    reject(&a, "main() { let n: int\nn = 1\nn = 2 }", once, "n = 2", "let n:");
    reject(&a,
           "main() { var flag = true\nlet n: int\n"
           "n = if flag { n = 1\n2 } else { 3 } }",
           once, "n = if", "let n:");
    reject(&a, "main() { let n: int\nn += 1 }",
           "Cannot reassign an immutable binding.", "n +=", NULL);
    reject(&a, "main() { let n: int\nn = true }",
           "Assignment value has the wrong type.", "true", NULL);
  }
  TEST("assignment facts intersect continuing if and match paths") {
    accepts(&a, "main() { var flag = true\nlet n: int\n"
                "if flag { n = 1 } else { n = 2 }\nprint(str(n)) }");
    accepts(&a, "main() { var flag = true\nlet n: int\n"
                "flag |> { true => n = 1\nfalse => n = 2 }\nprint(str(n)) }");
    accepts(&a, "f(flag: bool) -> int { let n: int\n"
                "if flag { n = 1 } else { ret 2 }\nn }\nmain() {}");
    reject(&a,
           "main() { var flag = true\nlet n: int\n"
           "if flag { n = 1 }\nprint(str(n)) }",
           read, "n))", "let n:");
    reject(&a,
           "main() { var flag = true\nlet n: int\n"
           "if flag { n = 1 }\nn = 2 }",
           once, "n = 2", "let n:");
    reject(&a,
           "main() { var flag = true\nlet n: int\n"
           "let b = flag and if flag { n = 1\ntrue } else { false }\n"
           "print(str(n)) }",
           read, "n))", "let n:");
  }
  TEST(
      "basic loop guards reject repeated let writes and zero iteration reads") {
    accepts(
        &a,
        "main() { let n: int\nwhile true { n = 1\nbreak }\nprint(str(n)) }");
    accepts(&a, "main() { for i in 0..3 { let n: int\nn = "
                "i\nprint(str(n))\ncontinue } }");
    accepts(&a, "main() { var n: int\nn = 0\nfor i in 0..3 { n += i "
                "}\nprint(str(n)) }");
    reject(&a, "main() { var flag = true\nlet n: int\nwhile flag { n = 1 } }",
           once, "n = 1", "let n:");
    reject(&a, "main() { let n: int\nfor i in 0..3 { n = i\ncontinue } }", once,
           "n = i", "let n:");
    reject(&a, "main() { var n: int\nfor i in 0..3 { n = i }\nprint(str(n)) }",
           read, "n))", "var n:");
  }
  TEST("ownership slots are registered before assignment and keep normal RC") {
    Result r = valid(&a, "Box { text: str }\nmain() { let never: Box\n"
                         "let text: str\ntext = \"owned \" + \"text\"\n"
                         "var box: Box\nbox = Box { text = text }\n"
                         "let alias = box\nbox = Box { text = \"other\" }\n"
                         "print(alias.text) }");
    IrProgram *ir = ir_lower(&a, r.program);
    ASSERT_TRUE(ir != NULL);
    if (ir) {
      size_t first_store = SIZE_MAX, drops = 0;
      IrFunction *f = &ir->functions[ir->entry];
      for (size_t i = 0; i < f->count; i++) {
        IrInstruction *in = &f->instructions[i];
        if (in->op == IR_STORE && first_store == SIZE_MAX)
          first_store = in->a;
        if (in->op == IR_DROP_VAR && in->a == 0)
          drops++;
      }
      ASSERT_EQ_INT(1, first_store); // No synthetic store to the never slot.
      ASSERT_EQ_INT(1, drops);
    }
    accepts(
        &a,
        "Box { text: str }\nf(flag: bool) -> int { let box: Box\n"
        "if flag { ret 0 }\nbox = Box { text = \"owned\" }\n1 }\nmain() {}");
    accepts(&a, "main() { let n: int\nn = if true { ret 0 } else { 1 }\nn }");
  }
  TEST("deferred outcomes begin obligations only at produced values") {
    const char *prefix = "R = :ok[int] | :!bad[str]\nf() -> R { 1 }\n";
    const char *bodies[] = {
        "let r: R", "let r: R\nr = f()\nr!",
        "var r: R\nr = f()\nr!\nr = :!bad[\"owned\"]\nret r",
        "let r: R\nif true { r = f()\nr! }",
        "let r: R\nr = f()\nr |> { _ => () }"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      char source[1024];
      snprintf(source, sizeof(source), "%smain() { %s\n}", prefix, bodies[i]);
      accepts(&a, source);
    }
    reject(&a, "R = :ok[int] | :!bad\nmain() { let r: R\nr! }", read, "r!",
           "let r:");
    reject(
        &a,
        "R = :ok[int] | :!bad\nmain() { let r: R\nr = :!bad\nr!\nr = :ok[1] }",
        once, "r = :ok", "let r:");
    reject(&a, "R = :ok[int] | :!bad\nmain() { let r: R\nr = :!bad\n}",
           "Outcome binding is not handled before leaving its scope.", "}",
           "let r:");
    reject(&a,
           "R = :ok[int] | :!bad\nmain() { var r: R\nr = :!bad\nr = :ok[1] }",
           "Outcome binding cannot be reassigned.", "r = :ok", "var r:");
  }
  TEST("deferred assignments preserve readonly parameters and fresh "
       "replacements") {
    reject(&a,
           "Box { n: int }\nf(box: Box) { let alias: Box\nalias = box\n"
           "alias.n = 2 }\nmain() {}",
           "Cannot mutate through an ordinary parameter; use &.", "alias.n",
           NULL);
    accepts(&a, "Box { n: int }\nf(box: &Box) { let alias: Box\nalias = box\n"
                "alias.n = 2 }\nmain() {}");
    accepts(&a, "Box { n: int }\nf(box: Box) { var alias: Box\nalias = box\n"
                "alias = Box { n = 1 }\nalias.n = 2 }\nmain() {}");
    reject(
        &a,
        "Box { n: int }\nR = :ok[Box] | :!bad\nsource(box: Box) -> R { box }\n"
        "f(box: Box) { let r: R\nr = source(box)\nlet alias = r!\n"
        "alias.n = 2 }\nmain() {}",
        "Cannot mutate through an ordinary parameter; use &.", "alias.n", NULL);
  }
  TEST("scope and recovery keep ordinary type checks and immutable names") {
    accepts(&a, "main() { let n = 1\nif true { let n: int\nn = "
                "2\nprint(str(n)) }\nprint(str(n)) }");
    accepts(&a, "main() { ret\nlet n: int\nprint(str(n)) }");
    reject(&a, "main() { let n: int\nlet n: int }",
           "Duplicate binding in this scope.", "let n: int }", NULL);
    Result wrong =
        check(&a, "main() { var n: int\nn = true\nn = 1\nprint(str(n)) }");
    ASSERT_EQ_INT(1, wrong.errors.count);
    if (wrong.errors.count == 1)
      ASSERT_EQ_INT(0, strcmp("Assignment value has the wrong type.",
                              wrong.errors.items[0]->message));
    reject(&a, "main() { ret\nlet n: int\nn = true }",
           "Assignment value has the wrong type.", "true", NULL);
    reject(&a, "main() { let n = 1\nn = 2 }",
           "Cannot reassign an immutable binding.", "n = 2", NULL);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
