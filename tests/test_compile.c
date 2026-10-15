#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  Diagnostics errors;
  SemaProgram *program;
} Checked;
static Checked resolve(Arena *a, const char *text, const char *path) {
  Checked c;
  diagnostics_init(&c.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *source = source_create(a, path, text, strlen(text));
  TokenArray tokens = newline_filter(a, lexer_tokenize(a, source, &c.errors));
  Program *ast = parse_program(a, &names, tokens, &c.errors);
  c.program = sema_resolve(a, ast, &c.errors);
  return c;
}
static void invalid(Arena *a, const char *text, const char *message) {
  Checked c = resolve(a, text, "invalid.brf");
  sema_check(c.program, &c.errors);
  int found = 0;
  for (size_t i = 0; i < c.errors.count; i++)
    if (strstr(c.errors.items[i]->message, message))
      found = 1;
  ASSERT_TRUE(found);
  ASSERT_TRUE(!c.program->checked);
  ASSERT_TRUE(ir_lower(a, c.program) == NULL);
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("resolution links forward calls before types are checked") {
    Checked c = resolve(&a, "main() { hello() }\nhello() { print(\"Hi\") }",
                        "hello.brf");
    ASSERT_EQ_INT(0, c.errors.count);
    SemaExpr *call = c.program->functions[0].body->as.block.items[0];
    ASSERT_TRUE(call->type == NULL);
    ASSERT_TRUE(call->as.call.target == &c.program->functions[1].symbol);
    ASSERT_TRUE(ir_lower(&a, c.program) == NULL);
    sema_check(c.program, &c.errors);
    ASSERT_EQ_INT(0, c.errors.count);
    ASSERT_TRUE(c.program->checked);
    ASSERT_TRUE(call->type == &c.program->unit_type);
    SemaExpr *print = c.program->functions[1].body->as.block.items[0];
    ASSERT_TRUE(print->as.call.args[0]->type == &c.program->str_type);
  }
  TEST("name diagnostics collect independent errors and duplicate notes") {
    Checked c =
        resolve(&a, "main() { missing()\nother() }\nmain() {}", "bad.brf");
    ASSERT_EQ_INT(3, c.errors.count);
    ASSERT_TRUE(c.errors.items[0]->notes != NULL);
    ASSERT_TRUE(c.errors.items[0]->notes->span.file != NULL);
    invalid(&a, "helper() {}", "must define main");
    invalid(&a, "main() {}\nprint() {}", "reserved");
  }
  TEST("unsupported features are rejected rather than ignored") {
    invalid(&a, "main() { 1 }", "expression is not implemented");
    invalid(&a, "main() { let a = \"x\" }", "Local declarations");
    invalid(&a, "main(x: str) {}", "parameters are not implemented");
    invalid(&a, "main(): str { \"x\" }", "written return type");
    invalid(&a, "pub User {}\nmain() {}", "Only function declarations");
    invalid(&a, "main() { main<int>() }", "Generic calls");
    invalid(&a, "main() { Module.f() }", "plain function names");
    invalid(&a, "main() { print(\"x\") |> f() }",
            "expression is not implemented");
  }
  TEST("prior frontend errors prevent lowering a surviving partial tree") {
    invalid(&a, "main() { @ }", "Unexpected character");
    invalid(&a, "main() { let = 1 }", "Expected a binding name");
  }
  TEST("call graph descent is bounded and IR instruction lists grow") {
    char text[16384];
    size_t length = (size_t)snprintf(text, sizeof(text), "main() { f0() }\n");
    for (size_t i = 0; i < 140; i++)
      length += (size_t)snprintf(text + length, sizeof(text) - length,
                                 "f%zu() { f%zu() }\n", i, i + 1);
    snprintf(text + length, sizeof(text) - length, "f140() {}\n");
    invalid(&a, text, "call graph depth limit");
    length = (size_t)snprintf(text, sizeof(text), "main() {\n");
    for (size_t i = 0; i < 100; i++)
      length += (size_t)snprintf(text + length, sizeof(text) - length,
                                 "print(\"x\")\n");
    snprintf(text + length, sizeof(text) - length, "}\n");
    Checked c = resolve(&a, text, "large.brf");
    sema_check(c.program, &c.errors);
    ASSERT_EQ_INT(0, c.errors.count);
    IrProgram *p = ir_lower(&a, c.program);
    ASSERT_TRUE(p != NULL);
    if (p) {
      ASSERT_EQ_INT(201, p->functions[0].count);
      ASSERT_EQ_INT(100, p->functions[0].temporaries);
    }
  }
  TEST("arity and return checking prevent invalid C emission") {
    invalid(&a, "main() { print() }", "exactly one str");
    invalid(&a, "main() { print(()) }", "expects a str");
    invalid(&a, "main() { f(\"x\") }\nf() {}", "Wrong number");
    invalid(&a, "main() { \"x\" }", "Value-returning functions");
    invalid(&a, "main() { ret \"x\" }", "unit return values");
    invalid(&a, "main() { main() }", "Recursive functions");
    invalid(&a, "main() { f() }\nf() { main() }", "Recursive functions");
  }
  TEST("lowering decodes bytes and sequences string temporaries before calls") {
    Checked c = resolve(
        &a, "main() { print(\"prefix\")\nprint(\"a\\0b\\n\\t\\\\\\\"\\'é\") }",
        "bytes.brf");
    sema_check(c.program, &c.errors);
    ASSERT_EQ_INT(0, c.errors.count);
    IrProgram *p = ir_lower(&a, c.program);
    ASSERT_TRUE(p != NULL);
    if (p) {
      IrFunction *f = &p->functions[0];
      ASSERT_EQ_INT(5, f->count);
      ASSERT_EQ_INT(IR_STRING, f->instructions[0].op);
      ASSERT_EQ_INT(IR_CALL, f->instructions[1].op);
      ASSERT_EQ_INT(IR_STRING, f->instructions[2].op);
      const unsigned char expected[] = {'a',  0,   'b',  '\n', '\t',
                                        '\\', '"', '\'', 0xc3, 0xa9};
      ASSERT_EQ_INT(sizeof(expected), f->instructions[2].as.string.length);
      ASSERT_TRUE(memcmp(expected, f->instructions[2].as.string.bytes,
                         sizeof(expected)) == 0);
      ASSERT_EQ_INT(0, f->instructions[2].as.string.bytes[sizeof(expected)]);
      ASSERT_EQ_INT(1, f->instructions[3].as.call.argument);
      ASSERT_EQ_INT(IR_RETURN, f->instructions[4].op);
    }
  }
  TEST("explicit unit discards the tail; early return truncates only IR") {
    Checked c =
        resolve(&a,
                "main(): () { \"discard\" }\nf() { ret ()\n\"unreachable\" }\n"
                "g() { ret print(\"before\")\n\"unreachable\" }",
                "unit.brf");
    sema_check(c.program, &c.errors);
    ASSERT_EQ_INT(0, c.errors.count);
    IrProgram *p = ir_lower(&a, c.program);
    ASSERT_TRUE(p != NULL);
    if (p) {
      ASSERT_EQ_INT(2, p->functions[0].count);
      ASSERT_EQ_INT(1, p->functions[1].count);
      ASSERT_EQ_INT(IR_RETURN, p->functions[1].instructions[0].op);
      ASSERT_EQ_INT(3, p->functions[2].count);
      ASSERT_EQ_INT(IR_CALL, p->functions[2].instructions[1].op);
      ASSERT_EQ_INT(IR_RETURN, p->functions[2].instructions[2].op);
    }
    invalid(&a, "main() { ret\nmissing() }", "Unknown function");
  }
  TEST("emitted C is self-contained, mangled, and source-mapped") {
    Checked c = resolve(&a, "main() { auto() }\nauto() { print(\"Hi\") }",
                        "quote\"and\\slash.brf");
    sema_check(c.program, &c.errors);
    ASSERT_EQ_INT(0, c.errors.count);
    IrProgram *p = ir_lower(&a, c.program);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out && p) {
      ASSERT_TRUE(emit_c(out, p));
      ASSERT_EQ_INT(0, fflush(out));
      rewind(out);
      char text[8192] = {0};
      size_t count = fread(text, 1, sizeof(text) - 1, out);
      ASSERT_TRUE(count > 0);
      ASSERT_TRUE(strstr(text, "brt_print(brt_tmp_0)") != NULL);
      ASSERT_TRUE(strstr(text, "void brf_main(void)") != NULL);
      ASSERT_TRUE(strstr(text, "void brf_auto(void)") != NULL);
      ASSERT_TRUE(strstr(text, "  brf_auto();") != NULL);
      ASSERT_TRUE(strstr(text, "int main(void)") != NULL);
      ASSERT_TRUE(strstr(text, "quote\\\"and\\\\slash.brf") != NULL);
      ASSERT_TRUE(strstr(text, "brt_bytes_0[] = \"Hi\";") != NULL);
      ASSERT_TRUE(strstr(text, "brt_literal_0 = {2, brt_bytes_0};") != NULL);
      ASSERT_TRUE(strstr(text, "brf_fn_0") == NULL);
      fclose(out);
    } else if (out)
      fclose(out);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
