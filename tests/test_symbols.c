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
// Permission diagnostics must identify the argument/RHS, not the call or
// destination field, and report once after a loop fixpoint on every check.
static void permission_diagnostic(Arena *a, const char *text, const char *site,
                                  const char *value, const char *message) {
  const char *at = strstr(text, site), *within = strstr(site, value);
  ASSERT_TRUE(at != NULL && within != NULL);
  if (!at || !within)
    return;
  ASSERT_TRUE(strstr(at + strlen(site), site) == NULL);
  size_t start = (size_t)(at - text) + (size_t)(within - site);
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned pass = 0; pass < 3; pass++) {
    Diagnostics again;
    diagnostics_init(&again, a);
    const Diagnostics *errors = &r.errors;
    if (pass == 1) {
      sema_check_permissions(r.program, &again);
      errors = &again;
    } else if (pass == 2) {
      sema_check(r.program, &again);
      errors = &again;
    }
    if (errors->count != 1) {
      fprintf(stderr, "permission case: %s\n", text);
      diagnostics_print(errors, stderr);
    }
    ASSERT_EQ_INT(1, errors->count);
    if (errors->count) {
      ASSERT_EQ_STRN(message, errors->items[0]->message,
                     strlen(errors->items[0]->message));
      ASSERT_EQ_INT(start, errors->items[0]->span.start);
      ASSERT_EQ_INT(start + strlen(value), errors->items[0]->span.end);
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
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
  TEST("hashed interning keeps sets canonical across growth and conflicts") {
    Result r = valid(&a, "main() {}");
    SemaProgram *p = r.program;
    enum { COUNT = 300 }; // Several interning-table resizes.
    SemaTag *tags[COUNT];
    const SemaType *single[COUNT];
    for (size_t i = 0; i < COUNT; i++) {
      char *name = arena_alloc(&a, 16);
      snprintf(name, 16, "t%zu", i);
      tags[i] = sema_tag(p, name);
      SemaVariant v = {tags[i], NULL};
      single[i] = sema_symbol_set(p, &v, 1);
      ASSERT_TRUE(sema_symbol_set(p, &v, 1) == single[i]);
    }
    const SemaType *forward = single[0], *backward = single[COUNT - 1];
    for (size_t i = 1; i < COUNT; i++) {
      forward = sema_symbol_union(p, forward, single[i]);
      backward = sema_symbol_union(p, backward, single[COUNT - 1 - i]);
    }
    ASSERT_TRUE(forward == backward);
    ASSERT_EQ_INT(COUNT, forward->symbols->count);
    SemaVariant reversed[COUNT];
    for (size_t i = 0; i < COUNT; i++)
      reversed[i] = (SemaVariant){tags[COUNT - 1 - i], NULL};
    ASSERT_TRUE(sema_symbol_set(p, reversed, COUNT) == forward);
    const SemaType *number = &p->types[SEMA_INT];
    SemaVariant conflict = {tags[7], number};
    ASSERT_TRUE(sema_symbol_union(p, forward,
                                  sema_symbol_set(p, &conflict, 1)) == NULL);
    SemaVariant duplicate[] = {
        {tags[3], NULL}, {tags[2], NULL}, {tags[3], number}};
    ASSERT_TRUE(sema_symbol_set(p, duplicate, 3) == NULL);
    SemaVariant outcome[] = {{tags[4], NULL}, {p->ok, number}, {tags[5], NULL}};
    const SemaType *full = sema_symbol_set(p, outcome, 3);
    ASSERT_TRUE(sema_ok(full) != NULL && sema_ok(full)->payload == number);
    ASSERT_TRUE(sema_failures(p, full) ==
                sema_symbol_union(p, single[4], single[5]));
    ASSERT_TRUE(sema_ok(sema_failures(p, full)) == NULL);
  }
  TEST("emission declares only referenced symbol sets, in dependency order") {
    // Each ? grows h's inferred error set by one variant. Inference interns
    // every intermediate union, but no lowered value has those types.
    char text[16384];
    size_t used = 0;
    for (int i = 0; i < 40; i++)
      used += (size_t)snprintf(text + used, sizeof(text) - used,
                               "g%d(x: int): Sym<:ok(int), :e%d(int)> { x }\n",
                               i, i);
    used += (size_t)snprintf(text + used, sizeof(text) - used, "h(x: int) {\n");
    for (int i = 0; i < 40; i++)
      used += (size_t)snprintf(text + used, sizeof(text) - used,
                               "let y%d = g%d(x)?\n", i, i);
    snprintf(text + used, sizeof(text) - used,
             "x }\nmain() { h(1) |> { :ok(v) => print(str(v))\n"
             "_ => print(\"e\") } }\n");
    Result r = valid(&a, text);
    IrProgram *ir = ir_lower(&a, r.program);
    ASSERT_TRUE(ir != NULL);
    if (ir) {
      ASSERT_TRUE(r.program->symbol_set_count > ir->symbol_set_count + 30);
      unsigned char *declared =
          arena_calloc(&a, r.program->symbol_set_count, 1);
      for (size_t i = 0; i < ir->symbol_set_count; i++) {
        const SemaSymSet *s = ir->symbol_sets[i];
        for (size_t j = 0; j < s->count; j++) {
          const SemaType *payload = s->variants[j].payload;
          if (payload && payload->kind == SEMA_SYM)
            ASSERT_TRUE(declared[payload->symbols->id]);
        }
        declared[s->id] = 1;
      }
      for (size_t i = 0; i < ir->count; i++)
        for (size_t j = 0; j < ir->functions[i].temporaries; j++) {
          const SemaType *t = ir->functions[i].temporary_types[j];
          if (t && t->kind == SEMA_SYM)
            ASSERT_TRUE(declared[t->symbols->id]);
        }
    }
    // Nested payload sets are declared even when only their container is used.
    valid(&a, "f(): Sym<:a(Sym<:b(Sym<:c(str)>)>)> { :a(:b(:c(\"x\"))) }\n"
              "main() { f() |> { :a(inner) => inner |> {\n"
              ":b(deep) => deep |> { :c(s) => print(s) } } } }");
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
            "read-only value cannot be passed");
    invalid(&a, CASE("set(s: &Choice) {}\nf(p: P) { set(:some(p)) }"),
            "read-only value cannot be passed");
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
  TEST("permission messages distinguish read-only values from mutable targets") {
    const char *prefix =
        "P { n: int }\n"
        "type Choice = Sym<:some(P), :none>\n"
        "type Nested = Sym<:outer(Choice), :empty>\n"
        "type Scalar = Sym<:text(str), :number(int)>\n"
        "Wrapper { part: P\nmarker: int }\n"
        "Holder { item: P\nchoice: Choice\nnested: Nested\nwrapper: Wrapper\n"
        "text: str\ncount: int\nscalar: Scalar }\n"
        "mut(p: &P) { p.n++ }\n"
        "mutChoice(s: &Choice) {}\nmutNested(s: &Nested) {}\n"
        "mutScalar(s: &Scalar) {}\n"
        "sameChoice(s: Choice): Choice { s }\n"
        "impl P { bump(p: &Self) { p.n++ } }\n";
    const char *borrow = "A read-only value cannot be passed to an & parameter; "
                         "take the parameter it comes from as &.";
    const char *store = "A read-only value cannot be stored in a field; take "
                         "the parameter it comes from as &.";
    struct {
      const char *type, *body, *site, *value;
      int storing;
    } cases[] = {
        {"P", "mut(value)", "mut(value)", "value", 0},
        {"P", "value.bump()", "value.bump()", "value", 0},
        {"P", "value |> mut()", "value |> mut()", "value", 0},
        {"P", "let alias = value\nmut(alias)", "mut(alias)", "alias", 0},
        {"P", "mutChoice(:some(value))", "mutChoice(:some(value))",
         ":some(value)", 0},
        {"P", "mutNested(:outer(:some(value)))",
         "mutNested(:outer(:some(value)))", ":outer(:some(value))", 0},
        {"Choice", "mutChoice(value)", "mutChoice(value)", "value", 0},
        {"Choice", "mutChoice(sameChoice(value))",
         "mutChoice(sameChoice(value))", "sameChoice(value)", 0},
        {"Choice", "value |> { :none => ()\nrest => mutChoice(rest) }",
         "mutChoice(rest)", "rest", 0},
        {"Choice", "value |> { :some(p) => mut(p)\n:none => () }",
         "mut(p)", "p", 0},
        {"P", "holder.item = value", "holder.item = value", "value", 1},
        {"Choice", "holder.choice = value", "holder.choice = value", "value", 1},
        {"P", "holder.choice = :some(value)", "holder.choice = :some(value)",
         ":some(value)", 1},
        {"P", "holder.nested = :outer(:some(value))",
         "holder.nested = :outer(:some(value))", ":outer(:some(value))", 1},
        {"Choice", "holder.choice = sameChoice(value)",
         "holder.choice = sameChoice(value)", "sameChoice(value)", 1},
        {"P", "holder.wrapper = Wrapper { part: value, marker: 1 }",
         "holder.wrapper = Wrapper { part: value, marker: 1 }",
         "Wrapper { part: value, marker: 1 }", 1},
        {"P", "var alias = P { n: 0 }\nwhile flag { holder.item = alias\n"
              "alias = value }", "holder.item = alias", "alias", 1},
        {"P", "var alias = P { n: 0 }\nwhile flag { mut(alias)\nalias = value }",
         "mut(alias)", "alias", 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[4096];
      snprintf(text, sizeof(text),
               "%sf(value: %s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
               prefix, cases[i].type, cases[i].body);
      permission_diagnostic(&a, text, cases[i].site, cases[i].value,
                            cases[i].storing ? store : borrow);
      // Changing the source permission, not its ownership or active tag,
      // makes the same operations legal. The parameter binding stays immutable.
      snprintf(text, sizeof(text),
               "%sf(value: &%s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
               prefix, cases[i].type, cases[i].body);
      valid(&a, text);
    }
    char text[4096];
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: P, holder: &Holder, text: str, scalar: Scalar) {\n"
             "holder.count = value.n\nholder.text = text\n"
             "holder.scalar = scalar\nmutScalar(scalar)\n"
             "var alias = value\nalias = P { n: 1 }\nmut(alias)\n"
             "holder.item = alias\n}\nmain() {}");
    valid(&a, text); // Copied scalars/immutable data and fresh rebinding stay legal.
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: Choice) { mut(value) }\nmain() {}");
    Result wrong_type = invalid(&a, text, "Function argument has the wrong type.");
    ASSERT_EQ_INT(1, wrong_type.errors.count);
    Diagnostics permissions_only;
    diagnostics_init(&permissions_only, &a);
    sema_check_permissions(wrong_type.program, &permissions_only);
    ASSERT_EQ_INT(0, permissions_only.count); // Do not add a borrow cascade.
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
