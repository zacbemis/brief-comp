#include "structs_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("syntactic self classification precedes body checking and inference") {
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
               "P { n: int\nf(value: int) -> () { %s } }\nmain() {}\n",
               bodies[i]);
      Result r = check(&a, text);
      // Many bodies deliberately contain unsupported or ill-typed operations.
      // Method classification and canonical receiver binding must not depend on
      // whether those operations resolve/check or on return-type probes.
      ASSERT_EQ_INT(1, r.program->struct_count);
      ASSERT_EQ_INT(2, r.program->count);
      SemaSymbol *method = &r.program->functions[0].symbol;
      ASSERT_EQ_INT(RECEIVER_SELF, method->receiver);
      ASSERT_EQ_INT(2, method->arity);
      SemaVar *self = method->params[0];
      ASSERT_TRUE(self->type == &r.program->structs[0].type);
      ASSERT_TRUE(!self->data_mutable && !self->mutable);
      ASSERT_EQ_INT(0, strcmp("self", self->name));
      ASSERT_EQ_INT(4, self->span.end - self->span.start);
      ASSERT_TRUE(!memcmp("self", self->span.file->text + self->span.start, 4));
      ASSERT_EQ_INT(0, strcmp("value", method->params[1]->name));
    }
    Result header = check(&a, "P { n: int\nf(&self, n: int) {} }\nmain() {}\n");
    ASSERT_EQ_INT(0, header.errors.count);
    SemaSymbol *mutable = &header.program->functions[0].symbol;
    ASSERT_EQ_INT(RECEIVER_MUT_SELF, mutable->receiver);
    ASSERT_EQ_INT(2, mutable->arity);
    ASSERT_TRUE(mutable->params[0]->data_mutable &&
                !mutable->params[0]->mutable);
    ASSERT_TRUE(mutable->params[0]->type == &header.program->structs[0].type);
    ASSERT_EQ_INT(
        '&',
        mutable->params[0]->span.file->text[mutable->params[0]->span.start]);
    Result outside = check(&a, "f() -> Missing { self\nunknown() }");
    ASSERT_EQ_INT(1, outside.errors.count);
    ASSERT_EQ_INT(
        0, strcmp("'self' is only available in struct or trait functions.",
                  outside.errors.items[0]->message));
    Result both = check(&a, "P { n: Missing\nf() { self }\ng(&self) { "
                            "unknown() } }\nmain() { self }");
    ASSERT_EQ_INT(1, both.errors.count);
    ASSERT_EQ_INT(0, both.program->struct_count);
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
    valid(&a, "Self = int\nP { n: Self\nf() { self.n } }\nmain() { "
              "print(\"self\")\nlet selfish = 1\n(P { n = selfish }).f() }");
  }
  TEST("wrong-way calls carry source-preserving fixes, not guessed receivers") {
    const char *prefix =
        "Date { day: int\nlabel() -> int { self.day }\ncombine(n: int) -> int "
        "{ self.day + n }\nnew(n: int) -> Date { Date { day = n } "
        "}\ncopy(value: Date) -> Date { value }\nzero() -> int { 0 } }\nView = "
        "Date\nHolder "
        "{ d: Date }\nmake() -> Date { Date { day = 1 } }\narg() -> int { 2 "
        "}\nmain() { let d = Date { day = 1 }\nlet holder = Holder { d = d }\n";
    struct {
      const char *call, *fixed;
      int method;
    } cases[] = {{"Date.label(d)", "d.label()", 1},
                 {"View.label(d)", "d.label()", 1},
                 {"Date.combine(d,  \targ())", "d.combine(arg())", 1},
                 {"Date.combine(d, \t\n arg())", "d.combine(\n arg())", 1},
                 {"Date.combine(make(), arg())", "(make()).combine(arg())", 1},
                 {"( /* T */ Date ).label( /* A */ (d), )",
                  "( /* T */ ( /* A */ (d)) ).label()", 1},
                 {"Date.label(// arg\n d)", "(// arg\n d).label()", 1},
                 {"d.new(arg())", "Date.new(arg())", 0},
                 {"((/* V */ d)).new(arg())", "((/* V */ Date)).new(arg())", 0},
                 {"make().copy()", "Date.copy(make())", 0},
                 {"d.copy()", "Date.copy(d)", 0},
                 {"d |> Date.label()", "d |> label()", 1},
                 {"d |> copy()", "d |> Date.copy()", 0},
                 {"Date.label()", NULL, 1},
                 {"Date.label(1)", NULL, 1},
                 {"make().new(arg())", NULL, 0},
                 {"holder.d.new(arg())", NULL, 0},
                 {"d |> zero()", NULL, 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[4096], text[8192], expected[8192];
        snprintf(lf, sizeof(lf), "%s%s\n}\n", prefix, cases[i].call);
        size_t n = 0;
        for (size_t j = 0; lf[j]; j++) {
          if (crlf && lf[j] == '\n')
            text[n++] = '\r';
          text[n++] = lf[j];
        }
        text[n] = 0;
        Result r = check(&a, text);
        for (unsigned repeat = 0; repeat < 2; repeat++) {
          if (repeat) {
            diagnostics_init(&r.errors, &a);
            sema_check(r.program, &r.errors);
          }
          if (r.errors.count != 1)
            diagnostics_print(&r.errors, stderr);
          ASSERT_EQ_INT(1, r.errors.count);
          ASSERT_TRUE(!r.program->checked && ir_lower(&a, r.program) == NULL);
          Diagnostic *d = r.errors.items[0];
          ASSERT_EQ_INT(
              0, strcmp(cases[i].method
                            ? "Methods are called on a value, not the type."
                            : "Associated functions are called on the type, "
                              "not a value.",
                        d->message));
          ASSERT_TRUE(d->notes && !d->notes->next);
          if (cases[i].fixed) {
            snprintf(lf, sizeof(lf), "%s%s\n}\n", prefix, cases[i].fixed);
            n = 0;
            for (size_t j = 0; lf[j]; j++) {
              if (crlf && lf[j] == '\n')
                expected[n++] = '\r';
              expected[n++] = lf[j];
            }
            expected[n] = 0;
            ASSERT_TRUE(d->fixes != NULL);
            char *fixed = apply_call_fixes(&a, text, d);
            ASSERT_TRUE(fixed != NULL);
            if (fixed) {
              ASSERT_EQ_INT(0, strcmp(expected, fixed));
              valid(&a, fixed);
            }
          } else
            ASSERT_TRUE(d->fixes == NULL);
          ASSERT_EQ_INT(0, strcmp(text, d->span.file->text));
        }
      }
    }
    valid(&a, "P { n: int\nget(p: P) -> bool { true } }\nget(p: P) -> int { "
              "p.n }\nmain() -> int { P { n = 1 } |> get() }");
    invalid(&a,
            "P { n: int\nget(p: P) -> bool { true } }\nmain() { let get = 0\nP "
            "{ n = 1 } |> get() }",
            "A local binding is not callable");
  }
  TEST("wrong-way diagnostics retain SCC return anchors and fixes") {
    const char *text = "P { n: int\nrecur(n: int) { if n == 0 { ret self.n "
                       "}\nP.recur(self, n - 1) } }\nmain() {}";
    Result r = check(&a, text);
    ASSERT_EQ_INT(1, r.errors.count);
    ASSERT_EQ_INT(SEMA_INT, r.program->functions[0].symbol.return_type->kind);
    ASSERT_EQ_INT(0, strcmp("Methods are called on a value, not the type.",
                            r.errors.items[0]->message));
    char *fixed = apply_call_fixes(&a, text, r.errors.items[0]);
    ASSERT_TRUE(fixed != NULL);
    if (fixed)
      valid(&a, fixed);
    text = "P { n: int\ncopy(p: P) { p } }\nf(p: P, b: bool) { if b { ret p "
           "}\ng(p, b) }\ng(p: P, b: bool) { f(p, true).copy() }\nmain() {}";
    r = check(&a, text);
    ASSERT_EQ_INT(1, r.errors.count);
    ASSERT_TRUE(r.program->functions[1].symbol.return_type ==
                &r.program->structs[0].type);
    fixed = apply_call_fixes(&a, text, r.errors.items[0]);
    ASSERT_TRUE(fixed != NULL);
    if (fixed)
      valid(&a, fixed);
  }
  TEST("member ownership, concrete types, associated and receiver calls") {
    valid(&a, "A { x: int\nnew() -> A { A { x = 1 } }\n"
              "get() { self.x } }\nmain() { A.new().get() }");
    valid(&a, "A { unit: ()\nget() { self\n1 } }\n"
              "B { unit: ()\nget() { self\ntrue } }\nget() {}\n"
              "main() { let n: int = A { unit = () }.get()\n"
              "let b: bool = B { unit = () }.get()\nget() }");
    valid(&a, "A { unit: ()\nmain() {} }\nmain() {}");
    valid(&a, "A { unit: ()\nmake() -> A { if true { ret A { unit = () } }\n"
              "A.make() } }\nmain() { A.make() }");
    invalid(&a, "A { unit: ()\nf() {}\nf() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "A { unit: ()\nf() {}\nf() {} }\nmain() {}",
            "Duplicate function");
    invalid(&a, "main() {}\nf(a: Self) {}", "type is unknown");
    invalid(&a, "A { unit: () }\nmain() { A.nope() }", "Unknown associated");
    invalid(&a, "A { unit: () }\nf(a: A) {}\nmain() { A { unit = () }.f() }",
            "Unknown method");
    invalid(&a, "A { unit: ()\nf() {} }\nmain() { A { unit = () }.f() }",
            "Associated functions are called on the type");
    invalid(&a, "A { unit: ()\nf(a: int) {} }\nmain() { A { unit = () }.f() }",
            "Associated functions are called on the type");
    invalid(&a, "A { unit: ()\nf() { self } }\nmain() { A { unit = () }.f(1) }",
            "Wrong number");
    invalid(&a, "A { unit: ()\nf(a: A) {} }\n", "must define main");
    invalid(&a, "main() { \"x\".f() }", "Unknown method");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
