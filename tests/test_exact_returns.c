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
  SourceFile *source = source_create(a, "exact.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, source, &r.errors)),
      &r.errors);
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
    for (size_t i = 0; i < r.program->count; i++)
      ASSERT_TRUE(r.program->functions[i].symbol.return_type ==
                  r.program->functions[i].symbol.outcome_type);
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
    const char *at = strstr(source, site);
    ASSERT_TRUE(at != NULL);
    Result r = check(a, source);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        ASSERT_EQ_INT((size_t)(at - source), d->span.start);
        ASSERT_TRUE(!d->fixes && !d->notes);
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("singleton ok normalization is canonical and non-destructive") {
    Result r = valid(&a, "Data = :a | :b\nError = :!bad[str]\n"
                         "Box { text: str }\nmain() {}");
    SemaProgram *p = r.program;
    const SemaType *types[] = {&p->types[SEMA_UNIT], &p->types[SEMA_INT],
                               &p->types[SEMA_STR],  &p->structs[0].type,
                               p->aliases[0].type,   p->aliases[1].type};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
      const SemaType *payload[] = {types[i]};
      SemaVariant variants[] = {{p->ok, payload, 1}, {p->ok, payload, 1}};
      SemaVariant saved[2];
      memcpy(saved, variants, sizeof(saved));
      size_t sets = p->symbol_set_count, tags = p->tag_count;
      for (unsigned repeat = 0; repeat < 3; repeat++) {
        ASSERT_TRUE(sema_symbol_set(p, variants, 1) == types[i]);
        ASSERT_TRUE(sema_symbol_set(p, variants, 2) == types[i]);
        ASSERT_TRUE(sema_symbol_type(p, p->ok, types[i]) == types[i]);
        ASSERT_TRUE(sema_success(p, types[i]) == types[i]);
        ASSERT_TRUE(!memcmp(saved, variants, sizeof(saved)));
        ASSERT_TRUE(payload[0] == types[i]);
        ASSERT_EQ_INT(sets, p->symbol_set_count);
        ASSERT_EQ_INT(tags, p->tag_count);
      }
    }
    SemaVariant empty = {p->ok, NULL, 0};
    ASSERT_TRUE(sema_symbol_set(p, &empty, 1) == &p->types[SEMA_UNIT]);
    const SemaType *number = &p->types[SEMA_INT], *text = &p->types[SEMA_STR];
    SemaVariant conflict[] = {{p->ok, &number, 1}, {p->ok, &text, 1}};
    ASSERT_TRUE(sema_symbol_set(p, conflict, 2) == NULL);
    SemaTag *bad = sema_tag(p, p->module, "bad");
    SemaVariant full[] = {{bad, &text, 1}, {p->ok, &number, 1}};
    const SemaType *outcome = sema_symbol_set(p, full, 2);
    ASSERT_EQ_INT(2, outcome->symbols->count);
    ASSERT_TRUE(sema_kind_part(p, outcome, SEMA_TAG_NEUTRAL) == number);
    ASSERT_TRUE(sema_has_errors(outcome));
  }
  TEST("all type positions and explicit values share normalization") {
    Result r =
        valid(&a, "Value = :ok[str]\nAgain = :ok[Value]\nNothing = :ok\n"
                  "Box { text: Again\nunit: Nothing }\n"
                  "identity(value: Value) -> Again { :ok[value] }\n"
                  "make() -> :ok[Box] { :ok[Box { text = identity(\"owned\"), "
                  "unit = :ok }] }\n"
                  "main() { let b: :ok[Box] = make()\n"
                  "let text: Value = :ok[b.text]\nprint(text) }");
    SemaProgram *p = r.program;
    ASSERT_TRUE(p->aliases[0].type == &p->types[SEMA_STR]);
    ASSERT_TRUE(p->aliases[1].type == p->aliases[0].type);
    ASSERT_TRUE(p->aliases[2].type == &p->types[SEMA_UNIT]);
    ASSERT_TRUE(p->structs[0].fields[0].type == &p->types[SEMA_STR]);
    ASSERT_TRUE(p->structs[0].fields[1].type == &p->types[SEMA_UNIT]);
    ASSERT_TRUE(p->functions[0].symbol.params[0]->type == p->aliases[0].type);
    ASSERT_TRUE(p->functions[1].written_type == &p->structs[0].type);
    IrProgram *ir = ir_lower(&a, p);
    ASSERT_TRUE(ir != NULL);
    if (ir)
      for (size_t i = 0; i < ir->count; i++)
        for (size_t j = 0; j < ir->functions[i].count; j++) {
          IrOp op = ir->functions[i].instructions[j].op;
          ASSERT_TRUE(op != IR_SYMBOL && op != IR_SYMBOL_PAYLOAD &&
                      op != IR_SYMBOL_TAG);
        }
    ASSERT_EQ_INT(0, p->symbol_set_count);
  }
  TEST("ordinary results cannot be matched as ok, in any call form") {
    const char *message = "Symbol patterns require a symbol input.";
    const char *prefixes[] = {
        "f() -> int { 7 }\n", "f() { 7 }\n", "f() -> :ok[int] { :ok[7] }\n",
        "f() -> str { \"text\" }\n", "f() -> :ok { :ok }\n"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(*prefixes); i++) {
      char text[512];
      snprintf(text, sizeof(text), "%smain() { f() |> { :ok[v] => () } }",
               prefixes[i]);
      rejects(&a, text, message, ":ok[v]");
    }
    rejects(&a,
            "Box { value: int\nread() -> int { self.value } }\n"
            "main() { let box = Box { value = 7 }\n"
            "box.read() |> { :ok[v] => () } }",
            message, ":ok[v]");
    rejects(&a,
            "double(n: int) -> int { n * 2 }\n"
            "main() { 7 |> double() |> { :ok[v] => () } }",
            message, ":ok[v]");
    rejects(&a, "main() { int('a') |> { :ok[v] => () } }", message, ":ok[v]");
    rejects(&a,
            "Data = :a | :b\nf() -> :ok[Data] { :a }\n"
            "main() { f() |> { :ok[v] => () } }",
            "Symbol pattern tag is not in the input set.", ":ok[v]");
  }
  TEST("ordinary call results are not wrapped by binding or argument context") {
    rejects(&a,
            "Maybe = :ok[int] | :none\nf(n: int) -> int { n }\n"
            "main() { let value: Maybe = f(7) }",
            "Binding value does not match its annotation.", "f(7)");
    rejects(&a,
            "Maybe = :ok[int] | :none\nf(n: int) -> int { n }\n"
            "take(value: Maybe) {}\nmain() { take(f(7)) }",
            "Function argument has the wrong type.", "f(7)");
    valid(&a, "Maybe = :ok[int] | :none\nf(n: int) -> int { n }\n"
              "take(value: Maybe) {}\n"
              "main() { let value: Maybe = :ok[f(7)]\n"
              "take(:ok[f(7)])\ntake(value) }");
  }
  TEST("exact contracts, partial annotations and normalized catch-alls") {
    valid(&a, "Data = :a | :b\n"
              "read(b: bool) -> :ok[Data] | :!bad { if b { ret :!bad }\n:a }\n"
              "f() -> int { read(false) |> { :!bad => 0\n"
              "other => other |> { :a => 1\n:b => 2 } } }\nmain() { f() }");
    valid(&a, "f() -> :ok[lng] | _ { ret :ok[4294967296] }\n"
              "g(b: bool) -> :ok[lng] | _ { if b { ret :!bad }\nf() }\n"
              "main() { print(str(f()))\ng(false)! }");
    rejects(&a, "f() -> int { ret :other }\nmain() {}",
            "Return value does not match the written return type.",
            "ret :other");
    rejects(&a, "f() -> :ok[int] | _ { ret false }\nmain() {}",
            "Return value does not match the written return type.",
            "ret false");
    rejects(&a, "f() -> :ok[int] { :ok[true] }\nmain() {}",
            "Function body does not match the written return type.", "{ :ok");
    rejects(&a, "f() -> :ok { :!bad }\nmain() {}",
            "An outcome cannot be discarded by a unit return.", ":!bad");
    rejects(&a, "f() -> () { if true { :!bad } else { () } }\nmain() {}",
            "An outcome cannot be discarded by a unit return.", ":!bad");
    valid(&a, "f() -> :ok | _ { :!bad }\n"
              "main() { f() |> { :!bad => ()\n:ok => () } }");
    valid(&a, "f() -> :ok[:!bad] { :ok[:!bad] }\n"
              "main() { f()? }");
    rejects(&a, "f() -> :ok | _ { :ok[7] }\nmain() {}",
            "Function body does not match the written return type.", "{ :ok");
  }
  TEST("normalization and contextual wrapping preserve readonly access") {
    const char *prefix = "Box { value: int }\n"
                         "Result = :ok[Box] | :!bad\n";
    char source[1024];
    snprintf(source, sizeof(source),
             "%sf(value: :ok[Box]) { value.value = 1 }\nmain() {}", prefix);
    rejects(&a, source, "Cannot mutate through an ordinary parameter; use &.",
            "value.value");
    snprintf(source, sizeof(source),
             "%sf(value: &:ok[Box]) { value.value = 1 }\nmain() {}", prefix);
    valid(&a, source);
    snprintf(source, sizeof(source),
             "%sf(value: Result) { value |> { :!bad => ()\n"
             "other => other.value = 1 } }\nmain() {}",
             prefix);
    rejects(&a, source, "Cannot mutate through an ordinary parameter; use &.",
            "other.value");
    snprintf(source, sizeof(source),
             "%sf(value: &Result) { value |> { :!bad => ()\n"
             "other => other.value = 1 } }\nmain() {}",
             prefix);
    valid(&a, source);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
