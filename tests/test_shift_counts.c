#include "compile_support.h"

static void reject(Arena *a, const char *source, const char *message,
                   const char *site) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = lines(a, source, crlf);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        if (!r.program->resolved)
          r = check(a, text); // Resolution errors require a fresh resolve.
        else {
          diagnostics_init(&r.errors, a);
          sema_check(r.program, &r.errors);
        }
      }
      if (r.errors.count != 1) {
        fprintf(stderr, "Unexpected diagnostics on check %u:\n%s\n", repeat,
                text);
        diagnostics_print(&r.errors, stderr);
      }
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        const char *at = strstr(text, site);
        ASSERT_TRUE(at != NULL);
        if (at)
          ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_TRUE(!d->fixes);
      }
    }
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  const char *types[] = {"shrt", "ushrt", "int", "uint", "lng", "ulng"};
  TEST("all integer type pairs preserve left result and independent IR count") {
    for (size_t left = 0; left < 6; left++)
      for (size_t right = 0; right < 6; right++)
        for (unsigned op = 0; op < 2; op++)
          for (int crlf = 0; crlf < 2; crlf++) {
            char source[512];
            snprintf(source, sizeof(source),
                     "f(value: %s, count: %s) { value %s count }\nmain() {}\n",
                     types[left], types[right], op ? ">>" : "<<");
            Result r = valid(&a, lines(&a, source, crlf));
            SemaFunction *f = &r.program->functions[0];
            const SemaType *lt = &r.program->types[SEMA_SHRT + left];
            const SemaType *rt = &r.program->types[SEMA_SHRT + right];
            ASSERT_TRUE(f->symbol.return_type == lt);
            SemaExpr *shift = f->body->as.block.items[0];
            ASSERT_TRUE(shift->type == lt && shift->as.binary.left->type == lt);
            ASSERT_TRUE(shift->as.binary.right->type == rt);
            ASSERT_TRUE(!shift->adapt && !shift->as.binary.common);
            IrProgram *ir = ir_lower(&a, r.program);
            ASSERT_TRUE(ir != NULL);
            if (!ir)
              continue;
            unsigned found = 0;
            IrFunction *function = &ir->functions[0];
            for (size_t i = 0; i < function->count; i++) {
              IrInstruction *in = &function->instructions[i];
              if (in->op != IR_BINARY)
                continue;
              found++;
              ASSERT_EQ_INT(op ? BINARY_RSHIFT : BINARY_LSHIFT, in->as.binary);
              ASSERT_TRUE(in->type == lt);
              ASSERT_TRUE(function->temporary_types[in->a] == lt);
              ASSERT_TRUE(function->temporary_types[in->b] == rt);
              ASSERT_TRUE(function->temporary_types[in->result] == lt);
            }
            ASSERT_EQ_INT(1, found);
            ir_insert_rc(&a, ir);
            size_t count = function->count;
            ir_insert_rc(&a, ir);
            ASSERT_EQ_INT(count, function->count);
            char *c = emit_text(&a, ir);
            if (c)
              ASSERT_TRUE((strstr(c, ") brt_panic(\"invalid shift count\");") !=
                           NULL) == (left != right));
          }
  }
  TEST("only the left operand and its result context anchor shift types") {
    struct {
      const char *source;
      SemaTypeKind left, right;
    } cases[] = {
        {"f(count: ulng) { 1 << count }\nmain() {}", SEMA_INT, SEMA_ULNG},
        {"f(value: ulng) { value << 1 }\nmain() {}", SEMA_ULNG, SEMA_INT},
        {"f(count: int) -> shrt { 1 << count }\nmain() {}", SEMA_SHRT,
         SEMA_INT},
        {"f(count: ulng) { (1 << count) << count }\nmain() {}", SEMA_INT,
         SEMA_ULNG},
        {"f() { 1 << lng(2) }\nmain() {}", SEMA_INT, SEMA_LNG},
        {"Left = shrt\nCount = ulng\nf(value: Left, count: Count) { value << "
         "count }\nmain() {}",
         SEMA_SHRT, SEMA_ULNG}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      for (int crlf = 0; crlf < 2; crlf++) {
        Result r = valid(&a, lines(&a, cases[i].source, crlf));
        SemaExpr *shift = r.program->functions[0].body->as.block.items[0];
        ASSERT_EQ_INT(cases[i].left, shift->type->kind);
        ASSERT_EQ_INT(cases[i].left, shift->as.binary.left->type->kind);
        ASSERT_EQ_INT(cases[i].right, shift->as.binary.right->type->kind);
      }
    reject(&a,
           "f(value: shrt, count: ulng) -> lng { value << count }\nmain() {}",
           "Function body does not match the written return type.", "{ value");
    reject(&a, "f(count: ulng) { f(count) << count }\nmain() {}",
           "Cannot infer recursive return type; write a return type.",
           "f(count:");
  }
  TEST("counts default independently even for unsigned or narrow left values") {
    const char *bodies[] = {"f(value: ulng) { value << -1 }\nmain() {}",
                            "f(value: shrt) { value >> 40000 }\nmain() {}",
                            "f(value: lng) { value << 32 }\nmain() {}"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      Result r = valid(&a, bodies[i]);
      SemaExpr *shift = r.program->functions[0].body->as.block.items[0];
      ASSERT_EQ_INT(SEMA_INT, shift->as.binary.right->type->kind);
    }
    reject(&a, "f(value: ulng) { value << 2147483648 }\nmain() {}",
           "Number literal is outside the range of its type.", "2147483648");
  }
  TEST("noninteger operands reject at their own sites without type guesses") {
    const char *bad[] = {"true", "'a'", "1.5", "\"text\"", "()", ":tag"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char source[512];
      snprintf(source, sizeof(source), "f() { 1 << %s }\nmain() {}", bad[i]);
      reject(&a, source, "A shift count must be an integer.",
             i == 4 ? "() }" : bad[i]);
      snprintf(source, sizeof(source), "f() { %s >> 1 }\nmain() {}", bad[i]);
      reject(&a, source, "The shifted value must be an integer.",
             i == 4 ? "() >>" : bad[i]);
    }
    reject(&a, "Box { n: int }\nf() { 1 << Box { n = 2 } }\nmain() {}",
           "A shift count must be an integer.", "Box { n =");
    reject(&a, "f(n: int, c: flt) { n << c }\nmain() {}",
           "A shift count must be an integer.", "c }");
    reject(&a, "f(n: int, c: dbl) { n >> c }\nmain() {}",
           "A shift count must be an integer.", "c }");
  }
  TEST("other operators still reject mixed integer types") {
    const char *ops[] = {"+", "&", "**", "<"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); i++) {
      char source[512];
      snprintf(source, sizeof(source),
               "f(value: shrt, count: ulng) { value %s count }\nmain() {}",
               ops[i]);
      reject(&a, source,
             "Operator operands must have the same type; numeric conversions "
             "are explicit.",
             "value ");
    }
  }
  TEST("error recovery and inference do not use a count as a result anchor") {
    reject(&a, "f(n: int) { n << missing }\nmain() {}", "Unknown binding.",
           "missing");
    reject(&a, "f(n: int) { n << int(\"text\") }\nmain() {}",
           "A call that can fail must be handled with ?, !, or a match.",
           "int(\"text\")");
    reject(&a, "f(n: int) { let c = int(\"text\")\nn << c }\nmain() {}",
           "Outcome binding must be handled with ?, !, a match, or return.",
           "c }");
    const char *one = "one(n: int, c: ulng) { if n == 0 { ret 1 }\n"
                      "two(n - 1, c) << c }\n";
    const char *two = "two(n: int, c: ulng) { if n == 0 { ret 2 }\n"
                      "one(n - 1, c) >> c }\n";
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      char source[1024];
      snprintf(source, sizeof(source), "%s%smain() {}", reverse ? two : one,
               reverse ? one : two);
      Result r = valid(&a, source);
      ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
      ASSERT_TRUE(r.program->functions[0].symbol.return_type ==
                  r.program->functions[1].symbol.return_type);
    }
  }
  arena_destroy(&a);
  RUN_TESTS();
}
