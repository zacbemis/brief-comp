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
  TEST("struct types are canonical, nominal, and visible before their "
       "definitions") {
    Result r = check(&a, "main() { make() }\nmake() { Box { child: Child { n: "
                         "1 } } }\nBox { child: Child }\nChild { n: shrt }");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_EQ_INT(2, r.program->struct_count);
    SemaStruct *box = &r.program->structs[0], *child = &r.program->structs[1];
    ASSERT_TRUE(box->fields[0].type == &child->type);
    ASSERT_TRUE(r.program->functions[0].symbol.return_type == &box->type);
    ASSERT_TRUE(r.program->functions[1].symbol.return_type == &box->type);
    ASSERT_TRUE(child->fields[0].type == &r.program->types[SEMA_SHRT]);
    invalid(&a, "A {}\nB {}\nmain() { let x: A = B {} }", "annotation");
    invalid(&a, "A {}\nB {}\nmain() { A {} == B {} }", "same type");
    valid(&a, "Node { next: Node }\nmain() {}");
    valid(&a, "A { b: B }\nB { a: A }\nmain() {}");
    invalid(&a,
            "Node { next: Node }\nmain() {}\nf(a: Node, b: Node) { a == b }",
            "can contain a reference cycle have no == or !=");
    invalid(&a,
            "Node { next: Node }\nHolder { node: Node }\nmain() {}\n"
            "f(a: Holder, b: Holder) { a != b }",
            "can contain a reference cycle have no == or !=");
    invalid(&a, "A {}\nmain() { A {} < A {} }",
            "Comparison of this type is not implemented yet.");
  }
  TEST("fields and struct literal initialization are complete and typed") {
    invalid(&a, "A { x: int\nx: int }\nmain() {}", "Duplicate struct field");
    invalid(&a, "A {}\nA {}\nmain() {}", "Duplicate or reserved");
    invalid(&a, "A {}\nA() {}\nmain() {}", "cannot share a name");
    invalid(&a, "A { x: int }\nmain() { A {} }", "Missing struct field");
    invalid(&a, "A { x: int }\nmain() { A { x: 1, x: 2 } }",
            "Duplicate struct field initializer");
    invalid(&a, "A {}\nmain() { A { x: 1 } }", "Unknown struct field");
    invalid(&a, "A { x: int }\nmain() { A { x: true } }",
            "initializer has the wrong type");
    invalid(&a, "main() { Missing {} }", "Unknown struct type");
    invalid(&a, "A {}\nmain() { A {}.missing }", "Unknown struct field");
    invalid(&a, "main() { 1.missing }", "Field access requires");
    invalid(&a, "A { x: int }\nmain() { let a = A { x: 0 }\na.x = true }",
            "Assignment value");
    invalid(&a, "A { x: str }\nmain() { let a = A { x: \"x\" }\na.x++ }",
            "numeric binding");
    valid(&a, "A { x: () }\nmain() { let a = A { x: () }\na.x = ()\na.x }");
  }
  TEST("impl membership, Self, associated calls and receiver calls") {
    valid(&a, "A { x: int }\nimpl A { new(): Self { Self { x: 1 } }\nget(a: "
              "Self) { a.x } }\nmain() { A.new().get() }");
    valid(&a, "A {}\nB {}\nimpl A { get(a: Self) { 1 } }\nimpl B { get(b: "
              "Self) { true } }\nget() {}\nmain() { let n: int = A "
              "{}.get()\nlet b: bool = B {}.get()\nget() }");
    valid(&a, "A {}\nimpl A { main() {} }\nmain() {}");
    valid(&a, "A {}\nimpl A { make(): Self { if true { ret Self {} "
              "}\nSelf.make() } }\nmain() { A.make() }");
    invalid(&a, "A {}\nimpl A { f() {}\nf() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "A {}\nimpl A { f() {} }\nimpl A { f() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "impl int {}\nmain() {}", "impl currently requires");
    invalid(&a, "main() {}\nf(a: Self) {}", "type is unknown");
    invalid(&a, "A {}\nmain() { A.nope() }", "Unknown associated");
    invalid(&a, "A {}\nf(a: A) {}\nmain() { A {}.f() }", "Unknown method");
    invalid(&a, "A {}\nimpl A { f() {} }\nmain() { A {}.f() }",
            "first parameter");
    invalid(&a, "A {}\nimpl A { f(a: int) {} }\nmain() { A {}.f() }",
            "first parameter");
    invalid(&a, "A {}\nimpl A { f(a: Self) {} }\nmain() { A {}.f(1) }",
            "Wrong number");
    invalid(&a, "A {}\nimpl A { f(a: Self) {} }", "must define main");
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
        {"save(value: P) {}", "f() { P {}.save() }", "P {}.save()", one},
        {"save(value: P) {}\nmake() { P {} }", "f() { make().save() }",
         "make().save()", one},
        {"save(value: P) {}", "f(h: Holder) { h.p.save() }", "h.p.save()", one},
        {"save(value: P) {}", "f(p: P, b: bool) { (if b { p } else { P {} }).save() }",
         "(if b { p } else { P {} }).save()", one},
        {"save(value: &P) {}", "f(p: &P) { p.save() }", "p.save()", one},
        {"save(value: &P) {}", "f(p: P) { p.save() }", "p.save()", one},
        {"save(value: P, n: int) {}", "f(p: P) { p.save(1) }", "p.save(1)", many},
        {"save(value: P, n: int) {}", "f(p: P) { p.save(false) }",
         "p.save(false)", many},
        {"save(value: int) {}", "f(p: int) { p.save() }", "p.save()", one},
        {"save(value: Sym<:a, :b>) {}", "f(p: Sym<:a>) { p.save() }",
         "p.save()", one},
        {"save(value: P) {}", "f(p: P) { if true { let save = 0 }\np.save() }",
         "p.save()", one},
        {"save(value: P) {}", "f(p: P) { p.save()\nlet save = 0 }",
         "p.save()", one},
        {"save(value: P) {}", "f(p: Q) { p.save() }", "p.save()", NULL},
        {"", "f(p: P) { p.save() }", "p.save()", NULL},
        {"impl Q { save(p: Self) {} }", "f(p: P) { p.save() }",
         "p.save()", NULL},
        {"save() {}", "f(p: P) { p.save() }", "p.save()", NULL},
        {"save(value: P, n: int) {}", "f(p: P) { p.save() }", "p.save()", NULL},
        {"save(value: P) {}", "f(p: P) { p.save(1) }", "p.save(1)", NULL},
        {"save(value: P) {}", "f(p: P) { let save = 0\np.save() }",
         "p.save()", NULL},
        {"save(value: P) {}", "f(p: P, save: int) { p.save() }",
         "p.save()", NULL},
        {"", "f() { \"text\".print() }", "\"text\".print()", NULL}};
    char code[2048], crlf[4096];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snprintf(code, sizeof(code), "P {}\nQ {}\nHolder { p: P }\n%s\n%s\nmain() {}",
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
        "P {}\nf(p: P) { p.archive() }\narchive(value: P) {}\nmain() {}",
        "p.archive()", "This is a top-level function, not a method; use "
                       "archive(value) or value |> archive().");
    SemaExpr *call = r.program->functions[0].body->as.block.items[0];
    ASSERT_TRUE(call->as.call.target == NULL);
    ASSERT_TRUE(call->as.call.fallback == &r.program->functions[1].symbol);
    ASSERT_TRUE(r.program->functions[0].edges == NULL);
    method_error(&a,
        "P {}\nsave(value: P) {}\n"
        "f(p: P, b: bool) { if b { ret p }\nforward(p, b)\np.save()\np }\n"
        "forward(p: P, b: bool) { if b { ret p }\nf(p, b) }\nmain() {}",
        "p.save()", one);
    valid(&a, "P {}\nsave(value: P) {}\nf(p: P) { save(p)\np |> save() }\nmain() {}");
    valid(&a, "P {}\nsave(value: &P) {}\nf(p: &P) { save(p)\np |> save() }\nmain() {}");
    valid(&a, "P {}\nsave(value: P, n: int) {}\n"
              "f(p: P) { save(p, 1)\np |> save(1) }\nmain() {}");
    valid(&a, "P {}\nimpl P { save(p: Self): int { 1 } }\n"
              "save(p: P): bool { true }\nf(p: P) { let save = 0\n"
              "let a: int = p.save()\nlet b: int = p |> save() }\nmain() {}");
    r = check(&a, "P {}\nimpl P { save(p: Self): int { 1 } }\n"
                  "save(p: P): bool { true }\nf(p: P) {\n"
                  "let a: int = p.save()\nlet b: int = p |> save()\n"
                  "let c: bool = save(p) }\nmain() {}");
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) != NULL);
    invalid(&a, "P {}\nsave(value: P, n: int) {}\n"
                "f(p: P) { save(p, false) }\nmain() {}",
            "Function argument has the wrong type.");
    invalid(&a, "P {}\nsave(value: &P) {}\nf(p: P) { save(p) }\nmain() {}",
            "A read-only value cannot be passed to an & parameter");
    r = check(&a, "P {}\nsave(value: P) {}\nf() { missing.save() }\nmain() {}");
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count)
      ASSERT_TRUE(r.errors.items[0]->notes == NULL);
    r = check(&a, "P {}\nsave(value: P) {}\ntake(value: int) {}\n"
                  "f(p: P) { p.save()\ntake(false) }\nmain() {}");
    ASSERT_EQ_INT(2, r.errors.count);
    if (r.errors.count == 2) {
      ASSERT_TRUE(r.errors.items[0]->notes != NULL);
      ASSERT_EQ_STRN("Function argument has the wrong type.",
                     r.errors.items[1]->message, strlen(r.errors.items[1]->message));
      ASSERT_TRUE(r.errors.items[1]->notes == NULL);
    }
  }
  TEST("recursive lookup failures retain their receiver anchor and diagnostic") {
    struct {
      const char *type, *body, *site, *message;
    } cases[] = {
        {"P", "f(p, b).missing", "f(p, b).missing", "Unknown struct field."},
        {"P", "f(p, b).missing()", "f(p, b).missing()", "Unknown method for this type."},
        {"P", "f(p, b).missing(1)", "f(p, b).missing(1)", "Unknown method for this type."},
        {"P", "f(p, b) |> absent()", "absent()", "Unknown function."},
        {"P", "let absent = 0\nf(p, b) |> absent()", "absent()",
         "A local binding is not callable."},
        {"int", "f(p, b).missing", "f(p, b).missing", "Field access requires a struct."},
        {"str", "f(p, b).missing()", "f(p, b).missing()", "Unknown method for this type."},
        {"Sym<:a, :b>", "f(p, b).missing", "f(p, b).missing", "Field access requires a struct."},
        {"Sym<:a, :b>", "f(p, b).missing()", "f(p, b).missing()", "Unknown method for this type."},
        {"P", "let alias = f(p, b)\nalias.missing", "alias.missing", "Unknown struct field."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char f[512], g[512], text[2048], crlf[4096];
      snprintf(f, sizeof(f), "f(p: %s, b: bool) { if b { ret p }\ng(p, b) }\n",
               cases[i].type);
      snprintf(g, sizeof(g), "g(p: %s, b: bool) { %s }\n",
               cases[i].type, cases[i].body);
      for (unsigned order = 0; order < 2; order++) {
        snprintf(text, sizeof(text), "P {}\n%s%smain() {}", order ? g : f,
                 order ? f : g);
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
        "P {}\nf(p: P, b: bool) { if b { ret p }\nf(p, b).missing }\nmain() {}",
        "f(p, b).missing", "Unknown struct field.", NULL);
    recursive_lookup_error(&a,
        "P {}\nmissing(p: P) { p }\n"
        "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
        "g(p: P, b: bool) { f(p, b).missing() }\nmain() {}",
        "f(p, b).missing()", "Unknown method for this type.",
        "This is a top-level function, not a method; use missing(value) or value |> missing().");
    Result r = check(&a,
        "P {}\nf(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
        "g(p: P, b: bool) { f(p, b).missing }\n"
        "wrong(): int { false }\nmain() {}");
    ASSERT_EQ_INT(2, r.errors.count);
    if (r.errors.count == 2) {
      ASSERT_EQ_STRN("Unknown struct field.", r.errors.items[0]->message,
                     strlen(r.errors.items[0]->message));
      ASSERT_EQ_STRN("Function body does not match the written return type.",
                     r.errors.items[1]->message, strlen(r.errors.items[1]->message));
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    r = check(&a, "P {}\nf(p: P) { g(p) }\ng(p: P) { f(p).missing }\nmain() {}");
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    ASSERT_EQ_INT(1, r.errors.count);
    if (r.errors.count)
      ASSERT_EQ_STRN("Cannot infer recursive return type; write a return type.",
                     r.errors.items[0]->message, strlen(r.errors.items[0]->message));
    r = check(&a,
        "P {}\nf(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
        "g(p: P, b: bool) { unknown.missing }\nmain() {}");
    ASSERT_EQ_INT(1, r.errors.count);
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
    if (r.errors.count)
      ASSERT_EQ_STRN("Unknown binding.", r.errors.items[0]->message,
                     strlen(r.errors.items[0]->message));
    valid(&a, "P {}\nimpl P { same(p: Self): Self { p } }\n"
              "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: P, b: bool) { f(p, true).same() }\nmain() {}");
    valid(&a, "P {}\nsame(p: P) { p }\n"
              "f(p: P, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: P, b: bool) { f(p, true) |> same() }\nmain() {}");
    valid(&a, "Link { next: Sym<:end, :more(Link)> }\n"
              "f(p: Link, b: bool) { if b { ret p }\ng(p, b) }\n"
              "g(p: Link, b: bool) { f(p, true).next |> { :end => p\n:more(next) => next } }\n"
              "main() { f(Link { next: :end }, false) }");
  }
  TEST("read-only aliases and immutable parameter bindings") {
    valid(&a, "A { n: int }\nf(a: &A) { a.n++ }\nmain() { let a = A { n: 1 "
              "}\nf(a) }");
    invalid(&a, "A { n: int }\nf(a: A) { a.n++ }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A { n: int }\nf(a: A) { a.n = 2 }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A { n: int }\nB { a: A }\nf(b: B) { b.a.n += 1 }\nmain() {}",
            "Cannot mutate through");
    invalid(&a, "A {}\nf(a: &A) {}\ng(a: A) { f(a) }\nmain() {}",
            "read-only value cannot be passed");
    invalid(&a, "A {}\nB { a: A }\nf(a: &A) {}\ng(b: B) { f(b.a) }\nmain() {}",
            "read-only value cannot be passed");
    invalid(&a, "A {}\nf(a: &A) { a = A {} }\nmain() {}", "immutable binding");
    invalid(&a, "main() {}\nf(a: &str) {}", "requires a struct or symbol type");
    valid(&a, "A { n: int }\nimpl A { bump(a: &Self) { a.n++ } }\ng(a: &A) { "
              "a.bump() }\nmain() { g(A { n: 1 }) }");
    invalid(&a,
            "A { n: int }\nimpl A { bump(a: &Self) { a.n++ } }\ng(a: A) { "
            "a.bump() }\nmain() {}",
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
        "f(q: Q) { var inner = A { n: 0 }\ninner = q.p\nmut(inner) }",
        "f(q: Q, b: bool) { let inner = if b { q.p } else { A { n: 0 } "
        "}\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n: 0 }\nif b { inner = q.p "
        "}\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n: 0 }\nif b { inner = q.p } else "
        "{ inner = A { n: 1 } }\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n: 0 }\nif b { inner = A { n: 1 } "
        "} else { inner = q.p }\nmut(inner) }",
        "f(q: Q, b: bool) { var inner = A { n: 0 }\nlet ignored = b and if "
        "true { inner = q.p\ntrue } else { false }\nmut(inner) }",
        // Earlier argument permission must not change after a later rebind.
        "g(a: &A, u: ()) {}\nf(q: Q) { var inner = q.p\ng(inner, if true { "
        "inner = A { n: 0 } } else {}) }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[2048];
      snprintf(text, sizeof(text), "%s%s", prefix, bad[i]);
      invalid(&a, text, i < 2 ? "Cannot mutate through"
                              : "read-only value cannot be passed");
    }
    invalid(&a, "A { n: int }\nf(a: A) { while true {}\na.n++ }\nmain() {}",
            "Cannot mutate through");
    valid(&a, "A { n: int }\nf(a: A) { var copy = a.n\ncopy++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: &A) { let alias = a\nalias.n++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = a\nalias = A { n: 1 "
              "}\nalias.n++ }\nmain() {}");
    valid(&a,
          "A { n: int }\nf(a: A, b: bool) { var alias = a\nif b { alias = A { "
          "n: 1 } } else { alias = A { n: 2 } }\nalias.n++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A, b: bool) { var alias = A { n: 0 }\nif b { "
              "alias = a\nret }\nalias.n++ }\nmain() {}");
    invalid(&a,
            "A { n: int }\nidentity(a: A): A { a }\nf(a: A) { let owned = "
            "identity(a)\nowned.n++ }\nmain() {}",
            "Cannot mutate through");
  }
  TEST("calls and literals preserve read-only access conservatively") {
    const char *prefix =
        "A { n: int }\nQ { p: A }\nR { p: A\nother: A\nn: int }\n"
        "U { p: A\nu: () }\nidentity(a: A): A { a }\n"
        "pick(a: A, b: A): A { a }\nafter(a: A, u: ()): A { a }\n"
        "number(a: A): int { a.n }\nmut(a: &A) {}\n"
        "impl A { same(a: Self): Self { a }\n"
        "new(a: Self): Self { Self { n: 0 } } }\nmain() {}\n";
    const char *bad[] = {
        "f(q: Q) { let inner = identity(q.p)\ninner.n++ }",
        "f(q: Q) { identity(identity(q.p)).n++ }", "f(q: Q) { q.p.same().n++ }",
        "f(q: Q) { A.same(q.p).n++ }",
        // Even a genuinely fresh result is restricted if a struct argument is.
        "f(q: Q) { A.new(q.p).n++ }", "f(q: Q) { pick(A { n: 0 }, q.p).n++ }",
        "f(q: Q) { let wrapper = Q { p: q.p }\nwrapper.p.n++ }",
        "f(q: Q) { let wrapper = R { p: q.p, other: A { n: 0 }, n: 0 }\n"
        "wrapper.other.n++ }",
        "f(q: Q) { let wrapper = R { p: q.p, other: A { n: 0 }, n: 0 }\n"
        "wrapper.n++ }",
        "f(q: Q) { let wrapper = Q { p: Q { p: q.p }.p }\nwrapper.p.n++ }",
        // Capture argument/initializer permissions before later side effects.
        "f(q: Q) { var x = q.p\nlet result = after(x, if true { x = A { n: 0 } "
        "} else {})\nresult.n++ }",
        "f(q: Q) { var x = q.p\nlet result = U { p: x, u: if true { "
        "x = A { n: 0 } } else {} }\nresult.p.n++ }",
        // Initializers follow written order, not field declaration order.
        "f(q: Q) { var x = A { n: 0 }\nlet result = U { u: if true { "
        "x = q.p } else {}, p: x }\nresult.p.n++ }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, bad[i]);
      invalid(&a, text, "Cannot mutate through");
    }
    const char *borrow[] = {"f(q: Q) { mut(identity(q.p)) }",
                            "f(q: Q) { mut(Q { p: q.p }.p) }"};
    for (size_t i = 0; i < sizeof(borrow) / sizeof(*borrow); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, borrow[i]);
      invalid(&a, text, "read-only value cannot be passed");
    }
    const char *good[] = {
        "f(q: &Q) { identity(q.p).n++\nQ { p: q.p }.p.n++ }",
        "f(q: Q) { identity(A { n: q.p.n }).n++ }",
        "f(q: Q) { var n = number(q.p)\nn++\nA { n: n }.n++ }",
        "f(q: Q) { var x = identity(q.p)\nx = A { n: 0 }\nx.n++ }",
        "f(q: Q) { var x = A { n: 0 }\nlet result = after(x, if true { "
        "x = q.p } else {})\nresult.n++ }",
        "f(q: Q) { var x = A { n: 0 }\nlet result = U { p: x, u: if true { "
        "x = q.p } else {} }\nresult.p.n++ }"};
    for (size_t i = 0; i < sizeof(good) / sizeof(*good); i++) {
      char text[4096];
      snprintf(text, sizeof(text), "%s%s", prefix, good[i]);
      valid(&a, text);
    }
  }
  TEST("loop permissions converge across backedges, break and continue") {
    const char *prefix = "A { n: int }\nQ { p: A }\nidentity(a: A): A { a }\n"
                         "mut(a: &A) {}\nmain() {}\n";
    const char *bad[] = {
        "f(a: A, b: bool) { var alias = A { n: 0 }\nwhile b { "
        "mut(alias)\nalias = a } }",
        "f(a: A) { var alias = A { n: 0 }\nfor i in 0..2 { mut(alias)\nalias = "
        "a\ncontinue } }",
        "f(a: A, b: bool) { var alias = A { n: 0 }\nwhile b { alias = a\nbreak "
        "}\nmut(alias) }",
        "f(a: A) { var alias = A { n: 0 }\nwhile true { alias = a\nbreak "
        "}\nmut(alias) }",
        "f(a: A) { var alias = A { n: 0 }\nfor i in 0..2 { alias = a "
        "}\nmut(alias) }",
        // Propagation needs multiple iterations through two local names.
        "f(a: A, b: bool) { var x = A { n: 0 }\nvar y = A { n: 1 }\nwhile b { "
        "mut(x)\nx = y\ny = a } }",
        "f(a: A, b: bool) { var alias = A { n: 0 }\nwhile b { for i in 0..2 { "
        "alias = a\nbreak }\ncontinue }\nmut(alias) }",
        "f(a: A, b: bool) { var alias = A { n: 0 }\nwhile b { "
        "mut(alias)\nalias = identity(a) } }",
        "f(a: A, b: bool) { var box = Q { p: A { n: 0 } }\nwhile b { "
        "mut(box.p)\nbox = Q { p: a }\ncontinue } }"};
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
              "= A { n: 0 }\nalias.n++ } }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = a\nwhile true { alias = A { "
              "n: 0 }\nbreak }\nalias.n++ }\nmain() {}");
    valid(&a, "A { n: int }\nf(a: A) { var alias = A { n: 0 }\nwhile true { "
              "break\nalias = a }\nalias.n++ }\nmain() {}");
  }
  TEST(
      "nested permission loops do not recursively multiply fixed-point walks") {
    char text[4096];
    size_t n =
        (size_t)snprintf(text, sizeof(text), "A {}\nf(a: A, b: bool) {\n");
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
    invalid(&a, "A {}\nmain() { str(A {}) }", "expects a number or bool");
  }
  TEST("long field dependency chains use no compiler C recursion") {
    size_t count = 2050, capacity = count * 64, length = 0;
    char *text = arena_alloc(&a, capacity);
    for (size_t i = 0; i < count; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "T%zu { child: T%zu }\n", i, i + 1);
    snprintf(text + length, capacity - length, "T%zu {}\nmain() {}", count);
    Result r = check(&a, text);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(r.program->structs[0].equatable);
    ASSERT_EQ_INT(count + 1, r.program->struct_count);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
