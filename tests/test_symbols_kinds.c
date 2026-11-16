#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("explicit tag kinds preserve identity, modules, IDs and first-use "
       "spans") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *left = source_create(&a, "left.brf", ":!found :found", 14);
    SourceFile *right = source_create(&a, "right.brf", ":found", 6);
    Diagnostics errors;
    diagnostics_init(&errors, &a);
    SemaTag *x = sema_declare_tag(p, left, "found", SEMA_TAG_ERROR,
                                  span_make(left, 0, 7), &errors);
    SemaTag *y = sema_declare_tag(p, right, "found", SEMA_TAG_NEUTRAL,
                                  span_make(right, 0, 6), &errors);
    ASSERT_EQ_INT(0, errors.count);
    ASSERT_TRUE(x && y && x != y);
    if (x && y) {
      ASSERT_EQ_INT(SEMA_TAG_ERROR, x->kind);
      ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, y->kind);
      ASSERT_EQ_INT(2, x->id);
      ASSERT_EQ_INT(3, y->id);
      ASSERT_TRUE(x->module == left && y->module == right);
      ASSERT_TRUE(x->kind_span.file == left);
      ASSERT_TRUE(sema_tag(p, left, "found") == x);
      ASSERT_TRUE(sema_declare_tag(p, left, "found", SEMA_TAG_NEUTRAL,
                                   span_make(left, 8, 14), &errors) == NULL);
      ASSERT_EQ_INT(1, errors.count);
      ASSERT_EQ_INT(SEMA_TAG_ERROR, x->kind);
      ASSERT_TRUE(errors.items[0]->notes &&
                  errors.items[0]->notes->span.file == left);
      ASSERT_TRUE(sema_declare_tag(p, left, "found", SEMA_TAG_NEUTRAL,
                                   span_make(left, 8, 14), &errors) == NULL);
      ASSERT_EQ_INT(1, errors.count); // One kind-conflict diagnostic per tag.
      ASSERT_EQ_INT(3, p->tag_count);
    }
  }
  TEST("a kind conflict reports at the later spelling in source") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *f = source_create(&a, "order.brf", ":clash :!clash", 14);
    Diagnostics errors;
    diagnostics_init(&errors, &a);
    // Declared out of source order, as an alias is resolved before a body.
    ASSERT_TRUE(sema_declare_tag(p, f, "clash", SEMA_TAG_ERROR,
                                 span_make(f, 7, 14), &errors) != NULL);
    ASSERT_TRUE(sema_declare_tag(p, f, "clash", SEMA_TAG_NEUTRAL,
                                 span_make(f, 0, 6), &errors) == NULL);
    ASSERT_EQ_INT(1, errors.count);
    if (errors.count) {
      ASSERT_EQ_INT(7, errors.items[0]->span.start);
      ASSERT_TRUE(errors.items[0]->notes &&
                  errors.items[0]->notes->span.start == 0);
    }
  }
  TEST("explicit kind does not reallocate an implicit tag or canonical set") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *module = source_create(&a, "implicit.brf", ":!implicit", 10);
    SemaTag *implicit = sema_tag(p, module, "implicit");
    SemaVariant variant = {implicit, NULL, 0};
    const SemaType *before = sema_symbol_set(p, &variant, 1);
    size_t id = implicit->id;
    ASSERT_TRUE(!implicit->kind_declared);
    Diagnostics errors;
    diagnostics_init(&errors, &a);
    ASSERT_TRUE(sema_declare_tag(p, module, "implicit", SEMA_TAG_ERROR,
                                 span_make(module, 0, 10),
                                 &errors) == implicit);
    ASSERT_EQ_INT(0, errors.count);
    ASSERT_EQ_INT(id, implicit->id);
    ASSERT_EQ_INT(SEMA_TAG_ERROR, implicit->kind);
    ASSERT_TRUE(sema_symbol_set(p, &variant, 1) == before);
    ASSERT_TRUE(sema_variant(before, implicit)->tag == implicit);
    ASSERT_TRUE(sema_ok(before) == NULL && sema_failures(p, before) == NULL);
  }
  TEST("error-kind identity survives checks and matched error-only calls") {
    Result r = valid(
        &a, "E = :!failed[str]\nf() -> E { :!failed[\"heap\" + \" payload\"] "
            "}\nmain() { f() |> { :!failed[text] => print(text) } }");
    SemaTag *failed = sema_tag(r.program, r.program->module, "failed");
    ASSERT_EQ_INT(SEMA_TAG_ERROR, failed->kind);
    ASSERT_TRUE(failed->kind_declared &&
                failed->kind_span.file == r.program->module);
    size_t id = failed->id;
    Diagnostics again;
    diagnostics_init(&again, &a);
    sema_check(r.program, &again);
    ASSERT_EQ_INT(0, again.count);
    ASSERT_EQ_INT(id, failed->id);
    ASSERT_EQ_INT(SEMA_TAG_ERROR, failed->kind);
    valid(&a, "main() { :!failure }"); // Valid source; N2 entry status is 1.
    invalid(&a, "A = :bad\nB = :!bad\nmain() {}", "both neutral and error");
    invalid(&a, "A = :!bad\nB = :bad\nmain() {}", "both neutral and error");
    invalid(&a, "main() { :!bad |> { :bad => () } }", "both neutral and error");
    invalid(&a, "main() { :!ok }", "neutral symbols and cannot");
    invalid(&a, "main() { :!none }", "neutral symbols and cannot");
  }
  TEST("ok and none are shared by every module without eager allocation") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *other = source_create(&a, "other.brf", "", 0);
    SemaTag *none = sema_tag(p, p->module, "none");
    ASSERT_EQ_INT(2, none->id);
    const SourceFile *modules[] = {p->module, other, NULL};
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
      ASSERT_TRUE(sema_tag(p, modules[i], "ok") == p->ok);
      ASSERT_TRUE(sema_tag(p, modules[i], "none") == none);
    }
    ASSERT_TRUE(p->ok->module == NULL && none->module == NULL);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, p->ok->kind);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, none->kind);
    ASSERT_EQ_INT(2, p->tag_count);
    ASSERT_EQ_INT(2, p->tag_names.count);
    ASSERT_TRUE(name_map_get(&p->tag_names, NULL, "ok") == p->ok);
    ASSERT_TRUE(name_map_get(&p->tag_names, NULL, "none") == none);
    ASSERT_TRUE(name_map_get(&p->tag_names, other, "ok") == NULL);
    ASSERT_TRUE(name_map_get(&p->tag_names, p->module, "none") == NULL);
    ASSERT_TRUE(sema_tag(p, p->module, "okay") != sema_tag(p, other, "okay"));
    ASSERT_TRUE(sema_tag(p, p->module, "noneThing") !=
                sema_tag(p, other, "noneThing"));
    // N2 reserves :none for absence, not a data-bearing constructor.
    invalid(&a, "main() { :none[7] }", "':none' cannot carry a payload.");
  }
  TEST(
      "none has zero semantic positions, including in canonical constructors") {
    Result r = valid(&a, "Empty = :none\nAgain = :none[]\n"
                         "Maybe = :some[int] | Empty\n"
                         "main() { let value: Maybe = :none[]\n"
                         "value |> { :none[] => ()\n:some[x] => () } }");
    SemaProgram *p = r.program;
    SemaTag *none = sema_tag(p, p->module, "none");
    const SemaType *empty = r.program->aliases[0].type;
    ASSERT_TRUE(empty == r.program->aliases[1].type);
    ASSERT_TRUE(empty == sema_symbol_type(p, none, NULL));
    ASSERT_EQ_INT(0, sema_variant(empty, none)->payload_count);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, none->kind);
    ASSERT_TRUE(!none->module);
    const SemaType *payloads[] = {&p->types[SEMA_UNIT], &p->types[SEMA_INT],
                                  &p->types[SEMA_STR]};
    SemaTag *other = sema_tag(p, p->module, "other");
    size_t sets = p->symbol_set_count;
    for (size_t count = 1; count <= 3; count++) {
      SemaVariant variants[] = {{other, NULL, 0}, {none, payloads, count}};
      SemaVariant saved[2];
      memcpy(saved, variants, sizeof(saved));
      ASSERT_TRUE(sema_symbol_set(p, variants, 2) == NULL);
      ASSERT_TRUE(sema_symbol_set(p, variants + 1, 1) == NULL);
      ASSERT_EQ_INT(sets, p->symbol_set_count);
      ASSERT_TRUE(!memcmp(saved, variants, sizeof(saved)));
    }
    for (size_t i = 0; i < 3; i++)
      ASSERT_TRUE(sema_symbol_type(p, none, payloads[i]) == NULL);
    ASSERT_EQ_INT(sets, p->symbol_set_count);
    // Only the shared reserved tag changes, not similarly named data tags.
    ASSERT_TRUE(sema_symbol_type(p, sema_tag(p, p->module, "noneThing"),
                                 &p->types[SEMA_UNIT]) != NULL);
  }
  TEST("none payload rejection is primary, exact and source preserving") {
    const char *cases[] = {
        "A = :none[int]\nmain() {}",
        "A = :none[()]\nmain() {}",
        "A = :none[int, str]\nmain() {}",
        "A = :none[Missing]\nmain() {}",
        "A = :none[A]\nmain() {}",
        "A = B\nB = :none[A]\nmain() {}",
        "A = :outer[:none[Missing]]\nmain() {}",
        "Holder { field: :none[int] }\nmain() {}",
        "f(x: :none[int]) {}\nmain() {}",
        "f() -> :none[int] {}\nmain() {}",
        "f() -> :ok[int] | :none[int] | _ { 1 }\nmain() {}",
        "main() { let value: :none[int] = :none }",
        "main() { :none[7] }",
        "main() { :none[()] }",
        "main() { :none[unknown, missing()] }",
        "main() { :outer[:none[missing()]] }",
        "main() { ret\n:none[missing()] }",
        "main() { :none |> { :none[x] => missing(x) } }",
        "main() { :none |> { :none[_] => missing() } }",
        "main() { :none |> { :none[()] => missing() } }",
        "main() { :none |> { :none[x, y] => missing(x, y) } }",
        "main() { :none |> { :none[x: Missing] => missing(x) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[512];
        size_t length = 0;
        for (const char *s = cases[i]; *s; s++) {
          if (crlf && *s == '\n')
            text[length++] = '\r';
          text[length++] = *s;
        }
        text[length] = 0;
        const char *at = strstr(text, ":none[");
        ASSERT_TRUE(at != NULL);
        if (!at)
          continue;
        for (unsigned repeat = 0; repeat < 2; repeat++) {
          Result r = check(&a, text);
          if (r.errors.count != 1)
            diagnostics_print(&r.errors, stderr);
          ASSERT_EQ_INT(1, r.errors.count);
          ASSERT_TRUE(!r.program->resolved && !r.program->checked);
          ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
          ASSERT_EQ_INT(length, r.program->module->length);
          ASSERT_TRUE(!memcmp(text, r.program->module->text, length));
          if (r.errors.count) {
            Diagnostic *d = r.errors.items[0];
            ASSERT_EQ_INT(
                0, strcmp("':none' cannot carry a payload.", d->message));
            ASSERT_EQ_INT((size_t)(at - text), d->span.start);
            ASSERT_EQ_INT(d->span.start + 5, d->span.end);
            ASSERT_TRUE(!d->fixes && !d->notes);
          }
        }
      }
  }
  TEST("none payload rules do not bypass unsupported enclosing scopes") {
    const char *sources[] = {
        "A = Db.:bad[:none[Missing]]\nmain() {}",
        "main() { Arr[int] { :none[missing()] } }",
        "main() { :other |> { :outer[x: :none[Missing]] => x } }",
        "main() { :!none }"};
    const char *messages[] = {
        "Module-qualified symbols are not implemented yet.",
        "Collection literals are not implemented yet.",
        "Typed symbol payload patterns are not implemented yet.",
        "':ok' and ':none' are neutral symbols and cannot use ':!'."};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
      Result r = invalid(&a, sources[i], messages[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved);
      if (r.errors.count)
        ASSERT_TRUE(!r.errors.items[0]->fixes);
    }
  }
  TEST("conversion failures conflict with neutral invalid and overflow tags") {
    const char *source =
        "Maybe = :ok[int] | :none\n"
        "parse(s: str) -> :ok[int] | :invalid[str] { int(s)? }\n"
        "small(n: lng) -> :ok[int] | :overflow { int(n)? }\n"
        "main() { :none }";
    Result r = check(&a, source);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, &a);
        sema_check(r.program, &r.errors);
      }
      ASSERT_EQ_INT(2, r.errors.count);
      ASSERT_TRUE(r.program->resolved && !r.program->checked);
      ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
      ASSERT_EQ_INT(SEMA_TAG_NEUTRAL,
                    sema_tag(r.program, r.program->module, "invalid")->kind);
      ASSERT_EQ_INT(SEMA_TAG_NEUTRAL,
                    sema_tag(r.program, r.program->module, "overflow")->kind);
      for (size_t i = 0; i < r.errors.count; i++) {
        Diagnostic *d = r.errors.items[i];
        int text = strstr(d->message, ":!invalid") != NULL;
        const char *call = strstr(source, text ? "int(s)" : "int(n)");
        const char *decl = strstr(source, text ? ":invalid" : ":overflow");
        ASSERT_EQ_INT(0, strcmp(text ? "Numeric conversions use ':!invalid', "
                                       "not neutral ':invalid'."
                                     : "Numeric conversions use ':!overflow', "
                                       "not neutral ':overflow'.",
                                d->message));
        ASSERT_EQ_INT((size_t)(call - source), d->span.start);
        ASSERT_EQ_INT(d->span.start + 6, d->span.end);
        ASSERT_TRUE(d->notes && !d->notes->next && !d->fixes);
        if (d->notes)
          ASSERT_EQ_INT((size_t)(decl - source), d->notes->span.start);
      }
    }
  }
  TEST("conversion kind conflicts survive inference, source order and "
       "rechecks") {
    struct {
      const char *text, *call, *decl;
    } cases[] = {
        {"Bad = :invalid[str]\nf(s: str) { int(s)? }\nmain() {}", "int(s)",
         ":invalid"},
        {"f(s: str) { int(s)? }\nBad = :invalid[str]\nmain() {}", "int(s)",
         ":invalid"},
        {"Bad = :overflow\nf(n: lng) { int(n)! }\nmain() {}", "int(n)",
         ":overflow"},
        {"f(n: lng) { int(n)! }\nBad = :overflow\nmain() {}", "int(n)",
         ":overflow"},
        {"Number = int\nBad = :invalid[str]\nf(s: str) { Number(s)! }\nmain() "
         "{}",
         "Number(s)", ":invalid"},
        {"Bad = :invalid[str]\nf(s: str) { ret 1\nint(s)? }\nmain() {}",
         "int(s)", ":invalid"},
        {"Bad = :overflow\nf(n: lng) { if n == 0 { ret 1 }\nint(g(n))? }\n"
         "g(n: lng) { if n == 0 { ret lng(1) }\nlng(f(n)) }\nmain() {}",
         "int(g(n))", ":overflow"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char source[1024];
        size_t length = 0;
        for (const char *s = cases[i].text; *s; s++) {
          if (crlf && *s == '\n')
            source[length++] = '\r';
          source[length++] = *s;
        }
        source[length] = 0;
        Result r = check(&a, source);
        size_t tags = r.program->tag_count;
        for (unsigned repeat = 0; repeat < 3; repeat++) {
          if (repeat) {
            diagnostics_init(&r.errors, &a);
            sema_check(r.program, &r.errors);
          }
          if (r.errors.count != 1)
            diagnostics_print(&r.errors, stderr);
          ASSERT_EQ_INT(1, r.errors.count);
          ASSERT_TRUE(r.program->resolved && !r.program->checked);
          ASSERT_TRUE(ir_lower(&a, r.program) == NULL);
          ASSERT_EQ_INT(tags, r.program->tag_count);
          ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
          if (r.errors.count) {
            Diagnostic *d = r.errors.items[0];
            ASSERT_TRUE(strstr(d->message, "Numeric conversions use ':!") !=
                        NULL);
            ASSERT_EQ_INT((size_t)(strstr(source, cases[i].call) - source),
                          d->span.start);
            ASSERT_EQ_INT(d->span.start + strlen(cases[i].call), d->span.end);
            ASSERT_TRUE(d->notes && !d->notes->next && !d->fixes);
            if (d->notes)
              ASSERT_EQ_INT((size_t)(strstr(source, cases[i].decl) - source),
                            d->notes->span.start);
          }
        }
      }
    Result r = invalid(&a,
                       "A = :invalid[str]\nB = :!invalid[str]\n"
                       "f(s: str) { int(s)? }\nmain() {}",
                       "both neutral and error");
    ASSERT_EQ_INT(1, r.errors.count);
    valid(&a, "Data = :invalid[str] | :overflow\nNumber = lng\n"
              "f(n: int) { Number(n) }\nmain() { int(7)\n:invalid[\"data\"] }");
  }
  TEST("intrinsic error kinds preserve module identities and payload schemas") {
    Result r =
        valid(&a, "Maybe = :ok[int] | :none\n"
                  "parse(s: str) -> :ok[int] | :!invalid[str] { int(s)? }\n"
                  "small(n: lng) -> :ok[int] | :!overflow { int(n)? }\n"
                  "main() { :none }");
    SemaProgram *p = r.program;
    SemaTag *invalid_tag = sema_tag(p, p->module, "invalid");
    SemaTag *overflow = sema_tag(p, p->module, "overflow");
    const SemaType *number = &p->types[SEMA_INT], *text = &p->types[SEMA_STR];
    const SemaType *parsed = sema_conversion(p, text, number);
    const SemaType *narrowed = sema_conversion(p, &p->types[SEMA_LNG], number);
    ASSERT_TRUE(sema_single_payload(sema_variant(parsed, invalid_tag)) == text);
    ASSERT_TRUE(sema_variant(narrowed, overflow)->payload_count == 0);
    ASSERT_TRUE(invalid_tag->module == p->module &&
                overflow->module == p->module);
    ASSERT_EQ_INT(SEMA_TAG_ERROR, invalid_tag->kind);
    ASSERT_EQ_INT(SEMA_TAG_ERROR, overflow->kind);
    // Extraction/handling still use :ok until their own N2 slice.
    ASSERT_TRUE(sema_failures(p, parsed) != NULL);
    ASSERT_TRUE(sema_failures(p, narrowed) != NULL);
    size_t count = p->tag_count;
    sema_check(p, &r.errors);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_EQ_INT(count, p->tag_count);
    ASSERT_TRUE(sema_conversion(p, text, number) == parsed);
    ASSERT_TRUE(sema_tag(p, p->module, "invalid") == invalid_tag);
    ASSERT_TRUE(sema_tag(p, p->module, "overflow") == overflow);
    // Inference creates conversion tags even without a written symbol set.
    valid(&a,
          "parse(s: str) { int(s)? }\nsmall(n: lng) { int(n)? }\nmain() {}");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
