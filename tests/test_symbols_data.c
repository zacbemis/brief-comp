#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST(
      "typed payload patterns stop before type, binder and arm-body cascades") {
    const char *cases[] = {
        "main() { :item[1] |> { :item[value: Missing] => unknown(value) } }",
        "main() { 1 |> { :item[value: int] => value } }",
        ("main() { :outer[:inner[1]] |> { :outer[:inner[x: Missing]] => f(x) } "
         "}"),
        "main() { :item[1] |> { :item[(x: :a | :b)] => f(x) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      if (r.errors.count) {
        ASSERT_EQ_INT(
            0, strcmp("Typed symbol payload patterns are not implemented yet.",
                      r.errors.items[0]->message));
        ASSERT_TRUE(r.errors.items[0]->span.file == r.program->module);
        ASSERT_TRUE(r.errors.items[0]->fixes == NULL);
      }
    }
    Result many =
        check(&a, "main() { 1 |> { :pair[x: Missing, y: Other] => f(x, y) } }");
    ASSERT_EQ_INT(1, many.errors.count);
    if (many.errors.count)
      ASSERT_EQ_INT(
          0, strcmp("Typed symbol payload patterns are not implemented yet.",
                    many.errors.items[0]->message));
  }
  TEST("qualified symbols stop before unavailable payloads and arm bodies") {
    const char *cases[] = {
        "A = Db.:!bad[Missing]\nmain() {}",
        "A = Db.:next[A]\nmain() {}",
        "A = Db.:pair[Missing, Other]\nmain() {}",
        "main() { Db.:!bad[missing()] }",
        "main() { 1 |> { Db.:!bad[x: Missing] => missing(x) } }",
        "main() { 1 |> { :outer[Db.:!bad[x]] => missing(x) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_EQ_INT(1, r.program->tag_count); // Only the eager shared :ok.
      if (r.errors.count) {
        ASSERT_EQ_INT(
            0, strcmp("Module-qualified symbols are not implemented yet.",
                      r.errors.items[0]->message));
        ASSERT_TRUE(!r.errors.items[0]->fixes);
      }
    }
    Result local = check(&a, "main() { :bad[1]\nDb.:!bad[unknown] }");
    ASSERT_EQ_INT(1, local.errors.count);
    ASSERT_EQ_INT(2, local.program->tag_count);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL,
                  sema_tag(local.program, local.program->module, "bad")->kind);
  }
  TEST("all payload types, forward structs, nested symbols and unit payloads") {
    valid(&a, "Choice = :object[Node] | :nested[:yes | :no] | "
              ":unit[()] | :empty\n"
              "Node { choice: Choice }\n"
              "main() { let n = Node { choice = :empty }\n"
              "let c: Choice = :object[n]\nc |> { :object[x] => x.choice\n"
              "other => other }\n() }");
    valid(&a, "main() { let a: :value[lng] = :value[4294967296]\n"
              "let b: :value[flt] = :value[0.1]\n"
              "let c: :value[()] = :value[()]\n"
              "c |> { :value[x] => x } }");
    valid(&a, "main() { :outer[:inner[\"a\" + \"b\"]] |> { :outer[x] => "
              "x |> { :inner[s] => print(s) } } }");
    valid(
        &a,
        "Inner = :a[str] | :b\n"
        "f(b: bool) -> :outer[Inner] { if b { :outer[:a[\"x\"]] } "
        "else { :outer[:b] } }\n"
        "main() { let x: :outer[Inner] = :outer[:a[\"x\"]]\n"
        "let y = :b\nlet z: :outer[Inner] = :outer[y]\n"
        "f(true) |> { :outer[inner] => inner |> { :a[_] => ()\n:b => () } } }");
    invalid(&a, "main() { let a: :a = :a[()] }", "does not match");
    invalid(&a, "main() { let a: :a[()] = :a }", "does not match");
    invalid(&a, "main() { let a: :a[lng] = :a[true] }", "does not match");
  }
  TEST("packing, widening and payload reads independently acquire owners") {
    Result r = valid(
        &a, "make() -> :a[str] | :b { :a[\"x\" + \"y\"] }\n"
            "read(s: :a[str] | :b) { s |> { :a[text] => text\n:b => \"\" } }\n"
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
    Result r = valid(
        &a, "Tier = :bronze | :silver | :gold\n"
            "Box { tier: Tier }\n"
            "choose(b: bool) { if b { :bronze } else { :gold } }\n"
            "forward(b: bool) -> Tier { if b { ret :silver }\nchoose(b) }\n"
            "same(x: Tier) -> Tier { x }\n"
            "main() -> () { var x: Tier = :bronze\nx = :gold\n"
            "let box = Box { tier = :silver }\nbox.tier = choose(false)\n"
            "forward(true) |> same() |> { Tier => () } }");
    ASSERT_EQ_INT(2,
                  r.program->functions[0].symbol.return_type->symbols->count);
    invalid(&a, "f(x: :a) {}\nmain() { f(:b) }", "argument has the wrong type");
    invalid(&a, "f(b: bool) -> :a { if b { ret :b }\n:a }\nmain() {}",
            "Return value");
    invalid(&a, "f() -> :a { :b }\nmain() {}", "body does not match");
    invalid(&a, "main() { var x = :a\nx = :b }", "Assignment value");
    invalid(&a, "B { x: :a }\nmain() { B { x = :b } }",
            "initializer has the wrong type");
  }
  TEST("symbol patterns, named groups, residual binders and lexical scope") {
    Result r = valid(&a, "Pair = :a | :b[str]\n"
                         "f(x: Pair | :c) { x |> { :a => ret :handled\n"
                         "other => other } }\n"
                         "main() { let x: Pair | :c = :b[\"B\"]\n"
                         "x |> { :a => ()\nPair => ()\n:c => () } }");
    const SemaType *ft = r.program->functions[0].symbol.return_type;
    ASSERT_TRUE(!sema_variant(ft, sema_tag(r.program, r.program->module, "a")));
    ASSERT_EQ_INT(3, ft->symbols->count);
    invalid(&a, "main() { :a[1] |> { :a[x] => x++ } }", "immutable binding");
    invalid(&a, "main() { :a[1] |> { :a[x] => x }\nx }", "Unknown binding");
    invalid(&a, "main() { let x: :a | :b = :a\nx |> { :a => () } }",
            "not exhaustive");
    invalid(&a, "main() { :a |> { :a => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "A = :a\nmain() { :b |> { A => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { :a |> { :b => ()\n_ => () } }", "tag is not in");
    invalid(&a, "main() { :a[1] |> { :a => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { :a[_] => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { 1 => ()\n_ => () } }", "require symbol or");
    invalid(&a, "main() { 1 |> { :a => ()\n_ => () } }",
            "require a symbol input");
    invalid(&a, "A = :a[str]\nmain() { :a[1] |> { A => () } }",
            "conflicting payload");
    invalid(&a, "main() { :a[1] |> { :a[1] => () } }",
            "Refutable symbol payload");
    invalid(&a, "A = :a\nmain() { :outer[:a] |> { :outer[A] => () } }",
            "Refutable symbol payload");
  }
  TEST("equality extends through finite symbols, never recursive shapes") {
    valid(&a, "B { value: :text[str] | :unit[()] }\n"
              "main() { let a = B { value = :text[\"a\"] }\n"
              "let b = B { value = :unit[()] }\nprint(str(a == b))\n"
              "print(str(:a == :b))\nprint(str(:n[1] == :n[2])) }");
    Result r = valid(&a, "Link = :none | :next[Node]\nNode { link: "
                         "Link }\nmain() {}\n");
    ASSERT_TRUE(!r.program->structs[0].equatable);
    ASSERT_TRUE(!r.program->aliases[0].type->symbols->equatable);
    invalid(&a,
            "Link = :none | :next[Node]\nNode { link: Link }\n"
            "f(x: Link, y: Link) { x == y }\nmain() {}",
            "can contain a reference cycle have no == or !=");
    invalid(&a, "main() { :a < :b }", "Comparison of this type");
    invalid(&a, "main() { :a[1] == :a[false] }", "conflicting payload");
  }
  TEST("permissions traverse payloads, calls, narrowing, aliases and stores") {
    const char *prefix = "P { x: int }\nChoice = :some[P] | :none\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, body), text)
    invalid(&a,
            CASE("f(s: Choice) { s |> { :some[p] => p.x = 1\n:none => () } }"),
            "Cannot mutate");
    valid(&a,
          CASE("f(s: &Choice) { s |> { :some[p] => p.x = 1\n:none => () } }"));
    invalid(&a, CASE("f(p: P) { :some[p] |> { :some[q] => q.x = 1 } }"),
            "Cannot mutate");
    invalid(
        &a,
        CASE("get(s: Choice) { s |> { :some[p] => p\n:none => P { x = 0 } } }\n"
             "f(s: Choice) { get(s).x = 1 }"),
        "Cannot mutate");
    invalid(&a,
            CASE("wrap(p: P) { :some[p] }\n"
                 "f(p: P) { wrap(p) |> { :some[q] => q.x = 1 } }"),
            "Cannot mutate");
    invalid(&a, CASE("set(s: &Choice) {}\nf(s: Choice) { set(s) }"),
            "read-only value cannot be passed");
    invalid(&a, CASE("set(s: &Choice) {}\nf(p: P) { set(:some[p]) }"),
            "read-only value cannot be passed");
    invalid(&a, CASE("H { s: Choice }\nf(h: &H, p: P) { h.s = :some[p] }"),
            "cannot be stored in a field");
    valid(&a, CASE("H { s: Choice }\nf(h: &H, p: &P) { h.s = :some[p] }"));
    invalid(&a,
            CASE("f(s: Choice) { s |> { :none => ()\nother => other |> { "
                 ":some[p] => p.x = 1 } } }"),
            "Cannot mutate");
    valid(&a, CASE("f(s: Choice) { var x = s\nx = :some[P { x = 1 }]\n"
                   "x |> { :some[p] => p.x = 2\n:none => () } }"));
#undef CASE
  }
  TEST(
      "permission messages distinguish read-only values from mutable targets") {
    const char *prefix =
        "P { n: int\n bump(&self) { self.n++ } }\n"
        "Choice = :some[P] | :none\nNested = :outer[Choice] | :empty\n"
        "Scalar = :text[str] | :number[int]\nWrapper { part: P\nmarker: int }\n"
        "Holder { item: P\nchoice: Choice\nnested: Nested\nwrapper: Wrapper\n"
        "text: str\ncount: int\nscalar: Scalar }\n"
        "mut(p: &P) { p.n++ }\n"
        "mutChoice(s: &Choice) {}\nmutNested(s: &Nested) {}\n"
        "mutScalar(s: &Scalar) {}\nsameChoice(s: Choice) -> Choice { s }\n";
    const char *borrow =
        "A read-only value cannot be passed to an & parameter; "
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
        {"P", "mutChoice(:some[value])", "mutChoice(:some[value])",
         ":some[value]", 0},
        {"P", "mutNested(:outer[:some[value]])",
         "mutNested(:outer[:some[value]])", ":outer[:some[value]]", 0},
        {"Choice", "mutChoice(value)", "mutChoice(value)", "value", 0},
        {"Choice", "mutChoice(sameChoice(value))",
         "mutChoice(sameChoice(value))", "sameChoice(value)", 0},
        {"Choice", "value |> { :none => ()\nrest => mutChoice(rest) }",
         "mutChoice(rest)", "rest", 0},
        {"Choice", "value |> { :some[p] => mut(p)\n:none => () }", "mut(p)",
         "p", 0},
        {"P", "holder.item = value", "holder.item = value", "value", 1},
        {"Choice", "holder.choice = value", "holder.choice = value", "value",
         1},
        {"P", "holder.choice = :some[value]", "holder.choice = :some[value]",
         ":some[value]", 1},
        {"P", "holder.nested = :outer[:some[value]]",
         "holder.nested = :outer[:some[value]]", ":outer[:some[value]]", 1},
        {"Choice", "holder.choice = sameChoice(value)",
         "holder.choice = sameChoice(value)", "sameChoice(value)", 1},
        {"P", "holder.wrapper = Wrapper { part = value, marker = 1 }",
         "holder.wrapper = Wrapper { part = value, marker = 1 }",
         "Wrapper { part = value, marker = 1 }", 1},
        {"P",
         "var alias = P { n = 0 }\nwhile flag { holder.item = alias\n"
         "alias = value }",
         "holder.item = alias", "alias", 1},
        {"P",
         "var alias = P { n = 0 }\nwhile flag { mut(alias)\nalias = value }",
         "mut(alias)", "alias", 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[4096];
      snprintf(
          text, sizeof(text),
          "%sf(value: %s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
          prefix, cases[i].type, cases[i].body);
      permission_diagnostic(&a, text, cases[i].site, cases[i].value,
                            cases[i].storing ? store : borrow);
      // Changing the source permission, not its ownership or active tag,
      // makes the same operations legal. The parameter binding stays immutable.
      snprintf(
          text, sizeof(text),
          "%sf(value: &%s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
          prefix, cases[i].type, cases[i].body);
      valid(&a, text);
    }
    char text[4096];
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: P, holder: &Holder, text: str, scalar: Scalar) {\n"
             "holder.count = value.n\nholder.text = text\n"
             "holder.scalar = scalar\nmutScalar(scalar)\n"
             "var alias = value\nalias = P { n = 1 }\nmut(alias)\n"
             "holder.item = alias\n}\nmain() {}");
    valid(
        &a,
        text); // Copied scalars/immutable data and fresh rebinding stay legal.
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: Choice) { mut(value) }\nmain() {}");
    Result wrong_type =
        invalid(&a, text, "Function argument has the wrong type.");
    ASSERT_EQ_INT(1, wrong_type.errors.count);
    Diagnostics permissions_only;
    diagnostics_init(&permissions_only, &a);
    sema_check_permissions(wrong_type.program, &permissions_only);
    ASSERT_EQ_INT(0, permissions_only.count); // Do not add a borrow cascade.
  }
  TEST("native aliases preserve scalar, unit and symbol payload identities") {
    struct {
      const char *spelling;
      SemaTypeKind kind;
    } cases[] = {
        {"bool", SEMA_BOOL},   {"chr", SEMA_CHR},   {"shrt", SEMA_SHRT},
        {"ushrt", SEMA_USHRT}, {"int", SEMA_INT},   {"uint", SEMA_UINT},
        {"lng", SEMA_LNG},     {"ulng", SEMA_ULNG}, {"flt", SEMA_FLT},
        {"dbl", SEMA_DUB},     {"str", SEMA_STR},   {"()", SEMA_UNIT}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[512];
      snprintf(text, sizeof(text),
               "Alias = Forward\nForward = %s\nA = :item[Alias]\nB = "
               ":item[%s]\nf(n: Alias) -> Forward { n }\nmain() {}",
               cases[i].spelling, cases[i].spelling);
      Result r = valid(&a, text);
      ASSERT_EQ_INT(4, r.program->alias_count);
      ASSERT_TRUE(r.program->aliases[0].type ==
                  &r.program->types[cases[i].kind]);
      ASSERT_TRUE(r.program->aliases[0].type == r.program->aliases[1].type);
      ASSERT_TRUE(r.program->aliases[2].type == r.program->aliases[3].type);
      ASSERT_TRUE(r.program->functions[0].symbol.params[0]->type ==
                  r.program->aliases[0].type);
    }
    valid(&a, "Num = lng\nText = str\nf(s: str) { Num(s)? }\nmain() { "
              "Text(Num(4294967296)) }");
    invalid(&a, "Small = shrt\nmain() { Small(32768) }", "outside the range");
    invalid(&a, "Flag = bool\nmain() { Flag(1) }", "Unknown function");
    invalid(&a, "Text = str\nmain() { Text(\"already text\") }",
            "str expects a number or bool");
    invalid(&a, "Unit = ()\nf(n: &Unit) {}\nmain() {}", "Mutable borrowing");
    invalid(&a, "Unit = ()\nf() -> Unit { ret 1 }\nmain() {}",
            "written return type");
    invalid(&a, "A = :next[B]\nB = A\nmain() {}", "Recursive named types");
    Result pattern = invalid(&a, "A = int\nmain() { :a |> { A => missing() } }",
                             "A named symbol pattern requires a symbol set.");
    ASSERT_EQ_INT(1, pattern.errors.count);
    size_t capacity = 160000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "A%zu = A%zu\n", i, i + 1);
    snprintf(text + length, capacity - length, "A2050 = lng\nmain() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->alias_count);
    for (size_t i = 0; i < r.program->alias_count; i++)
      ASSERT_TRUE(r.program->aliases[i].type == &r.program->types[SEMA_LNG]);
  }
  TEST("symbol coverage agrees with an independent five-tag bitset oracle") {
    Result r = valid(&a, "All = :a | :b | :c | :d | :e\nmain() {}\n");
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
  arena_destroy(&a);
  RUN_TESTS();
}
