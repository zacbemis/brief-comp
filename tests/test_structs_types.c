#include "structs_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("checked fields and functions retain written visibility without access "
       "changes") {
    Result r = check(
        &a, "Vault { pub visible: int\nhidden: int\n"
            "pub new(n: int) -> Vault { Vault { visible = n, hidden = 0 } }\n"
            "read() -> int { self.hidden } }\n"
            "pub exported(v: Vault) -> int { v.hidden }\n"
            "local(v: &Vault) { v.hidden = v.visible }\n"
            "main() { let v = Vault.new(7)\nlocal(v)\nv.read()\nexported(v) }");
    if (r.errors.count)
      diagnostics_print(&r.errors, stderr);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_EQ_INT(1, r.program->struct_count);
    ASSERT_EQ_INT(5, r.program->count);
    if (r.program->struct_count == 1 && r.program->count == 5) {
      SemaStruct *s = &r.program->structs[0];
      ASSERT_EQ_INT(VISIBILITY_PUB, s->fields[0].visibility);
      ASSERT_EQ_INT(VISIBILITY_PRIV, s->fields[1].visibility);
      ASSERT_EQ_INT(0, s->fields[0].id);
      ASSERT_EQ_INT(1, s->fields[1].id);
      ASSERT_EQ_INT(s->syntax->as.structure.fields[0].span.start,
                    s->fields[0].span.start);
      ASSERT_EQ_INT(VISIBILITY_PUB, r.program->functions[0].symbol.visibility);
      ASSERT_EQ_INT(VISIBILITY_PRIV, r.program->functions[1].symbol.visibility);
      ASSERT_EQ_INT(VISIBILITY_PUB, r.program->functions[2].symbol.visibility);
      ASSERT_EQ_INT(VISIBILITY_PRIV, r.program->functions[3].symbol.visibility);
      ASSERT_EQ_INT(VISIBILITY_PRIV, r.program->functions[4].symbol.visibility);
      Diagnostics again;
      diagnostics_init(&again, &a);
      sema_check(r.program, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_EQ_INT(VISIBILITY_PUB, s->fields[0].visibility);
      ASSERT_EQ_INT(VISIBILITY_PUB, r.program->functions[2].symbol.visibility);
    }
    // Visibility does not grant mutation through an ordinary parameter.
    invalid(&a, "P { pub x: int }\nf(p: P) { p.x = 1 }\nmain() {}",
            "Cannot mutate through an ordinary parameter");
  }
  TEST("struct types are canonical, nominal, and visible before their "
       "definitions") {
    Result r =
        check(&a, "main() { make() }\nmake() { Box { child = Child { n = "
                  "1 } } }\nBox { child: Child }\nChild { n: shrt }");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_EQ_INT(2, r.program->struct_count);
    SemaStruct *box = &r.program->structs[0], *child = &r.program->structs[1];
    ASSERT_TRUE(box->fields[0].type == &child->type);
    ASSERT_TRUE(r.program->functions[0].symbol.return_type == &box->type);
    ASSERT_TRUE(r.program->functions[1].symbol.return_type == &box->type);
    ASSERT_TRUE(child->fields[0].type == &r.program->types[SEMA_SHRT]);
    invalid(
        &a,
        "A { unit: () }\nB { unit: () }\nmain() { let x: A = B { unit = () } }",
        "annotation");
    invalid(&a,
            "A { unit: () }\nB { unit: () }\nmain() { A { unit = () } == B { "
            "unit = () } }",
            "same type");
    valid(&a, "Node { next: Node }\nmain() {}");
    valid(&a, "A { b: B }\nB { a: A }\nmain() {}");
    invalid(&a,
            "Node { next: Node }\nmain() {}\nf(a: Node, b: Node) { a == b }",
            "can contain a reference cycle have no == or !=");
    invalid(&a,
            "Node { next: Node }\nHolder { node: Node }\nmain() {}\n"
            "f(a: Holder, b: Holder) { a != b }",
            "can contain a reference cycle have no == or !=");
    invalid(&a, "A { unit: () }\nmain() { A { unit = () } < A { unit = () } }",
            "Comparison of this type is not implemented yet.");
  }
  TEST("fields and struct literal initialization are complete and typed") {
    invalid(&a, "A { x: int\nx: int }\nmain() {}", "Duplicate struct field");
    invalid(&a, "A { unit: () }\nA { unit: () }\nmain() {}",
            "Duplicate or reserved");
    invalid(&a, "A { unit: () }\nA() {}\nmain() {}", "cannot share a name");
    invalid(&a, "A { x: int }\nmain() { A {} }", "Missing struct field");
    invalid(&a, "A { x: int }\nmain() { A { x = 1, x = 2 } }",
            "Duplicate struct field initializer");
    invalid(&a, "A { unit: () }\nmain() { A { unit = (), x = 1 } }",
            "Unknown struct field");
    invalid(&a, "A { x: int }\nmain() { A { x = true } }",
            "initializer has the wrong type");
    invalid(&a, "main() { Missing {} }", "Unknown struct type");
    invalid(&a, "A { unit: () }\nmain() { A { unit = () }.missing }",
            "Unknown struct field");
    invalid(&a, "main() { 1.missing }", "Field access requires");
    invalid(&a, "A { x: int }\nmain() { let a = A { x = 0 }\na.x = true }",
            "Assignment value");
    invalid(&a, "A { x: str }\nmain() { let a = A { x = \"x\" }\na.x++ }",
            "numeric binding");
    valid(&a, "A { x: () }\nmain() { let a = A { x = () }\na.x = ()\na.x }");
  }
  TEST("struct aliases share nominal identity, constructors and method "
       "namespace") {
    Result r = check(
        &a, "Alias = Forward\nForward = Item\nSelf = Alias\n"
            "NodeAlias = Node\nNode { next: :next[NodeAlias] | :none }\n"
            "Item { n: int\nmake(n: int) -> Self { Self { n = n } }\n"
            "read() -> int { self.n } }\n"
            "main() { let n: Alias = Item { n = 1 }\n"
            "let same: bool = n == Forward.make(1)\nSelf.make(2).read() }");
    if (r.errors.count)
      diagnostics_print(&r.errors, stderr);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_EQ_INT(2, r.program->struct_count);
    const SemaType *item = &r.program->structs[1].type;
    for (size_t i = 0; i < 3; i++)
      ASSERT_TRUE(r.program->aliases[i].type == item);
    ASSERT_TRUE(r.program->aliases[3].type == &r.program->structs[0].type);
    ASSERT_TRUE(r.program->functions[0].symbol.owner == item->structure);
    ASSERT_TRUE(r.program->functions[1].symbol.params[0]->type == item);
    valid(&a, "Ref = Item\nItem { n: int }\n"
              "f(v: &Ref) { v.n++ }\nmain() { f(Ref { n = 1 }) }");
    invalid(&a, "Ref = Item\nItem { n: int }\nf(v: Ref) { v.n++ }\nmain() {}",
            "Cannot mutate through");
    invalid(&a,
            "Ref = Item\nItem { n: int }\nHolder { v: Ref }\n"
            "f(v: Ref, h: &Holder) { h.v = v }\nmain() {}",
            "cannot be stored");
    invalid(&a,
            "Ref = Item\nItem { n: int }\nOther { n: int }\n"
            "main() { let n: Ref = Other { n = 1 } }",
            "annotation");
    invalid(&a, "Ref = Item\nItem { n: int\nf() {}\nf() {} }\nmain() {}",
            "Duplicate function name");
  }
  TEST("primitive member names keep owned targets separate from converters") {
    Result r = check(
        &a,
        "Ref = P\nP { unit: ()\nstr(n: int) -> str { str(n) }\n"
        "int(n: int) -> int { int(n) } }\nmain() { print(Ref.str(P.int(1))) }");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    SemaSymbol *owned_str =
        sema_method(r.program, &r.program->structs[0].type, "str");
    SemaSymbol *owned_int =
        sema_method(r.program, &r.program->structs[0].type, "int");
    ASSERT_TRUE(owned_str && owned_int && owned_str != owned_int);
    ASSERT_TRUE(owned_str->owner == &r.program->structs[0]);
    ASSERT_EQ_INT(0, owned_str->builtin);
    SemaExpr *builtin_str = r.program->functions[0].body->as.block.items[0];
    SemaExpr *builtin_int = r.program->functions[1].body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_BUILTIN_STR, builtin_str->as.call.target->builtin);
    ASSERT_EQ_INT(SEMA_BUILTIN_CONVERT, builtin_int->as.call.target->builtin);
    ASSERT_TRUE(builtin_str->as.call.target != owned_str);
    ASSERT_TRUE(builtin_int->as.call.target != owned_int);
    SemaExpr *print = r.program->functions[2].body->as.block.items[0];
    ASSERT_TRUE(print->as.call.args[0]->as.call.target == owned_str);
    ASSERT_TRUE(print->as.call.args[0]->as.call.args[0]->as.call.target ==
                owned_int);
    invalid(&a, "P { unit: ()\nint() {}\nint() {} }\nmain() {}",
            "Duplicate function name");
    invalid(&a,
            "P { unit: ()\nstr(n: int) -> str { str(n) } }\nmain() { "
            "P.str(false) }",
            "argument has the wrong type");
    Result missing = check(&a, "main() { (1).str() }");
    ASSERT_EQ_INT(1, missing.errors.count);
    ASSERT_EQ_INT(0, strcmp("Unknown method for this type.",
                            missing.errors.items[0]->message));
    ASSERT_TRUE(!missing.errors.items[0]->notes && !missing.program->checked);
    ASSERT_TRUE(ir_lower(&a, missing.program) == NULL);
    Result unknown = check(&a, "main() { Json.str(1) }");
    ASSERT_EQ_INT(1, unknown.errors.count);
    ASSERT_TRUE(!unknown.program->checked);
    Result trait =
        check(&a, "T { str(n: Missing) -> Missing\nint() { missing() } }");
    ASSERT_EQ_INT(1, trait.errors.count);
    ASSERT_TRUE(!trait.program->resolved && !trait.program->checked);
  }
  TEST("conformance is a declaration barrier, not fake trait resolution") {
    const char *sources[] = {
        ("P: Missing[:same] and models.Other[Unknown] { n: Missing\nf() { "
         "missing() } }\n"
         "Q: Other[:!same] { n: Unknown }\nAlias = Cycle\nCycle = Alias\n"),
        "P: A {}\nQ: B {}\nAlias = Missing\n"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = check(&a, sources[i]);
      ASSERT_EQ_INT(2, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_EQ_INT(0, r.program->struct_count);
      ASSERT_EQ_INT(0, r.program->alias_count);
      ASSERT_EQ_INT(0, r.program->count);
      ASSERT_EQ_INT(1, r.program->tag_count);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      for (size_t j = 0; j < r.errors.count; j++) {
        Diagnostic *d = r.errors.items[j];
        ASSERT_EQ_INT(
            0, strcmp("Trait conformance is not implemented yet.", d->message));
        ASSERT_TRUE(!d->fixes && !d->notes);
        ASSERT_EQ_INT(':', d->span.file->text[d->span.start]);
      }
    }
    Result both =
        check(&a, "import models.User\nP: Missing { n: Missing }\nEmpty {}\n");
    ASSERT_EQ_INT(3, both.errors.count);
  }
  TEST("traits stop before unavailable type, member and main cascades") {
    const char *cases[] = {
        "Empty {}\n", ("Empty {}\nAlias = Empty\nf(n: Empty) { missing() }"),
        ("Empty { f() { missing() } }\nData { n: Missing }")};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_EQ_INT(0, r.program->struct_count);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      if (r.errors.count) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp("A struct needs at least one field; traits are "
                                "not implemented yet.",
                                d->message));
        ASSERT_EQ_INT(0, d->span.start);
        ASSERT_EQ_INT((size_t)(strchr(cases[i], '\n') - cases[i]), d->span.end);
        ASSERT_TRUE(!d->fixes && !d->notes);
      }
    }
    Result both = check(&a, "import models.User\nA {}\nB {}\n");
    ASSERT_EQ_INT(3, both.errors.count);
    valid(&a, "Marker { unit: () }\nmain() { Marker { unit = () } }");
    invalid(&a, "Marker { unit: () }\nmain() { Marker {} }",
            "Missing struct field");
    invalid(&a, "Marker { unit: () }\nf(m: Marker) { m.unit = () }\nmain() {}",
            "Cannot mutate through");
  }
  TEST("member headers are available before bodies and defaults stay "
       "unsupported") {
    valid(&a,
          "P { n: int\nodd(n: int) -> bool { if n == 0 { ret false }\nP.even(n "
          "- 1) }\n"
          "even(n: int) -> bool { if n == 0 { ret true }\nP.odd(n - 1) } }\n"
          "main() { P.even(4) }");
    Result trait = check(
        &a, "T { required(n: Missing) -> Missing\ndefault() { missing() } }");
    ASSERT_EQ_INT(1, trait.errors.count);
    ASSERT_TRUE(!trait.program->resolved && !trait.program->checked);
    if (trait.errors.count)
      ASSERT_EQ_INT(0, strcmp("A struct needs at least one field; traits are "
                              "not implemented yet.",
                              trait.errors.items[0]->message));
  }
  TEST("ordinary Self types do not mean the current owner") {
    valid(&a, "Self { n: int\nmake() -> Self { Self { n = 1 } } }\n"
              "Other { value: Self\npass(s: Self) -> Self { Self.make() } }\n"
              "main() { let s: Self = Other.pass(Self.make()) }");
    valid(&a, "Self = Value\nValue = :value[int] | :none\nOther { n: Self\n"
              "pass(n: Self) -> Self { n } }\n"
              "main() { let n: Self = Other.pass(:value[1]) }");
    valid(&a,
          "Self = :value[int] | :none\nOther { n: Self\n"
          "pass(n: Self) -> Self { n } }\nmain() { Other.pass(:value[1]) }");
    Result unknown = check(&a, "f(n: Self) {}\nmain() {}");
    ASSERT_EQ_INT(1, unknown.errors.count);
    if (unknown.errors.count)
      ASSERT_TRUE(unknown.errors.items[0]->fixes == NULL);
  }
  TEST("string operators and builtin conversion boundaries") {
    valid(&a,
          "main() { var s = \"a\" + str(1)\ns += str(true)\ns == \"a1true\" }");
    invalid(&a, "main() { \"x\" + 1 }", "same type");
    invalid(&a, "main() { \"x\" - \"y\" }", "Arithmetic requires");
    invalid(&a, "main() { var s = \"x\"\ns -= \"y\" }", "Compound assignment");
    invalid(&a, "main() { str() }", "exactly one number or bool");
    invalid(&a, "main() { str(1, 2) }", "exactly one number or bool");
    invalid(&a, "main() { str(()) }", "expects a number or bool");
    invalid(&a, "main() { str(\"x\") }", "expects a number or bool");
    invalid(&a, "main() { str('x') }", "expects a number or bool");
    valid(&a, "main() { int(\"1\") }");
    invalid(&a, "main() { int(true) }", "expects a number, chr or str");
    invalid(&a, "A { unit: () }\nmain() { str(A { unit = () }) }",
            "expects a number or bool");
  }
  TEST("long field dependency chains use no compiler C recursion") {
    size_t count = 2050, capacity = count * 64, length = 0;
    char *text = arena_alloc(&a, capacity);
    for (size_t i = 0; i < count; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "T%zu { child: T%zu }\n", i, i + 1);
    snprintf(text + length, capacity - length, "T%zu { unit: () }\nmain() {}",
             count);
    Result r = check(&a, text);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->structs[0].equatable);
    ASSERT_EQ_INT(count + 1, r.program->struct_count);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
