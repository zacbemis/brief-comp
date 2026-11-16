#include "structs_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
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
    valid(&a, "P { unit: ()\nsave() -> int { self\n1 } }\n"
              "save(p: P) -> bool { true }\nf(p: P) { let save = 0\n"
              "let a: int = p.save()\nlet b: int = p |> save() }\nmain() {}");
    r = check(&a, "P { unit: ()\nsave() -> int { self\n1 } }\n"
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
    valid(&a, "P { unit: ()\nsame() -> P { self } }\n"
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
  arena_destroy(&a);
  RUN_TESTS();
}
