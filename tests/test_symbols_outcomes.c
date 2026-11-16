#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("error-kind classification is shallow and agrees with subset masks") {
    ASSERT_TRUE(!sema_has_errors(NULL));
    ASSERT_TRUE(!sema_only_errors(NULL));
    SemaSymSet empty = {0};
    SemaType empty_type = {.kind = SEMA_SYM, .symbols = &empty};
    ASSERT_TRUE(!sema_only_errors(&empty_type));
    ASSERT_TRUE(!sema_single_success(NULL));
    ASSERT_TRUE(!sema_single_success(&empty_type));
    for (unsigned kinds = 0; kinds < 8; kinds++) {
      SemaProgram *p = valid(&a, "main() {}").program;
      for (size_t i = 0; i < SEMA_SYM; i++) {
        ASSERT_TRUE(!sema_has_errors(&p->types[i]));
        ASSERT_TRUE(!sema_only_errors(&p->types[i]));
        ASSERT_TRUE(!sema_single_success(&p->types[i]));
        ASSERT_TRUE(!sema_kind_part(p, &p->types[i], SEMA_TAG_ERROR));
        ASSERT_TRUE(!sema_kind_part(p, &p->types[i], SEMA_TAG_NEUTRAL));
      }
      ASSERT_TRUE(!sema_kind_part(p, NULL, SEMA_TAG_ERROR));
      ASSERT_TRUE(!sema_kind_part(p, &empty_type, SEMA_TAG_NEUTRAL));
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      SemaTag *leaf = sema_declare_tag(p, p->module, "leaf", SEMA_TAG_ERROR,
                                       (Span){0}, &errors);
      const SemaType *nested = sema_symbol_type(p, leaf, &p->types[SEMA_STR]);
      ASSERT_TRUE(sema_has_errors(nested));
      const char *names[] = {"healthy", "bad", "invalid"};
      SemaTag *tags[5] = {p->ok, sema_tag(p, p->module, "none")};
      for (size_t i = 0; i < 3; i++)
        tags[i + 2] = sema_declare_tag(p, p->module, names[i],
                                       kinds & (1u << i) ? SEMA_TAG_ERROR
                                                         : SEMA_TAG_NEUTRAL,
                                       (Span){0}, &errors);
      ASSERT_EQ_INT(0, errors.count);
      const SemaType *pair[] = {&p->types[SEMA_UNIT], &p->types[SEMA_INT]};
      for (unsigned mask = 1; mask < 32; mask++) {
        SemaVariant variants[5];
        size_t count = 0;
        for (size_t i = 0; i < 5; i++)
          if (mask & (1u << i)) {
            variants[count++] = (SemaVariant){tags[i],
                                              i == 3   ? pair
                                              : i == 4 ? &nested
                                                       : NULL,
                                              i == 3   ? 2
                                              : i == 4 ? 1
                                                       : 0};
          }
        const SemaType *type = sema_symbol_set(p, variants, count);
        if ((mask & 3) == 3) {
          ASSERT_TRUE(type == NULL);
          continue;
        }
        ASSERT_TRUE(type != NULL);
        if (mask == 1) {
          ASSERT_TRUE(type == &p->types[SEMA_UNIT]);
          ASSERT_TRUE(!sema_has_errors(type) && !sema_single_success(type));
          ASSERT_TRUE(!sema_kind_part(p, type, SEMA_TAG_NEUTRAL));
          continue;
        }
        SemaVariant saved[5], canonical[5];
        memcpy(saved, variants, count * sizeof(*variants));
        memcpy(canonical, type->symbols->variants, count * sizeof(*canonical));
        const SemaType *expected[2];
        const SemaVariant *single = NULL;
        unsigned neutral = mask & ~(kinds << 2), neutral_count = 0;
        for (size_t i = 0; i < 5; i++)
          if (neutral & (1u << i)) {
            single = sema_variant(type, tags[i]);
            neutral_count++;
          }
        if (neutral_count != 1)
          single = NULL;
        for (unsigned kind = 0; kind < 2; kind++) {
          SemaVariant group[5];
          size_t n = 0;
          unsigned selected = kind ? mask & (kinds << 2) : neutral;
          for (size_t i = 0; i < 5; i++)
            if (selected & (1u << i))
              group[n++] = *sema_variant(type, tags[i]);
          expected[kind] = n ? sema_symbol_set(p, group, n) : NULL;
        }
        size_t sets = p->symbol_set_count, tag_count = p->tag_count;
        for (unsigned repeat = 0; repeat < 3; repeat++) {
          ASSERT_TRUE(single == sema_single_success(type));
          ASSERT_TRUE(expected[0] == sema_kind_part(p, type, SEMA_TAG_NEUTRAL));
          ASSERT_TRUE(expected[1] == sema_kind_part(p, type, SEMA_TAG_ERROR));
          ASSERT_TRUE(!memcmp(saved, variants, count * sizeof(*variants)));
          ASSERT_TRUE(!memcmp(canonical, type->symbols->variants,
                              count * sizeof(*canonical)));
          ASSERT_EQ_INT((mask & (kinds << 2)) != 0, sema_has_errors(type));
          ASSERT_EQ_INT(!(mask & 3) && !((mask >> 2) & ~kinds),
                        sema_only_errors(type));
          ASSERT_EQ_INT(sets, p->symbol_set_count);
          ASSERT_EQ_INT(tag_count, p->tag_count);
        }
      }
    }
    Result r = valid(&a, "Error = :!problem[str]\n"
                         "Data = :box[Error]\nBox { value: Error }\nmain() {}");
    ASSERT_TRUE(sema_has_errors(r.program->aliases[0].type));
    ASSERT_TRUE(!sema_has_errors(r.program->aliases[1].type));
    ASSERT_TRUE(!sema_has_errors(&r.program->structs[0].type));
    ASSERT_TRUE(
        sema_has_errors(sema_success(r.program, r.program->aliases[0].type)));
  }
  TEST("mixed outcomes remove only error tags and preserve success schemas") {
    struct {
      const char *schema, *value;
      SemaTypeKind kind;
      size_t successes, failures;
    } cases[] = {
        {":created[int] | :!failed[str]", ":created[7]", SEMA_INT, 1, 1},
        {":!early | :created[str] | :!late[str, ()]", ":created[\"owned\"]",
         SEMA_STR, 1, 2},
        {":created | :!failed", ":created", SEMA_UNIT, 1, 1},
        {":created[()] | :!failed", ":created[()]", SEMA_UNIT, 1, 1},
        {":none | :!failed", ":none", SEMA_UNIT, 1, 1},
        {":created[str] | :updated[str] | :!failed[str]", ":created[\"owned\"]",
         SEMA_SYM, 2, 1},
        {":ok[str] | :none | :!failed[str]", ":none", SEMA_SYM, 2, 1},
        {":created[str, (), :note[str]] | :updated[()] | :!failed[str, ()]",
         ":created[\"owned\", (), :note[\"nested\"]]", SEMA_SYM, 2, 1},
        {":created[Box] | :!failed", ":created[Box { value = 7 }]", SEMA_STRUCT,
         1, 1},
        {":created[:!inner[str]] | :!outer", ":created[:!inner[\"nested\"]]",
         SEMA_SYM, 1, 1}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
      for (unsigned panic = 0; panic < 2; panic++)
        for (unsigned crlf = 0; crlf < 2; crlf++) {
          char lf[2048], text[4096];
          snprintf(lf, sizeof(lf),
                   "Box { value: int }\nOutcome = %s\n"
                   "f() -> Outcome { %s }\nmain() { let value = f()%c }",
                   cases[i].schema, cases[i].value, panic ? '!' : '?');
          size_t length = 0;
          for (const char *s = lf; *s; s++) {
            if (crlf && *s == '\n')
              text[length++] = '\r';
            text[length++] = *s;
          }
          text[length] = 0;
          if (i + 1 == sizeof(cases) / sizeof(*cases)) {
            Result old = invalid(
                &a, text,
                "Outcome binding is not handled before leaving its scope.");
            ASSERT_EQ_INT(1, old.errors.count);
            ASSERT_TRUE(old.errors.items[0]->notes != NULL);
            // Extracting a nested error creates a fresh top-level obligation.
            // Keep the original negative input and handle the positive copy.
            size_t end = strlen(lf) - 1;
            snprintf(lf + end, sizeof(lf) - end,
                     "\nvalue |> { :!inner[s] => print(s) } }");
            length = 0;
            for (const char *s = lf; *s; s++) {
              if (crlf && *s == '\n')
                text[length++] = '\r';
              text[length++] = *s;
            }
            text[length] = 0;
          }
          Result r = valid(&a, text);
          for (unsigned repeat = 0; repeat < 3; repeat++) {
            if (repeat) {
              diagnostics_init(&r.errors, &a);
              sema_check(r.program, &r.errors);
            }
            ASSERT_EQ_INT(0, r.errors.count);
            ASSERT_TRUE(r.program->checked);
            ASSERT_TRUE(!memcmp(text, r.program->module->text, length));
            const SemaFunction *main = &r.program->functions[1];
            const SemaExpr *postfix =
                main->body->as.block.items[0]->as.binding.value;
            const SemaType *input = postfix->as.value->type;
            ASSERT_EQ_INT(cases[i].kind, postfix->type->kind);
            ASSERT_EQ_INT(cases[i].failures, postfix->failures->symbols->count);
            ASSERT_TRUE(sema_only_errors(postfix->failures));
            ASSERT_TRUE(sema_assignable(postfix->failures, input));
            ASSERT_EQ_INT(panic ? SEMA_NEXT : SEMA_NEXT | SEMA_RETURN,
                          postfix->flow);
            if (cases[i].successes > 1) {
              ASSERT_TRUE(postfix->type ==
                          sema_kind_part(r.program, input, SEMA_TAG_NEUTRAL));
              ASSERT_EQ_INT(cases[i].successes, postfix->type->symbols->count);
              ASSERT_TRUE(!sema_has_errors(postfix->type));
              ASSERT_TRUE(sema_assignable(postfix->type, input));
              ASSERT_TRUE(postfix->type != input);
            }
            IrProgram *ir = ir_lower(&a, r.program);
            ASSERT_TRUE(ir != NULL);
            if (ir) {
              size_t casts = 0, projections = 0;
              const SemaVariant *success = sema_single_success(input);
              for (size_t j = 0; j < ir->functions[1].count; j++) {
                const IrInstruction *in = &ir->functions[1].instructions[j];
                casts += in->op == IR_SYMBOL_CAST && in->type == postfix->type;
                projections += in->op == IR_SYMBOL_PAYLOAD && success &&
                               in->tag == success->tag;
              }
              if (cases[i].successes > 1)
                ASSERT_EQ_INT(1, casts);
              else
                ASSERT_EQ_INT(cases[i].kind != SEMA_UNIT, projections);
              ir_insert_rc(&a, ir);
              FILE *out = tmpfile();
              ASSERT_TRUE(out != NULL);
              if (out) {
                ASSERT_TRUE(emit_c(out, ir));
                fclose(out);
              }
            }
          }
        }
    valid(&a,
          "Box { value: int\nread() -> int { self.value } }\n"
          "f() -> :created[Box] | :!failed { :created[Box { value = 7 }] }\n"
          "main() { print(str(f()!.read())) }");
    valid(&a, "f() -> :ok[str] | :none | :!failed { :none }\n"
              "g() -> :ok[str] | :none | :!failed { f()? }\nmain() { g()? }");
    Result bad =
        invalid(&a,
                "f() -> :created[int, str] | :!failed { :created[7, \"x\"] }\n"
                "main() { f()? }",
                "Extracting several success payloads is not implemented yet.");
    ASSERT_EQ_INT(1, bad.errors.count);
    bad =
        invalid(&a,
                "f() -> :created[(), ()] | :!failed { :created[(), ()] }\n"
                "main() { f()! }",
                "Extracting several success payloads is not implemented yet.");
    ASSERT_EQ_INT(1, bad.errors.count);
    bad = invalid(&a,
                  "f() -> :ok[str] | :none | :!failed { :none }\n"
                  "g() -> :ok[str] | :none { f()? }\nmain() {}",
                  "Propagated variants are not in the written return type.");
    ASSERT_EQ_INT(1, bad.errors.count);
  }
  TEST("mixed success extraction preserves readonly payload permissions") {
    const char *prefix =
        "Box { value: int\nbump(&self) { self.value += 1 } }\n";
    const char *schemas[] = {":created[Box] | :!failed",
                             ":created[Box] | :updated[Box] | :!failed"};
    const char *bodies[] = {"value!.bump()",
                            "value! |> { :created[box] => box.bump()\n"
                            ":updated[box] => box.bump() }"};
    for (size_t i = 0; i < 2; i++) {
      char text[2048];
      snprintf(text, sizeof(text),
               "%sOutcome = %s\nchange(value: Outcome) { %s }\nmain() {}",
               prefix, schemas[i], bodies[i]);
      Result r = invalid(
          &a, text, "A read-only value cannot be passed to an & parameter");
      ASSERT_EQ_INT(i + 1, r.errors.count);
      snprintf(text, sizeof(text),
               "%sOutcome = %s\nchange(value: &Outcome) { %s }\nmain() {}",
               prefix, schemas[i], bodies[i]);
      valid(&a, text);
    }
  }
  arena_destroy(&a);
  RUN_TESTS();
}
