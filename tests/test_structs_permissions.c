#include "structs_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
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
    valid(&a, "A { n: int\nbump(&self) { self.n++ } }\n"
              "g(a: &A) { a.bump() }\nmain() { g(A { n = 1 }) }");
    invalid(&a,
            "A { n: int\nbump(&self) { self.n++ } }\n"
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
        "A { n: int\nsame() -> A { self }\nnew(a: A) -> A { A { n = 0 } } }\n"
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
  arena_destroy(&a);
  RUN_TESTS();
}
