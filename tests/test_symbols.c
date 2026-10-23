#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

typedef struct {
  Diagnostics errors;
  SemaProgram *program;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "symbols.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  if (r.program->checked) {
    IrProgram *ir = ir_lower(a, r.program);
    ASSERT_TRUE(ir != NULL);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(!emit_c(out, ir));
      ir_insert_rc(a, ir);
      size_t count = ir->functions[0].count;
      ir_insert_rc(a, ir);
      ASSERT_EQ_INT(count, ir->functions[0].count);
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static Result invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    found |= strstr(r.errors.items[i]->message, message) != NULL;
  if (!found)
    diagnostics_print(&r.errors, stderr);
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  return r;
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("aliases flatten, deduplicate and intern independently of listing "
       "order") {
    Result r = valid(&a, "type All = Sym<Other, :b(int), :a>\n"
                         "type Other = Sym<:a, :b(int), :a>\n"
                         "type Again = Sym<:b(int), :a>\nmain() {}\n");
    const SemaType *type = r.program->aliases[0].type;
    ASSERT_TRUE(type == r.program->aliases[1].type);
    ASSERT_TRUE(type == r.program->aliases[2].type);
    ASSERT_EQ_INT(2, type->symbols->count);
    ASSERT_EQ_INT(3, r.program->tag_count); // Includes the program's :ok tag.
    ASSERT_TRUE(type->symbols->variants[0].tag->id <
                type->symbols->variants[1].tag->id);
    ASSERT_TRUE(sema_variant(type, sema_tag(r.program, "a"))->payload == NULL);
    ASSERT_TRUE(sema_variant(type, sema_tag(r.program, "b"))->payload ==
                &r.program->types[SEMA_INT]);
  }
  TEST("all payload types, forward structs, nested symbols and unit payloads") {
    valid(&a, "type Choice = Sym<:object(Node), :nested(Sym<:yes, :no>), "
              ":unit(()), :empty>\n"
              "Node { choice: Choice }\n"
              "main() { let n = Node { choice: :empty }\n"
              "let c: Choice = :object(n)\nc |> { :object(x) => x.choice\n"
              "other => other }\n() }");
    valid(&a, "main() { let a: Sym<:value(lng)> = :value(4294967296)\n"
              "let b: Sym<:value(flt)> = :value(0.1)\n"
              "let c: Sym<:value(())> = :value(())\n"
              "c |> { :value(x) => x } }");
    valid(&a, "main() { :outer(:inner(\"a\" + \"b\")) |> { :outer(x) => "
              "x |> { :inner(s) => print(s) } } }");
    valid(
        &a,
        "type Inner = Sym<:a(str), :b>\n"
        "f(b: bool): Sym<:outer(Inner)> { if b { :outer(:a(\"x\")) } "
        "else { :outer(:b) } }\n"
        "main() { let x: Sym<:outer(Inner)> = :outer(:a(\"x\"))\n"
        "let y = :b\nlet z: Sym<:outer(Inner)> = :outer(y)\n"
        "f(true) |> { :outer(inner) => inner |> { :a(_) => ()\n:b => () } } }");
    invalid(&a, "main() { let a: Sym<:a> = :a(()) }", "does not match");
    invalid(&a, "main() { let a: Sym<:a(())> = :a }", "does not match");
    invalid(&a, "main() { let a: Sym<:a(lng)> = :a(true) }", "does not match");
  }
  TEST("packing, widening and payload reads independently acquire owners") {
    Result r = valid(
        &a,
        "make(): Sym<:a(str), :b> { :a(\"x\" + \"y\") }\n"
        "read(s: Sym<:a(str), :b>) { s |> { :a(text) => text\n:b => \"\" } }\n"
        "main() { read(make()) }");
    IrProgram *ir = ir_lower(&a, r.program);
    ir_insert_rc(&a, ir);
    unsigned seen = 0;
    for (size_t i = 0; i < ir->count; i++) {
      IrFunction *f = &ir->functions[i];
      for (size_t j = 0; j < f->count; j++) {
        IrInstruction in = f->instructions[j];
        if (in.op != IR_SYMBOL && in.op != IR_SYMBOL_CAST &&
            in.op != IR_SYMBOL_PAYLOAD)
          continue;
        seen |= in.op == IR_SYMBOL ? 1u : in.op == IR_SYMBOL_CAST ? 2u : 4u;
        if (!j || j + 1 >= f->count) {
          FAIL("missing ownership operations around symbol definition");
          continue;
        }
        ASSERT_EQ_INT(IR_DROP_TEMP, f->instructions[j - 1].op);
        ASSERT_EQ_INT(in.result, f->instructions[j - 1].a);
        ASSERT_EQ_INT(IR_RETAIN, f->instructions[j + 1].op);
        ASSERT_EQ_INT(in.result, f->instructions[j + 1].a);
      }
    }
    ASSERT_EQ_INT(7, seen);
  }
  TEST("subset assignment, fields, parameters, returns and branch joins") {
    Result r =
        valid(&a, "type Tier = Sym<:bronze, :silver, :gold>\n"
                  "Box { tier: Tier }\n"
                  "choose(b: bool) { if b { :bronze } else { :gold } }\n"
                  "forward(b: bool): Tier { if b { ret :silver }\nchoose(b) }\n"
                  "same(x: Tier): Tier { x }\n"
                  "main(): () { var x: Tier = :bronze\nx = :gold\n"
                  "let box = Box { tier: :silver }\nbox.tier = choose(false)\n"
                  "forward(true) |> same() |> { Tier => () } }");
    ASSERT_EQ_INT(2,
                  r.program->functions[0].symbol.return_type->symbols->count);
    invalid(&a, "f(x: Sym<:a>) {}\nmain() { f(:b) }",
            "argument has the wrong type");
    invalid(&a, "f(b: bool): Sym<:a> { if b { ret :b }\n:a }\nmain() {}",
            "Return value");
    invalid(&a, "f(): Sym<:a> { :b }\nmain() {}", "body does not match");
    invalid(&a, "main() { var x = :a\nx = :b }", "Assignment value");
    invalid(&a, "B { x: Sym<:a> }\nmain() { B { x: :b } }",
            "initializer has the wrong type");
  }
  TEST("sets grow across recursive SCCs without locking out later tags") {
    Result r = valid(
        &a, "f(n: int) { if n == 0 { :a } else { g(n - 1) } }\n"
            "g(n: int) { if n == 0 { :b } else { f(n - 1) } }\nmain() {}\n");
    const SemaType *type = r.program->functions[0].symbol.return_type;
    ASSERT_TRUE(type == r.program->functions[1].symbol.return_type);
    ASSERT_EQ_INT(2, type->symbols->count);
    size_t count = r.program->symbol_set_count;
    sema_check(r.program, &r.errors);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(type == r.program->functions[0].symbol.return_type);
    ASSERT_EQ_INT(count, r.program->symbol_set_count);
    valid(&a, "f(b: bool) { if b { ret :a(1) }\n:a(wide()) }\n"
              "wide(): lng { 4294967296 }\nmain() {}\n");
    valid(&a, "f() { ret :a\n:b }\nmain() {}\n");
    invalid(&a, "f(b: bool) { if b { :a(1) } else { :a(false) } }\nmain() {}",
            "conflicting payload");
    valid(&a, "f(b: bool) { if b { :a } else { 1 } }\nmain() {}");
    invalid(&a, "main() { let x = if true { :a } else { 1 } }",
            "compatible types");
    invalid(
        &a,
        "f(n: int) { if n == 0 { :end } else { :next(f(n - 1)) } }\nmain() {}",
        "consistent return type");
  }
  TEST("large recursive symbol components use the graph's explicit stacks") {
    size_t capacity = 250000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length +=
          (size_t)snprintf(text + length, capacity - length,
                           "f%zu(b: bool) { if b { :%s } else { f%zu(b) } }\n",
                           i, i == 2049 ? "last" : "first", (i + 1) % 2050);
    snprintf(text + length, capacity - length, "main() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->count);
    for (size_t i = 0; i < 2050; i++)
      ASSERT_EQ_INT(2,
                    r.program->functions[i].symbol.return_type->symbols->count);
  }
  TEST("global tags may have distinct payload types until their sets meet") {
    Result r = valid(
        &a,
        "type A = Sym<:value(int)>\ntype B = Sym<:value(str)>\nmain() {}\n");
    ASSERT_TRUE(r.program->aliases[0].type != r.program->aliases[1].type);
    ASSERT_TRUE(r.program->aliases[0].type->symbols->variants[0].tag ==
                r.program->aliases[1].type->symbols->variants[0].tag);
    invalid(
        &a,
        "type A = Sym<:value(int)>\ntype B = Sym<A, :value(str)>\nmain() {}",
        "conflicting payload");
    invalid(&a, "type A = Sym<:value, :value(())>\nmain() {}",
            "conflicting payload");
    invalid(&a, "type A = Sym<int>\nmain() {}", "inclusion requires");
    invalid(&a, "type A = int\nmain() {}", "require a symbol set");
  }
  TEST("symbol patterns, named groups, residual binders and lexical scope") {
    Result r = valid(&a, "type Pair = Sym<:a, :b(str)>\n"
                         "f(x: Sym<Pair, :c>) { x |> { :a => ret :handled\n"
                         "other => other } }\n"
                         "main() { let x: Sym<Pair, :c> = :b(\"B\")\n"
                         "x |> { :a => ()\nPair => ()\n:c => () } }");
    const SemaType *ft = r.program->functions[0].symbol.return_type;
    ASSERT_TRUE(!sema_variant(ft, sema_tag(r.program, "a")));
    ASSERT_EQ_INT(3, ft->symbols->count);
    invalid(&a, "main() { :a(1) |> { :a(x) => x++ } }", "immutable binding");
    invalid(&a, "main() { :a(1) |> { :a(x) => x }\nx }", "Unknown binding");
    invalid(&a, "main() { let x: Sym<:a, :b> = :a\nx |> { :a => () } }",
            "not exhaustive");
    invalid(&a, "main() { :a |> { :a => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "type A = Sym<:a>\nmain() { :b |> { A => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { :a |> { :b => ()\n_ => () } }", "tag is not in");
    invalid(&a, "main() { :a(1) |> { :a => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { :a(_) => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { 1 => ()\n_ => () } }", "require symbol or");
    invalid(&a, "main() { 1 |> { :a => ()\n_ => () } }",
            "require a symbol input");
    invalid(&a, "type A = Sym<:a(str)>\nmain() { :a(1) |> { A => () } }",
            "conflicting payload");
    invalid(&a, "main() { :a(1) |> { :a(1) => () } }",
            "Refutable symbol payload");
    invalid(&a,
            "type A = Sym<:a>\nmain() { :outer(:a) |> { :outer(A) => () } }",
            "Refutable symbol payload");
  }
  TEST("equality extends through finite symbols, never recursive shapes") {
    valid(&a, "B { value: Sym<:text(str), :unit(())> }\n"
              "main() { let a = B { value: :text(\"a\") }\n"
              "let b = B { value: :unit(()) }\nprint(str(a == b))\n"
              "print(str(:a == :b))\nprint(str(:n(1) == :n(2))) }");
    Result r = valid(&a, "type Link = Sym<:none, :next(Node)>\nNode { link: "
                         "Link }\nmain() {}\n");
    ASSERT_TRUE(!r.program->structs[0].equatable);
    ASSERT_TRUE(!r.program->aliases[0].type->symbols->equatable);
    invalid(&a,
            "type Link = Sym<:none, :next(Node)>\nNode { link: Link }\n"
            "f(x: Link, y: Link) { x == y }\nmain() {}",
            "can contain a reference cycle have no == or !=");
    invalid(&a, "main() { :a < :b }", "Comparison of this type");
    invalid(&a, "main() { :a(1) == :a(false) }", "conflicting payload");
  }
  TEST("permissions traverse payloads, calls, narrowing, aliases and stores") {
    const char *prefix = "P { x: int }\ntype Choice = Sym<:some(P), :none>\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, body), text)
    invalid(&a,
            CASE("f(s: Choice) { s |> { :some(p) => p.x = 1\n:none => () } }"),
            "Cannot mutate");
    valid(&a,
          CASE("f(s: &Choice) { s |> { :some(p) => p.x = 1\n:none => () } }"));
    invalid(&a, CASE("f(p: P) { :some(p) |> { :some(q) => q.x = 1 } }"),
            "Cannot mutate");
    invalid(
        &a,
        CASE("get(s: Choice) { s |> { :some(p) => p\n:none => P { x: 0 } } }\n"
             "f(s: Choice) { get(s).x = 1 }"),
        "Cannot mutate");
    invalid(&a,
            CASE("wrap(p: P) { :some(p) }\n"
                 "f(p: P) { wrap(p) |> { :some(q) => q.x = 1 } }"),
            "Cannot mutate");
    invalid(&a, CASE("set(s: &Choice) {}\nf(s: Choice) { set(s) }"),
            "mutable argument");
    invalid(&a, CASE("set(s: &Choice) {}\nf(p: P) { set(:some(p)) }"),
            "mutable argument");
    invalid(&a, CASE("H { s: Choice }\nf(h: &H, p: P) { h.s = :some(p) }"),
            "cannot be stored in a field");
    valid(&a, CASE("H { s: Choice }\nf(h: &H, p: &P) { h.s = :some(p) }"));
    invalid(&a,
            CASE("f(s: Choice) { s |> { :none => ()\nother => other |> { "
                 ":some(p) => p.x = 1 } } }"),
            "Cannot mutate");
    valid(&a, CASE("f(s: Choice) { var x = s\nx = :some(P { x: 1 })\n"
                   "x |> { :some(p) => p.x = 2\n:none => () } }"));
#undef CASE
  }
  TEST("alias dependencies are iterative, cycles diagnosed, struct indirection "
       "allowed") {
    invalid(&a, "type A = B\ntype B = A\nmain() {}", "Recursive named types");
    invalid(&a, "type A = Sym<:next(A)>\nmain() {}", "Recursive named types");
    invalid(&a, "type A = Sym<:a>\nA() {}\nmain() {}", "cannot share a name");
    invalid(&a, "A {}\ntype A = Sym<:a>\nmain() {}", "reserved named type");
    invalid(&a, "type int = Sym<:a>\nmain() {}", "Expected a named type");
    invalid(&a, "type print = Sym<:a>\nmain() {}", "reserved named type");
    size_t capacity = 160000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "type A%zu = A%zu\n", i, i + 1);
    snprintf(text + length, capacity - length,
             "type A2050 = Sym<:a>\nmain() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->alias_count);
    ASSERT_TRUE(r.program->aliases[0].type == r.program->aliases[2050].type);
  }
  TEST("symbol coverage agrees with an independent five-tag bitset oracle") {
    Result r = valid(&a, "type All = Sym<:a, :b, :c, :d, :e>\nmain() {}\n");
    const SemaType *type = r.program->aliases[0].type;
    uint32_t random = UINT32_C(1234567);
    for (size_t trial = 0; trial < 500; trial++) {
      SemaExpr input = {.type = type};
      SemaExpr match = {.kind = SEMA_EXPR_MATCH};
      SemaArm arms[8] = {{0}};
      SemaVar bindings[8] = {{0}};
      unsigned masks[8], covered = 0;
      size_t expected_errors = 0;
      match.as.match.input = &input;
      match.as.match.arms = arms;
      match.as.match.count = 8;
      for (size_t i = 0; i < 8; i++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned kind = (random >> 16) % 4;
        if (kind == 0) {
          arms[i].kind = PAT_SYMBOL;
          unsigned tag = random % 5;
          arms[i].tag = type->symbols->variants[tag].tag;
          masks[i] = 1u << tag;
        } else if (kind == 1) {
          arms[i].kind = PAT_SET;
          unsigned group = (random % 31) + 1;
          SemaVariant variants[5];
          size_t n = 0;
          for (size_t j = 0; j < 5; j++)
            if (group & (1u << j))
              variants[n++] = type->symbols->variants[j];
          arms[i].set = sema_symbol_set(r.program, variants, n);
          masks[i] = group;
        } else {
          arms[i].kind = kind == 2 ? PAT_BIND : PAT_WILDCARD;
          if (kind == 2)
            arms[i].binding = &bindings[i];
          masks[i] = 31;
        }
      }
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      sema_match_coverage(r.program, &match, &errors);
      for (size_t i = 0; i < 8; i++) {
        unsigned useful = masks[i] & ~covered;
        ASSERT_EQ_INT(useful != 0, arms[i].useful);
        if (!useful)
          expected_errors++;
        unsigned actual = 0;
        for (size_t j = 0; j < 5; j++)
          if (arms[i].cases &&
              sema_variant(arms[i].cases, type->symbols->variants[j].tag))
            actual |= 1u << j;
        ASSERT_EQ_INT(useful, actual);
        if (useful && arms[i].kind == PAT_BIND)
          ASSERT_TRUE(arms[i].binding->type == arms[i].cases);
        covered |= masks[i];
      }
      expected_errors += covered != 31;
      ASSERT_EQ_INT(expected_errors, errors.count);
    }
  }
  TEST("outcome handling extends data symbols without changing their rules") {
    valid(&a, "main() { :ok(1) }");
    valid(&a, "f(): Sym<:ok, :bad> { :bad }\nmain() {}");
    invalid(&a, "f(): Sym<:a, _> { :a }\nmain() {}", "must declare :ok");
    valid(&a, "main() { :a }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
