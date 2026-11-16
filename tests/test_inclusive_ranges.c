#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/internal.h"
#include "test_utils.h"
#include <math.h>

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
  SourceFile *s = source_create(a, "inclusive.brf", text, strlen(text));
  TokenArray raw = lexer_tokenize(a, s, &r.errors);
  TokenArray tokens = newline_filter(a, raw);
  size_t bytes = tokens.count * sizeof(Token);
  Token *saved = arena_alloc(a, bytes);
  memcpy(saved, tokens.items, bytes);
  r.ast = parse_program(a, &names, tokens, &r.errors);
  ASSERT_TRUE(!memcmp(saved, tokens.items, bytes));
  r.program = sema_resolve(a, r.ast, &r.errors);
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
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  const SemaType **types = arena_calloc(a, r.program->count, sizeof(*types));
  for (size_t i = 0; i < r.program->count; i++)
    types[i] = r.program->functions[i].symbol.return_type;
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
    ASSERT_TRUE(r.program->checked && !strcmp(text, r.program->module->text));
    for (size_t i = 0; i < r.program->count; i++)
      ASSERT_TRUE(types[i] == r.program->functions[i].symbol.return_type);
  }
  IrProgram *ir = ir_lower(a, r.program);
  ASSERT_TRUE(ir != NULL);
  if (ir) {
    ir_insert_rc(a, ir);
    size_t n = ir->functions[ir->entry].count;
    ir_insert_rc(a, ir);
    ASSERT_EQ_INT(n, ir->functions[ir->entry].count);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static void accepts(Arena *a, const char *text) {
  for (int crlf = 0; crlf < 2; crlf++)
    (void)valid(a, lines(a, text, crlf));
}
static void reject(Arena *a, const char *source, const char *message,
                   const char *site) {
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
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        const char *at = strstr(text, lines(a, site, crlf));
        ASSERT_TRUE(at != NULL);
        if (at)
          ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_TRUE(!d->fixes);
      }
    }
  }
}
static SemaExpr *match(Result *r) {
  SemaExpr *pipe = r->program->functions[0].body->as.block.items[0];
  return pipe->as.block.items[1];
}
int main(void) {
  Arena a;
  arena_init(&a);
  const char *types[] = {"shrt", "ushrt", "int", "uint", "lng", "ulng"};
  const char *min[] = {
      "-32768", "0", "-2147483648", "0", "-9223372036854775808", "0"};
  const char *max[] = {"32767",
                       "65535",
                       "2147483647",
                       "4294967295",
                       "9223372036854775807",
                       "18446744073709551615"};
  TEST("inclusive tokens are longest matches and continue lines without "
       "rewriting") {
    Diagnostics errors;
    diagnostics_init(&errors, &a);
    SourceFile *s = source_create(&a, "tokens.brf", ". .. ..= .. = .(", 16);
    TokenArray raw = lexer_tokenize(&a, s, &errors);
    TokenType expected[] = {TOK_PERIOD,  TOK_DOT_DOT, TOK_DOT_DOT_EQUAL,
                            TOK_DOT_DOT, TOK_EQUAL,   TOK_DOT_LPAREN,
                            TOK_EOF};
    ASSERT_EQ_INT(0, errors.count);
    ASSERT_EQ_INT(7, raw.count);
    for (size_t i = 0; i < 7 && i < raw.count; i++)
      ASSERT_EQ_INT(expected[i], raw.items[i].type);
    ASSERT_EQ_INT(3, raw.items[2].span.end - raw.items[2].span.start);
    ASSERT_EQ_INT(
        0, strcmp("TOK_DOT_DOT_EQUAL", token_type_name(TOK_DOT_DOT_EQUAL)));
    for (int crlf = 0; crlf < 2; crlf++) {
      Result r = valid(&a, lines(&a,
                                 "main() { for i in 1 + 2..=\n3 + 4 {}\n"
                                 "1 |> { 0..=1 => ()\n_ => () } }",
                                 crlf));
      Expr *loop = r.ast->items[0]->as.function.body->as.block.items[0].as.expr;
      ASSERT_TRUE(loop->as.for_loop.iterable->as.binary.inclusive);
      Expr *pattern =
          r.ast->items[0]->as.function.body->as.block.items[1].as.expr;
      ASSERT_TRUE(
          pattern->as.pipe.stage->as.match.arms[0].pattern->as.range.inclusive);
      FILE *out = tmpfile();
      ASSERT_TRUE(out != NULL);
      if (out) {
        ast_print(out, r.ast);
        rewind(out);
        char printed[2048];
        size_t n = fread(printed, 1, sizeof(printed) - 1, out);
        printed[n] = 0;
        ASSERT_TRUE(
            strstr(printed, "(..= (+ (int 1) (int 2)) (+ (int 3) (int 4)))"));
        ASSERT_TRUE(strstr(printed, "(range-inclusive (int 0) (int 1))"));
        fclose(out);
      }
    }
  }
  TEST("every integer domain including ulng maximum normalizes inclusively") {
    for (size_t i = 0; i < 6; i++)
      for (int crlf = 0; crlf < 2; crlf++) {
        char source[1024];
        snprintf(source, sizeof(source),
                 "f(n: %s) { n |> { %s..=%s => 1 } }\nmain() {}", types[i],
                 min[i], max[i]);
        Result r = valid(&a, lines(&a, source, crlf));
        SemaArm *arm = &match(&r)->as.match.arms[0];
        const SemaType *type = &r.program->types[SEMA_SHRT + i];
        ASSERT_TRUE(arm->inclusive && arm->useful);
        ASSERT_TRUE(arm->first == 0 &&
                    arm->last == (UINT64_MAX >> (64 - type->bits)));
        ASSERT_TRUE(arm->first_value->type == type &&
                    arm->last_value->type == type);
      }
    for (size_t i = 0; i < 6; i++) {
      char source[1024], site[128];
      snprintf(source, sizeof(source),
               "f(n: %s) { n |> { %s..=%s => 1\n%s => 2\n_ => 0 } }\nmain() {}",
               types[i], max[i], max[i], max[i]);
      snprintf(site, sizeof(site), "%s => 2", max[i]);
      reject(&a, source, "Unreachable match arm.", site);
    }
  }
  TEST("inclusive character and floating endpoints include zero equivalence") {
    accepts(&a, "f(c: chr) { c |> { 'a'..='z' => 1\n_ => 0 } }\nmain() {}");
    accepts(&a, "f(c: chr) { c |> { '\\0'..='\xff' => 1 } }\nmain() {}");
    for (unsigned single = 0; single < 2; single++) {
      char source[1024];
      snprintf(source, sizeof(source),
               "f(n: %s) { n |> { -0.0..=0.0 => 1\n_ => 0 } }\nmain() {}",
               single ? "flt" : "dbl");
      Result r = valid(&a, source);
      SemaArm *arm = &match(&r)->as.match.arms[0];
      ASSERT_TRUE(arm->last == arm->first + 1);
      ASSERT_TRUE(signbit(arm->first_value->real) &&
                  !signbit(arm->last_value->real));
    }
    reject(
        &a,
        "f(n: dbl) { n |> { -0.0..=0.0 => 1\n0.0 => 2\n_ => 0 } }\nmain() {}",
        "Unreachable match arm.", "0.0 => 2");
    reject(&a, "f(n: dbl) { n |> { -1.0..=1.0 => 1 } }\nmain() {}",
           "Match is not exhaustive; add a catch-all for NaN.", "n |>");
    reject(&a, "f(n: int) { n |> { 5..=4 => 1\n_ => 0 } }\nmain() {}",
           "Unreachable match arm.", "5..=4");
    accepts(&a, "f(n: int) { n |> { 5..=5 => 1\n_ => 0 } }\nmain() {}");
  }
  TEST("inclusive loop continue reaches exhaustion before any checked "
       "increment") {
    for (size_t i = 0; i < 12; i++) {
      char source[1024];
      size_t k = i % 6;
      if (i < 6)
        snprintf(source, sizeof(source),
                 "main() { for i in %s(%s)..=%s { continue } }", types[k],
                 max[k], max[k]);
      else
        snprintf(source, sizeof(source),
                 "main() { for i in %s..=%s(%s) { continue } }", max[k],
                 types[k], max[k]);
      Result r = valid(&a, source);
      SemaExpr *loop = r.program->functions[0].body->as.block.items[0];
      ASSERT_TRUE(loop->as.for_loop.inclusive);
      ASSERT_EQ_INT(SEMA_SHRT + k, loop->as.for_loop.variable->type->kind);
      IrProgram *ir = ir_lower(&a, r.program);
      ASSERT_TRUE(ir != NULL);
      if (!ir)
        continue;
      IrFunction *f = &ir->functions[ir->entry];
      size_t entry = SIZE_MAX, exhausted = SIZE_MAX, branch = SIZE_MAX,
             add = SIZE_MAX, step = SIZE_MAX;
      const char *at = strstr(source, "continue");
      for (size_t j = 0; j < f->count; j++) {
        IrInstruction *in = &f->instructions[j];
        if (in->op == IR_JUMP && in->span.start == (size_t)(at - source))
          step = in->label;
        if (in->op == IR_BINARY && in->as.binary == BINARY_LE)
          entry = j;
        if (in->op == IR_BINARY && in->as.binary == BINARY_NE)
          exhausted = j;
        if (in->op == IR_BINARY && in->as.binary == BINARY_ADD)
          add = j;
        if (in->op == IR_BRANCH_FALSE && exhausted != SIZE_MAX && j > exhausted)
          branch = j;
      }
      ASSERT_TRUE(entry < exhausted && exhausted < branch && branch < add);
      size_t label = SIZE_MAX;
      for (size_t j = 0; j < f->count; j++)
        if (f->instructions[j].op == IR_LABEL &&
            f->instructions[j].label == step)
          label = j;
      ASSERT_TRUE(step != SIZE_MAX && label < exhausted);
      if (entry != SIZE_MAX && exhausted != SIZE_MAX)
        ASSERT_TRUE(f->instructions[exhausted].b == f->instructions[entry].b);
    }
    Result old = valid(&a, "main() { for i in 0..2 { continue } }");
    IrProgram *ir = ir_lower(&a, old.program);
    unsigned lt = 0, le = 0, ne = 0;
    for (size_t i = 0; i < ir->functions[ir->entry].count; i++) {
      IrInstruction *in = &ir->functions[ir->entry].instructions[i];
      if (in->op == IR_BINARY) {
        lt += in->as.binary == BINARY_LT;
        le += in->as.binary == BINARY_LE;
        ne += in->as.binary == BINARY_NE;
      }
    }
    ASSERT_EQ_INT(1, lt);
    ASSERT_EQ_INT(0, le);
    ASSERT_EQ_INT(0, ne);
  }
  TEST(
      "loop bounds and integer operand restrictions retain ordinary checking") {
    accepts(
        &a,
        "f(lower: lng, upper: lng) { for n in lower..=upper {} }\nmain() {}");
    reject(&a, "f(a: shrt, b: ulng) { for i in a..=b {} }\nmain() {}",
           "Range bounds must have the same type.", "for i");
    reject(&a, "main() { for i in 0..=1 { i = 2 } }",
           "Cannot reassign an immutable binding.", "i = 2");
    reject(&a, "main() { let value = 0..=1 }",
           "Ranges are only supported in for loops.", "0..=1");
    reject(&a, "main() { for i in 0..=1 {}\ni }", "Unknown binding.", "i }");
  }
  TEST("mixed ranges cannot chain and invalid patterns have exact primaries") {
    const char *ranges[] = {"0..=1..2", "0..1..=2", "0..=1..=2"};
    const char *sites[] = {"..2", "..=2", "..=2"};
    for (size_t i = 0; i < 3; i++) {
      char source[512];
      snprintf(source, sizeof(source), "main() { for n in %s {} }", ranges[i]);
      reject(&a, source, "Range operators do not chain.", sites[i]);
    }
    reject(&a, "f(n: bool) { n |> { false..=false => 1\n_ => 0 } }\nmain() {}",
           "Range and comparison patterns require numbers or chr.",
           "false..=false");
    reject(&a, "f(n: int) { n |> { 0..=2.0 => 1\n_ => 0 } }\nmain() {}",
           "Pattern value has the wrong type.", "2.0");
    reject(
        &a,
        "f(lower: int, n: int) { n |> { lower..=2 => 1\n_ => 0 } }\nmain() {}",
        "Non-literal range pattern bounds are not implemented yet.",
        "lower..=");
    reject(
        &a,
        "f(lower: int, n: int) { n |> { lower..2 => 1\n_ => 0 } }\nmain() {}",
        "Non-literal range pattern bounds are not implemented yet.",
        "lower..2");
    reject(
        &a,
        "f(upper: int, n: int) { n |> { -1..upper => 1\n_ => 0 } }\nmain() {}",
        "Non-literal range pattern bounds are not implemented yet.",
        "upper =>");
    reject(
        &a,
        "f(upper: int, n: int) { n |> { 0..=upper => 1\n_ => 0 } }\nmain() {}",
        "Non-literal range pattern bounds are not implemented yet.",
        "upper =>");
  }
  TEST("inclusive syntax reuses conservative flow facts and lexical cleanup") {
    accepts(&a, "main() { for i in 0..=2 { let n: int\nn = i\ncontinue } }");
    reject(&a, "main() { let n: int\nfor i in 0..=0 { n = i } }",
           "A declared let binding can only be assigned once on every path.",
           "n = i");
    reject(&a, "main() { var n: int\nfor i in 0..=0 { n = i }\nprint(str(n)) }",
           "Binding must be assigned before it is read.", "n))");
    accepts(
        &a,
        "Box { text: str }\nf(flag: bool) { for outer in 0..=2 { let box: Box\n"
        "for inner in 0..=2 { if flag { continue }\nbox = Box { text = "
        "\"owned\" }\nret box } }\nBox { text = \"none\" } }\nmain() {}");
  }
  TEST("inclusive and half-open character intervals agree with exhaustive "
       "oracle") {
    Result r = check(&a, "main() {}");
    const SemaType *type = &r.program->types[SEMA_CHR];
    uint32_t random = 71;
    for (unsigned trial = 0; trial < 400; trial++) {
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      unsigned lo = trial < 2 ? 0 : (random >> 24);
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      unsigned hi = trial < 2 ? 255 : (random >> 24);
      SemaExpr input = {.type = type},
               lower = {.type = type, .kind = SEMA_EXPR_CHAR, .integer = lo},
               upper = {.type = type, .kind = SEMA_EXPR_CHAR, .integer = hi};
      SemaArm arms[2] = {{.kind = PAT_RANGE,
                          .lower = &lower,
                          .upper = &upper,
                          .inclusive = (int)(trial & 1)},
                         {.kind = PAT_WILDCARD}};
      SemaExpr m = {.kind = SEMA_EXPR_MATCH};
      m.as.match.input = &input;
      m.as.match.arms = arms;
      m.as.match.count = 2;
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      sema_match_coverage(r.program, &m, &errors);
      unsigned matched = 0;
      for (unsigned value = 0; value < 256; value++) {
        int wanted =
            value >= lo && (arms[0].inclusive ? value <= hi : value < hi);
        int actual =
            arms[0].useful && value >= arms[0].first && value <= arms[0].last;
        ASSERT_EQ_INT(wanted, actual);
        matched += (unsigned)wanted;
      }
      ASSERT_EQ_INT(matched != 0, arms[0].useful);
      ASSERT_EQ_INT(matched != 256, arms[1].useful);
      ASSERT_EQ_INT(matched == 0 || matched == 256, errors.count);
    }
  }
  arena_destroy(&a);
  RUN_TESTS();
}
