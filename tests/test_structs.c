#include "ir/ir.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "sema/sema.h"
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
  SourceFile *s = source_create(a, "structs.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static void valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
}
static void invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    if (strstr(r.errors.items[i]->message, message))
      found = 1;
  if (!found) {
    fprintf(stderr, "missing '%s' for %s\n", message, text);
    diagnostics_print(&r.errors, stderr);
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
}
static Result method_error(Arena *a, const char *text, const char *site,
                           const char *advice) {
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != 1) {
      fprintf(stderr, "method diagnostic case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count == 1) {
      Diagnostic *d = r.errors.items[0];
      const char *call = strstr(text, site);
      ASSERT_TRUE(call != NULL);
      ASSERT_EQ_STRN("Unknown method for this type.", d->message,
                     strlen(d->message));
      if (call) {
        ASSERT_EQ_INT((size_t)(call - text), d->span.start);
        ASSERT_EQ_INT((size_t)(call - text) + strlen(site), d->span.end);
      }
      if (advice) {
        ASSERT_TRUE(d->notes != NULL);
        if (d->notes) {
          ASSERT_EQ_STRN(advice, d->notes->message, strlen(d->notes->message));
          ASSERT_TRUE(d->notes->next == NULL);
          SemaSymbol *candidate = NULL;
          for (size_t i = 0; i < r.program->count; i++) {
            SemaSymbol *s = &r.program->functions[i].symbol;
            if (!s->owner && d->notes->span.start == s->span.start)
              candidate = s;
          }
          ASSERT_TRUE(candidate != NULL);
          if (candidate) {
            ASSERT_TRUE(d->notes->span.file == candidate->span.file);
            ASSERT_EQ_INT(candidate->span.end, d->notes->span.end);
          }
        }
      } else
        ASSERT_TRUE(d->notes == NULL);
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
  return r;
}
static Result recursive_lookup_error(Arena *a, const char *text,
                                     const char *site, const char *message,
                                     const char *advice) {
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned repeat = 0; repeat < 2; repeat++) {
    if (repeat) {
      diagnostics_init(&r.errors, a);
      sema_check(r.program, &r.errors);
    }
    if (r.errors.count != 1) {
      fprintf(stderr, "recursive lookup case: %s\n", text);
      diagnostics_print(&r.errors, stderr);
    }
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count == 1) {
      Diagnostic *d = r.errors.items[0];
      const char *at = strstr(text, site);
      ASSERT_TRUE(at != NULL);
      ASSERT_EQ_STRN(message, d->message, strlen(d->message));
      if (at) {
        ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_EQ_INT((size_t)(at - text) + strlen(site), d->span.end);
      }
      if (advice) {
        ASSERT_TRUE(d->notes != NULL);
        if (d->notes) {
          ASSERT_EQ_STRN(advice, d->notes->message, strlen(d->notes->message));
          ASSERT_TRUE(d->notes->next == NULL);
        }
      } else
        ASSERT_TRUE(d->notes == NULL);
    }
    SemaFunction *f = NULL, *g = NULL;
    for (size_t i = 0; i < r.program->count; i++) {
      SemaFunction *fn = &r.program->functions[i];
      if (!strcmp(fn->symbol.name, "f"))
        f = fn;
      if (!strcmp(fn->symbol.name, "g"))
        g = fn;
      ASSERT_TRUE(!fn->inference_conflict);
    }
    ASSERT_TRUE(f != NULL);
    if (f) {
      ASSERT_TRUE(f->symbol.return_type == f->symbol.params[0]->type);
      if (g) {
        ASSERT_EQ_INT(f->component, g->component);
        ASSERT_EQ_INT(SEMA_ERROR, g->symbol.return_type->kind);
      }
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
  return r;
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("checked fields and functions retain written visibility without access "
       "changes") {
    Result r = check(
        &a, "Vault { pub visible: int\nhidden: int\n"
            "pub new(n: int) -> Vault { Vault { visible = n, hidden = 0 } }\n"
            "read(v: Vault) -> int { v.hidden } }\n"
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
            "read(item: Forward) -> int { item.n } }\n"
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
  TEST("self syntax is rejected before fabricated receiver signatures or "
       "bodies") {
    const char *bodies[] = {"self",
                            "(( /* group */ self ))",
                            "self.n",
                            "self.read()",
                            "unknown(self)",
                            "not self",
                            "self == unknown",
                            "unknown == self",
                            ":!bad[self]",
                            ":pair[unknown, self]",
                            "Arr[Missing] { unknown, self }",
                            "Other { first = unknown, second = self }",
                            "self.Inner { n = unknown }",
                            "self.(unknown)",
                            "unknown.(self)",
                            "let value = self",
                            "const value = self",
                            "var value = self",
                            "ret self",
                            "self.n++",
                            "other = self",
                            "self.n = unknown",
                            "other += self",
                            "if self { unknown } else { unknown }",
                            "if false { self } else { unknown }",
                            "if true { unknown } else { self }",
                            "while self { break }",
                            "while true { self\nbreak }",
                            "while true { continue\nself }",
                            "for value in self { unknown }",
                            "for value in 0..2 { self }",
                            "unknown |> { :tag => self\n_ => unknown }",
                            "self?",
                            "self!",
                            "self |> unknown()",
                            "unknown |> self.read()"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      char text[2048];
      snprintf(text, sizeof(text),
               "P { n: Missing\nf(value: Unknown) -> Missing { %s } }\nAlias = "
               "Cycle\nCycle = Alias\n",
               bodies[i]);
      Result r = check(&a, text);
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_EQ_INT(0, r.program->struct_count);
      ASSERT_EQ_INT(0, r.program->alias_count);
      ASSERT_EQ_INT(0, r.program->count);
      ASSERT_EQ_INT(1, r.program->tag_count);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      Diagnostic *d = r.errors.items[0];
      ASSERT_EQ_INT(
          0, strcmp("'self' receivers are not implemented yet.", d->message));
      ASSERT_TRUE(!d->fixes && !d->notes);
      ASSERT_EQ_INT(4, d->span.end - d->span.start);
      ASSERT_TRUE(!memcmp("self", d->span.file->text + d->span.start, 4));
    }
    Result header = check(
        &a, "P { n: Missing\nf(&self, n: Missing) -> Missing { unknown() } }");
    ASSERT_EQ_INT(1, header.errors.count);
    ASSERT_EQ_INT('&',
                  header.errors.items[0]
                      ->span.file->text[header.errors.items[0]->span.start]);
    Result outside = check(&a, "f() -> Missing { self\nunknown() }");
    ASSERT_EQ_INT(1, outside.errors.count);
    ASSERT_EQ_INT(
        0, strcmp("'self' is only available in struct or trait functions.",
                  outside.errors.items[0]->message));
    Result both = check(&a, "P { n: Missing\nf() { self }\ng(&self) { "
                            "unknown() } }\nmain() { self }");
    ASSERT_EQ_INT(3, both.errors.count);
    Result trait = check(
        &a,
        "T { f(self) -> Missing\ng(&self) { self.read() } }\nmain() { self }");
    ASSERT_EQ_INT(1, trait.errors.count);
    ASSERT_EQ_INT(0, strcmp("A struct needs at least one field; traits are not "
                            "implemented yet.",
                            trait.errors.items[0]->message));
    Result conformance =
        check(&a, "P: Trait { n: Missing\nf(&self) { self } }");
    ASSERT_EQ_INT(1, conformance.errors.count);
    ASSERT_EQ_INT(0, strcmp("Trait conformance is not implemented yet.",
                            conformance.errors.items[0]->message));
    valid(&a, "Self = int\nP { n: Self\nf(p: P) { p.n } }\nmain() { "
              "print(\"self\")\nlet selfish = 1\n(P { n = selfish }).f() }");
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
  TEST("retired Self shorthand has exact owner-aware fixes with no cascades") {
    const char *cases[] = {"f(b: &/* keep */ Self) -> int { b.n }",
                           "f() -> Self { Box { n = 1 } }",
                           "f() -> :value[Self] { :value[Box { n = 1 }] }",
                           "f() { let b: Self = Box { n = 1 }\nb }",
                           "f() -> Box { Self { n = 1 } }",
                           "f() -> Box { Self.make() }",
                           "f() -> Box { (/* keep */ Self).make() }",
                           "f() -> Box { (/* keep */\nSelf\n) { n = 1 } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[512], text[1024];
        snprintf(lf, sizeof(lf),
                 "Box { n: int\n\nmake() -> Box { Box { n = 1 } "
                 "}\n%s\n}\nmain() {}\n",
                 cases[i]);
        size_t n = 0;
        for (size_t j = 0; lf[j]; j++) {
          if (crlf && lf[j] == '\n')
            text[n++] = '\r';
          text[n++] = lf[j];
        }
        text[n] = 0;
        Result r = check(&a, text);
        ASSERT_EQ_INT(1, r.errors.count);
        ASSERT_TRUE(!r.program->resolved && !r.program->checked);
        if (r.errors.count != 1) {
          diagnostics_print(&r.errors, stderr);
          continue;
        }
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(
            0,
            strcmp("There is no implicit 'Self' type; name the enclosing type.",
                   d->message));
        ASSERT_TRUE(d->span.file == r.program->module);
        ASSERT_EQ_INT((size_t)(strstr(text, "Self") - text), d->span.start);
        ASSERT_EQ_INT(4, d->span.end - d->span.start);
        ASSERT_TRUE(d->fixes && !d->fixes->next && !d->notes);
        if (d->fixes) {
          ASSERT_EQ_INT(d->span.start, d->fixes->span.start);
          ASSERT_EQ_INT(d->span.end, d->fixes->span.end);
          ASSERT_EQ_INT(0, strcmp("Box", d->fixes->replacement));
          char fixed[1024];
          snprintf(fixed, sizeof(fixed), "%.*s%s%s", (int)d->span.start, text,
                   d->fixes->replacement, text + d->span.end);
          valid(&a, fixed);
          ASSERT_EQ_INT(0, strcmp(text, d->span.file->text));
        }
      }
    }
    Result fields = check(&a, "A { a: Self }\nB { b: Self }\nmain() {}");
    ASSERT_EQ_INT(2, fields.errors.count);
    if (fields.errors.count == 2) {
      ASSERT_EQ_INT(0, strcmp("A", fields.errors.items[0]->fixes->replacement));
      ASSERT_EQ_INT(0, strcmp("B", fields.errors.items[1]->fixes->replacement));
    }
  }
  TEST("member ownership, concrete types, associated and receiver calls") {
    valid(&a, "A { x: int\nnew() -> A { A { x = 1 } }\n"
              "get(a: A) { a.x } }\nmain() { A.new().get() }");
    valid(&a, "A { unit: ()\nget(a: A) { 1 } }\n"
              "B { unit: ()\nget(b: B) { true } }\nget() {}\n"
              "main() { let n: int = A { unit = () }.get()\n"
              "let b: bool = B { unit = () }.get()\nget() }");
    valid(&a, "A { unit: ()\nmain() {} }\nmain() {}");
    valid(&a, "A { unit: ()\nmake() -> A { if true { ret A { unit = () } }\n"
              "A.make() } }\nmain() { A.make() }");
    invalid(&a, "A { unit: ()\nf() {}\nf() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "A { unit: ()\nf() {}\nf() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "impl int {}\nmain() {}", "without 'impl'");
    invalid(&a, "main() {}\nf(a: Self) {}", "type is unknown");
    invalid(&a, "A { unit: () }\nmain() { A.nope() }", "Unknown associated");
    invalid(&a, "A { unit: () }\nf(a: A) {}\nmain() { A { unit = () }.f() }",
            "Unknown method");
    invalid(&a, "A { unit: ()\nf() {} }\nmain() { A { unit = () }.f() }",
            "first parameter");
    invalid(&a, "A { unit: ()\nf(a: int) {} }\nmain() { A { unit = () }.f() }",
            "first parameter");
    invalid(&a, "A { unit: ()\nf(a: A) {} }\nmain() { A { unit = () }.f(1) }",
            "Wrong number");
    invalid(&a, "A { unit: ()\nf(a: A) {} }\n", "must define main");
    invalid(&a, "main() { \"x\".f() }", "Unknown method");
  }
  TEST("unknown methods suggest only visible compatible top-level functions") {
    const char *one = "This is a top-level function, not a method; use "
                      "save(value) or value |> save().";
    const char *many = "This is a top-level function, not a method; use "
                       "save(value, ...) or value |> save(...).";
    struct {
      const char *definition, *body, *site, *note;
    } cases[] = {
        {"save(value: P) {}", "f(p: P) { p.save() }", "p.save()", one},
        {"save(value: P) {}", "f() { P { unit = () }.save() }",
         "P { unit = () }.save()", one},
        {"save(value: P) {}\nmake() { P { unit = () } }",
         "f() { make().save() }", "make().save()", one},
        {"save(value: P) {}", "f(h: Holder) { h.p.save() }", "h.p.save()", one},
        {"save(value: P) {}",
         "f(p: P, b: bool) { (if b { p } else { P { unit = () } }).save() }",
         "(if b { p } else { P { unit = () } }).save()", one},
        {"save(value: &P) {}", "f(p: &P) { p.save() }", "p.save()", one},
        {"save(value: &P) {}", "f(p: P) { p.save() }", "p.save()", one},
        {"save(value: P, n: int) {}", "f(p: P) { p.save(1) }", "p.save(1)",
         many},
        {"save(value: P, n: int) {}", "f(p: P) { p.save(false) }",
         "p.save(false)", many},
        {"save(value: int) {}", "f(p: int) { p.save() }", "p.save()", one},
        {"save(value: :a | :b) {}", "f(p: :a) { p.save() }", "p.save()", one},
        {"save(value: P) {}", "f(p: P) { if true { let save = 0 }\np.save() }",
         "p.save()", one},
        {"save(value: P) {}", "f(p: P) { p.save()\nlet save = 0 }", "p.save()",
         one},
        {"save(value: P) {}", "f(p: Q) { p.save() }", "p.save()", NULL},
        {"", "f(p: P) { p.save() }", "p.save()", NULL},
        {"Q { unit: ()\nsave(p: Q) {} }", "f(p: P) { p.save() }", "p.save()",
         NULL},
        {"save() {}", "f(p: P) { p.save() }", "p.save()", NULL},
        {"save(value: P, n: int) {}", "f(p: P) { p.save() }", "p.save()", NULL},
        {"save(value: P) {}", "f(p: P) { p.save(1) }", "p.save(1)", NULL},
        {"save(value: P) {}", "f(p: P) { let save = 0\np.save() }", "p.save()",
         NULL},
        {"save(value: P) {}", "f(p: P, save: int) { p.save() }", "p.save()",
         NULL},
        {"", "f() { \"text\".print() }", "\"text\".print()", NULL}};
    char code[2048], crlf[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code),
               "P { unit: () }\n%s\nHolder { p: P }\n%s\n%s\nmain() {}",
               !strncmp(cases[i].definition, "Q {", 3) ? "" : "Q { unit: () }",
               cases[i].definition, cases[i].body);
      size_t n = 0;
      for (size_t j = 0; code[j]; j++) {
        if (code[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = code[j];
      }
      crlf[n] = '\0';
      method_error(&a, code, cases[i].site, cases[i].note);
      method_error(&a, crlf, cases[i].site, cases[i].note);
    }
    Result r = method_error(&a,
                            "P { unit: () }\nf(p: P) { p.archive() "
                            "}\narchive(value: P) {}\nmain() {}",
                            "p.archive()",
                            "This is a top-level function, not a method; use "
                            "archive(value) or value |> archive().");
    SemaExpr *call = r.program->functions[0].body->as.block.items[0];
    ASSERT_TRUE(call->as.call.target == NULL);
    ASSERT_TRUE(call->as.call.fallback == &r.program->functions[1].symbol);
    ASSERT_TRUE(r.program->functions[0].edges == NULL);
    method_error(
        &a,
        "P { unit: () }\nsave(value: P) {}\n"
        "f(p: P, b: bool) { if b { ret p }\nforward(p, b)\np.save()\np }\n"
        "forward(p: P, b: bool) { if b { ret p }\nf(p, b) }\nmain() {}",
        "p.save()", one);
    valid(&a, "P { unit: () }\nsave(value: P) {}\nf(p: P) { save(p)\np |> "
              "save() }\nmain() {}");
    valid(&a,
          "P { unit: () }\nsave(value: &P) {}\nf(p: &P) { save(p)\np |> save() "
          "}\nmain() {}");
    valid(&a, "P { unit: () }\nsave(value: P, n: int) {}\n"
              "f(p: P) { save(p, 1)\np |> save(1) }\nmain() {}");
    valid(&a, "P { unit: ()\nsave(p: P) -> int { 1 } }\n"
              "save(p: P) -> bool { true }\nf(p: P) { let save = 0\n"
              "let a: int = p.save()\nlet b: int = p |> save() }\nmain() {}");
    r = check(&a, "P { unit: ()\nsave(p: P) -> int { 1 } }\n"
                  "save(p: P) -> bool { true }\nf(p: P) {\n"
                  "let a: int = p.save()\nlet b: int = p |> save()\n"
                  "let c: bool = save(p) }\nmain() {}");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) != NULL);
    invalid(&a,
            "P { unit: () }\nsave(value: P, n: int) {}\n"
            "f(p: P) { save(p, false) }\nmain() {}",
            "Function argument has the wrong type.");
    invalid(
        &a,
        "P { unit: () }\nsave(value: &P) {}\nf(p: P) { save(p) }\nmain() {}",
        "A read-only value cannot be passed to an & parameter");
    r = check(
        &a,
        "P { unit: () }\nsave(value: P) {}\nf() { missing.save() }\nmain() {}");
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count)
      ASSERT_TRUE(r.errors.items[0]->notes == NULL);
    r = check(&a, "P { unit: () }\nsave(value: P) {}\ntake(value: int) {}\n"
                  "f(p: P) { p.save()\ntake(false) }\nmain() {}");
    ASSERT_EQ_INT(2, r.errors.count);
    if (r.errors.count == 2) {
      ASSERT_TRUE(r.errors.items[0]->notes != NULL);
      ASSERT_EQ_STRN("Function argument has the wrong type.",
                     r.errors.items[1]->message,
                     strlen(r.errors.items[1]->message));
      ASSERT_TRUE(r.errors.items[1]->notes == NULL);
    }
  }
  TEST(
      "recursive lookup failures retain their receiver anchor and diagnostic") {
    struct {
      const char *type, *body, *site, *message;
    } cases[] = {
        {"P", "f(p, b).missing", "f(p, b).missing", "Unknown struct field."},
        {"P", "f(p, b).missing()", "f(p, b).missing()",
         "Unknown method for this type."},
        {"P", "f(p, b).missing(1)", "f(p, b).missing(1)",
         "Unknown method for this type."},
        {"P", "f(p, b) |> absent()", "absent()", "Unknown function."},
        {"P", "let absent = 0\nf(p, b) |> absent()", "absent()",
         "A local binding is not callable."},
        {"int", "f(p, b).missing", "f(p, b).missing",
         "Field access requires a struct."},
        {"str", "f(p, b).missing()", "f(p, b).missing()",
         "Unknown method for this type."},
        {":a | :b", "f(p, b).missing", "f(p, b).missing",
         "Field access requires a struct."},
        {":a | :b", "f(p, b).missing()", "f(p, b).missing()",
         "Unknown method for this type."},
        {"P", "let alias = f(p, b)\nalias.missing", "alias.missing",
         "Unknown struct field."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char f[512], g[512], text[2048], crlf[4096];
      snprintf(f, sizeof(f), "f(p: %s, b: bool) { if b { ret p }\ng(p, b) }\n",
               cases[i].type);
      snprintf(g, sizeof(g), "g(p: %s, b: bool) { %s }\n", cases[i].type,
               cases[i].body);
      for (unsigned order = 0; order < 2; order++) {
        snprintf(text, sizeof(text), "P { unit: () }\n%s%smain() {}",
                 order ? g : f, order ? f : g);
        size_t n = 0;
        for (size_t j = 0; text[j]; j++) {
          if (text[j] == '\n')
            crlf[n++] = '\r';
          crlf[n++] = text[j];
        }
        crlf[n] = '\0';
        recursive_lookup_error(&a, text, cases[i].site, cases[i].message, NULL);
        recursive_lookup_error(&a, crlf, cases[i].site, cases[i].message, NULL);
      }
    }
    recursive_lookup_error(&a,
                           "P { unit: () }\nf(p: P, b: bool) { if b { ret p "
                           "}\nf(p, b).missing }\nmain() {}",
                           "f(p, b).missing", "Unknown struct field.", NULL);
    recursive_lookup_error(&a,
                           "P { unit: () }\nmissing(p: P) { p }\n"
                           "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
                           "g(p: P, b: bool) { f(p, b).missing() }\nmain() {}",
                           "f(p, b).missing()", "Unknown method for this type.",
                           "This is a top-level function, not a method; use "
                           "missing(value) or value |> missing().");
    Result r = check(
        &a, "P { unit: () }\nf(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
            "g(p: P, b: bool) { f(p, b).missing }\n"
            "wrong() -> int { false }\nmain() {}");
    ASSERT_EQ_INT(2, r.errors.count);
    if (r.errors.count == 2) {
      ASSERT_EQ_STRN("Unknown struct field.", r.errors.items[0]->message,
                     strlen(r.errors.items[0]->message));
      ASSERT_EQ_STRN("Function body does not match the written return type.",
                     r.errors.items[1]->message,
                     strlen(r.errors.items[1]->message));
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    r = check(&a, "P { unit: () }\nf(p: P) { g(p) }\ng(p: P) { f(p).missing "
                  "}\nmain() {}");
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count)
      ASSERT_EQ_STRN("Cannot infer recursive return type; write a return type.",
                     r.errors.items[0]->message,
                     strlen(r.errors.items[0]->message));
    r = check(&a,
              "P { unit: () }\nf(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: P, b: bool) { unknown.missing }\nmain() {}");
    ASSERT_EQ_INT(1, r.errors.count);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    if (r.errors.count)
      ASSERT_EQ_STRN("Unknown binding.", r.errors.items[0]->message,
                     strlen(r.errors.items[0]->message));
    valid(&a, "P { unit: ()\nsame(p: P) -> P { p } }\n"
              "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: P, b: bool) { f(p, true).same() }\nmain() {}");
    valid(&a, "P { unit: () }\nsame(p: P) { p }\n"
              "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: P, b: bool) { f(p, true) |> same() }\nmain() {}");
    valid(&a, "Link { next: :end | :more[Link] }\n"
              "f(p: Link, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: Link, b: bool) { f(p, true).next |> { :end => "
              "p\n:more[next] => next } }\n"
              "main() { f(Link { next = :end }, false) }");
  }
  TEST("read-only aliases and immutable parameter bindings") {
    valid(&a, "A { n: int }\nf(a: &A) { a.n++ }\nmain() { let a = A { n = 1 "
              "}\nf(a) }");
    invalid(&a, "A { n: int }\nf(a: A) { a.n++ }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A { n: int }\nf(a: A) { a.n = 2 }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A { n: int }\nB { a: A }\nf(b: B) { b.a.n += 1 }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A { unit: () }\nf(a: &A) {}\ng(a: A) { f(a) }\nmain() {}",
            "read-only value cannot be passed");
    invalid(&a,
            "A { unit: () }\nB { a: A }\nf(a: &A) {}\ng(b: B) { f(b.a) "
            "}\nmain() {}",
            "read-only value cannot be passed");
    invalid(&a, "A { unit: () }\nf(a: &A) { a = A { unit = () } }\nmain() {}",
            "immutable binding");
    invalid(&a, "main() {}\nf(a: &str) {}", "requires a struct or symbol type");
    valid(&a, "A { n: int\nbump(a: &A) { a.n++ } }\n"
              "g(a: &A) { a.bump() }\nmain() { g(A { n = 1 }) }");
    invalid(&a,
            "A { n: int\nbump(a: &A) { a.n++ } }\n"
            "g(a: A) { a.bump() }\nmain() {}",
            "read-only value cannot be passed");
    invalid(&a, "A { n: int }\nf(a: A) { let alias = a\nalias.n++ }\nmain() {}",
            "Cannot mutate through");
  }
  TEST("permissions follow fields, copies, assignments and branch results") {
    const char *prefix = "A { n: int }\nQ { p: A }\nmut(a: &A) {}\nmain() {}\n";
    const char *bad[] = {
        "f(q: Q) { let inner = q.p\ninner.n = 1 }",
        "f(q: Q) { var inner = q.p\nlet other = inner\nother.n++ }",
        "f(q: Q) { let inner = q.p\nmut(inner) }",
        "f(q: Q) { var inner = A { n = 0 }\ninner = q.p\nmut(inner) }",
        "f(q: Q, b: bool) { let inner = if b { q.p } else { A { n = 0 } "
        "}\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n = 0 }\nif b { inner = q.p "
        "}\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n = 0 }\nif b { inner = q.p } else "
        "{ inner = A { n = 1 } }\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n = 0 }\nif b { inner = A { n = 1 "
        "} "
        "} else { inner = q.p }\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n = 0 }\nlet ignored = b and if "
        "true { inner = q.p\ntrue } else { false }\nmut(inner) }",
        // Earlier argument permission must not change after a later rebind.
        "g(a: &A, u: ()) {}\nf(q: Q) { var inner = q.p\ng(inner, if true { "
        "inner = A { n = 0 } } else {}) }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%s", prefix, bad[i]);
      invalid(&a, text,
              i < 2 ? "Cannot mutate through"
                    : "read-only value cannot be passed");
    }
    invalid(&a, "A { n: int }\nf(a: A) { while true {}\na.n++ }\nmain() {}",
            "Cannot mutate through");
    valid(&a, "A { n: int }\nf(a: A) { var copy = a.n\ncopy++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: &A) { let alias = a\nalias.n++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = a\nalias = A { n = 1 "
              "}\nalias.n++ }\nmain() {}");
    valid(&a,
          "A { n: int }\nf(a: A, b: bool) { var alias = a\nif b { alias = A { "
          "n = 1 } } else { alias = A { n = 2 } }\nalias.n++ }\nmain() {}");
    valid(&a,
          "A { n: int }\nf(a: A, b: bool) { var alias = A { n = 0 }\nif b { "
          "alias = a\nret }\nalias.n++ }\nmain() {}");
    invalid(&a,
            "A { n: int }\nidentity(a: A) -> A { a }\nf(a: A) { let owned = "
            "identity(a)\nowned.n++ }\nmain() {}",
            "Cannot mutate through");
  }
  TEST("calls and literals preserve read-only access conservatively") {
    const char *prefix =
        "A { n: int\nsame(a: A) -> A { a }\nnew(a: A) -> A { A { n = 0 } } }\n"
        "Q { p: A }\nR { p: A\nother: A\nn: int }\n"
        "U { p: A\nu: () }\nidentity(a: A) -> A { a }\n"
        "pick(a: A, b: A) -> A { a }\nafter(a: A, u: ()) -> A { a }\n"
        "number(a: A) -> int { a.n }\nmut(a: &A) {}\nmain() {}\n";
    const char *bad[] = {
        "f(q: Q) { let inner = identity(q.p)\ninner.n++ }",
        "f(q: Q) { identity(identity(q.p)).n++ }", "f(q: Q) { q.p.same().n++ }",
        "f(q: Q) { A.same(q.p).n++ }",
        // Even a genuinely fresh result is restricted if a struct argument is.
        "f(q: Q) { A.new(q.p).n++ }", "f(q: Q) { pick(A { n = 0 }, q.p).n++ }",
        "f(q: Q) { let wrapper = Q { p = q.p }\nwrapper.p.n++ }",
        "f(q: Q) { let wrapper = R { p = q.p, other = A { n = 0 }, n = 0 }\n"
        "wrapper.other.n++ }",
        "f(q: Q) { let wrapper = R { p = q.p, other = A { n = 0 }, n = 0 }\n"
        "wrapper.n++ }",
        "f(q: Q) { let wrapper = Q { p = Q { p = q.p }.p }\nwrapper.p.n++ }",
        // Capture argument/initializer permissions before later side effects.
        "f(q: Q) { var x = q.p\nlet result = after(x, if true { x = A { n = 0 "
        "} "
        "} else {})\nresult.n++ }",
        "f(q: Q) { var x = q.p\nlet result = U { p = x, u = if true { "
        "x = A { n = 0 } } else {} }\nresult.p.n++ }",
        // Initializers follow written order, not field declaration order.
        "f(q: Q) { var x = A { n = 0 }\nlet result = U { u = if true { "
        "x = q.p } else {}, p = x }\nresult.p.n++ }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, bad[i]);
      invalid(&a, text, "Cannot mutate through");
    }
    const char *borrow[] = {"f(q: Q) { mut(identity(q.p)) }",
                            "f(q: Q) { mut(Q { p = q.p }.p) }"};
    for (size_t i = 0; i < sizeof(borrow) / sizeof(*borrow); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, borrow[i]);
      invalid(&a, text, "read-only value cannot be passed");
    }
    const char *good[] = {
        "f(q: &Q) { identity(q.p).n++\nQ { p = q.p }.p.n++ }",
        "f(q: Q) { identity(A { n = q.p.n }).n++ }",
        "f(q: Q) { var n = number(q.p)\nn++\nA { n = n }.n++ }",
        "f(q: Q) { var x = identity(q.p)\nx = A { n = 0 }\nx.n++ }",
        "f(q: Q) { var x = A { n = 0 }\nlet result = after(x, if true { "
        "x = q.p } else {})\nresult.n++ }",
        "f(q: Q) { var x = A { n = 0 }\nlet result = U { p = x, u = if true { "
        "x = q.p } else {} }\nresult.p.n++ }"};
    for (size_t i = 0; i < sizeof(good) / sizeof(*good); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, good[i]);
      valid(&a, text);
    }
  }
  TEST("loop permissions converge across backedges, break and continue") {
    const char *prefix = "A { n: int }\nQ { p: A }\nidentity(a: A) -> A { a }\n"
                         "mut(a: &A) {}\nmain() {}\n";
    const char *bad[] = {
        "f(a: A, b: bool) { var alias = A { n = 0 }\nwhile b { "
        "mut(alias)\nalias = a } }",
        "f(a: A) { var alias = A { n = 0 }\nfor i in 0..2 { mut(alias)\nalias "
        "= "
        "a\ncontinue } }",
        "f(a: A, b: bool) { var alias = A { n = 0 }\nwhile b { alias = "
        "a\nbreak "
        "}\nmut(alias) }",
        "f(a: A) { var alias = A { n = 0 }\nwhile true { alias = a\nbreak "
        "}\nmut(alias) }",
        "f(a: A) { var alias = A { n = 0 }\nfor i in 0..2 { alias = a "
        "}\nmut(alias) }",
        // Propagation needs multiple iterations through two local names.
        "f(a: A, b: bool) { var x = A { n = 0 }\nvar y = A { n = 1 }\nwhile b "
        "{ "
        "mut(x)\nx = y\ny = a } }",
        "f(a: A, b: bool) { var alias = A { n = 0 }\nwhile b { for i in 0..2 { "
        "alias = a\nbreak }\ncontinue }\nmut(alias) }",
        "f(a: A, b: bool) { var alias = A { n = 0 }\nwhile b { "
        "mut(alias)\nalias = identity(a) } }",
        "f(a: A, b: bool) { var box = Q { p = A { n = 0 } }\nwhile b { "
        "mut(box.p)\nbox = Q { p = a }\ncontinue } }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%s", prefix, bad[i]);
      Result r = check(&a, text);
      ASSERT_EQ_INT(1, r.errors.count); // No repeated fixpoint diagnostics.
      if (r.errors.count)
        ASSERT_TRUE(strstr(r.errors.items[0]->message,
                           "read-only value cannot be passed") != NULL);
      ASSERT_TRUE(!r.program->checked);
      Diagnostics again;
      diagnostics_init(&again, &a);
      sema_check(r.program, &again);
      ASSERT_EQ_INT(1, again.count);
    }
    valid(&a, "A { n: int }\nf(a: A, b: bool) { var alias = a\nwhile b { alias "
              "= A { n = 0 }\nalias.n++ } }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = a\nwhile true { alias = A { "
              "n = 0 }\nbreak }\nalias.n++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = A { n = 0 }\nwhile true { "
              "break\nalias = a }\nalias.n++ }\nmain() {}");
  }
  TEST(
      "nested permission loops do not recursively multiply fixed-point walks") {
    char text[4096];
    size_t n = (size_t)snprintf(text, sizeof(text),
                                "A { unit: () }\nf(a: A, b: bool) {\n");
    for (size_t i = 0; i < 25; i++)
      n += (size_t)snprintf(text + n, sizeof(text) - n, "while b {\n");
    for (size_t i = 0; i < 25; i++)
      n += (size_t)snprintf(text + n, sizeof(text) - n, "}\n");
    snprintf(text + n, sizeof(text) - n, "}\nmain() {}");
    valid(&a, text);
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
    invalid(&a, "main() { int(\"1\") }", "must be handled");
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
