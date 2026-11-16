#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("positional schemas own their arrays and retain zero versus unit") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SemaTag *tag = sema_tag(p, p->module, "record");
    const SemaType *items[] = {&p->types[SEMA_INT], &p->types[SEMA_STR],
                               &p->types[SEMA_UNIT]};
    SemaVariant v = {tag, items, 3};
    const SemaType *type = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(type != NULL);
    const SemaVariant *stored = sema_variant(type, tag);
    ASSERT_EQ_INT(3, stored->payload_count);
    ASSERT_TRUE(stored->payloads != items);
    size_t sets = p->symbol_set_count;
    const SemaType *same[] = {items[0], items[1], items[2]};
    SemaVariant duplicate[] = {{tag, same, 3}, v};
    ASSERT_TRUE(sema_symbol_set(p, duplicate, 2) == type);
    ASSERT_EQ_INT(sets, p->symbol_set_count);
    ASSERT_TRUE(duplicate[0].payloads == same &&
                duplicate[1].payloads == items);
    items[1] = &p->types[SEMA_BOOL];
    ASSERT_TRUE(stored->payloads[1] == &p->types[SEMA_STR]);
    ASSERT_TRUE(sema_symbol_set(p, &v, 1) != type);
    v.payload_count = 0;
    const SemaType *empty = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(sema_variant(empty, tag)->payloads == NULL);
    ASSERT_EQ_INT(0, sema_variant(empty, tag)->payload_count);
    ASSERT_TRUE(empty == sema_symbol_type(p, tag, NULL));
    const SemaType *unit = sema_symbol_type(p, tag, &p->types[SEMA_UNIT]);
    ASSERT_TRUE(empty != unit);
    ASSERT_TRUE(!sema_assignable(empty, unit) && !sema_assignable(unit, empty));
    ASSERT_TRUE(sema_equatable(empty) && sema_equatable(unit));
    sets = p->symbol_set_count;
    SemaVariant missing = {tag, NULL, 1};
    ASSERT_TRUE(sema_symbol_set(p, &missing, 1) == NULL);
    const SemaType *null_item[] = {&p->types[SEMA_STR], NULL};
    missing.payloads = null_item;
    missing.payload_count = 2;
    ASSERT_TRUE(sema_symbol_set(p, &missing, 1) == NULL);
    ASSERT_EQ_INT(sets, p->symbol_set_count);
    const SemaType *number = &p->types[SEMA_INT];
    SemaVariant ok = {p->ok, &number, 1};
    ASSERT_TRUE(sema_symbol_set(p, &ok, 1) == number);
    ok.payloads = same;
    ok.payload_count = 3;
    ASSERT_EQ_INT(3, sema_ok(sema_symbol_set(p, &ok, 1))->payload_count);
  }
  TEST("all positional schemas agree with an independent base-three oracle") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SemaTag *tag = sema_tag(p, p->module, "row");
    const SemaType *choices[] = {&p->types[SEMA_INT], &p->types[SEMA_STR],
                                 &p->types[SEMA_UNIT]};
    enum {
      COUNT = 121
    }; // All lengths 0..4 over three distinct canonical types.
    const SemaType *schemas[COUNT];
    size_t lengths[COUNT], codes[COUNT], n = 0, limit = 1;
    for (size_t length = 0; length <= 4; length++, limit *= 3) {
      for (size_t code = 0; code < limit; code++) {
        const SemaType *items[4];
        size_t digits = code;
        for (size_t j = 0; j < length; j++) {
          items[j] = choices[digits % 3];
          digits /= 3;
        }
        SemaVariant v = {tag, items, length};
        const SemaType *type = sema_symbol_set(p, &v, 1);
        ASSERT_TRUE(type != NULL);
        lengths[n] = length;
        codes[n] = code;
        schemas[n++] = type;
        SemaVariant dup[] = {v, v};
        ASSERT_TRUE(sema_symbol_set(p, dup, 2) == type);
      }
    }
    ASSERT_EQ_INT(COUNT, n);
    ASSERT_TRUE(p->set_table_capacity >= 256);
    for (size_t i = 0; i < n; i++) {
      const SemaVariant *v = sema_variant(schemas[i], tag);
      size_t digits = codes[i];
      ASSERT_EQ_INT(lengths[i], v->payload_count);
      for (size_t k = 0; k < lengths[i]; k++) {
        ASSERT_TRUE(v->payloads[k] == choices[digits % 3]);
        digits /= 3;
      }
      for (size_t j = 0; j < n; j++) {
        int equal = lengths[i] == lengths[j] && codes[i] == codes[j];
        ASSERT_EQ_INT(equal, schemas[i] == schemas[j]);
        ASSERT_EQ_INT(equal, sema_assignable(schemas[i], schemas[j]));
        ASSERT_TRUE(sema_symbol_union(p, schemas[i], schemas[j]) ==
                    (equal ? schemas[i] : NULL));
      }
    }
  }
  TEST("wide positional schemas preserve the final item across scratch reuse") {
    SemaProgram *p = valid(&a, "main() {}").program;
    enum { WIDTH = 512 };
    const SemaType **items = arena_calloc(&a, WIDTH, sizeof(*items));
    for (size_t i = 0; i < WIDTH; i++)
      items[i] = &p->types[SEMA_INT];
    items[WIDTH - 1] = &p->types[SEMA_STR];
    SemaTag *tag = sema_tag(p, p->module, "wide");
    SemaVariant v = {tag, items, WIDTH};
    const SemaType *original = sema_symbol_set(p, &v, 1);
    items[WIDTH - 1] = &p->types[SEMA_BOOL];
    const SemaType *different = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(original != different);
    ASSERT_TRUE(!sema_assignable(original, different));
    ASSERT_TRUE(sema_symbol_union(p, original, different) == NULL);
    items[WIDTH - 1] = &p->types[SEMA_STR];
    SemaVariant copies[] = {v, v, v};
    ASSERT_TRUE(sema_symbol_set(p, copies, 3) == original);
    ASSERT_EQ_INT(WIDTH, sema_variant(original, tag)->payload_count);
    ASSERT_TRUE(sema_variant(original, tag)->payloads[WIDTH - 1] ==
                &p->types[SEMA_STR]);
    ASSERT_TRUE(sema_equatable(original));
  }
  TEST("positional unions and named coverage compare every payload position") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SemaTag *x = sema_tag(p, p->module, "pair"),
            *y = sema_tag(p, p->module, "other"),
            *z = sema_tag(p, p->module, "disjoint");
    const SemaType *pair[] = {&p->types[SEMA_INT], &p->types[SEMA_STR]};
    const SemaType *other[] = {&p->types[SEMA_UNIT], &p->types[SEMA_BOOL]};
    const SemaType *bad[] = {&p->types[SEMA_INT], &p->types[SEMA_BOOL]};
    SemaVariant input[] = {{x, pair, 2}, {y, other, 2}},
                reversed[] = {input[1], input[0], input[1]};
    const SemaType *all = sema_symbol_set(p, input, 2),
                   *one = sema_symbol_set(p, input, 1);
    const SemaType *two = sema_symbol_set(p, input + 1, 1);
    ASSERT_TRUE(sema_symbol_set(p, reversed, 3) == all);
    ASSERT_TRUE(sema_symbol_union(p, one, two) == all &&
                sema_symbol_union(p, two, one) == all);
    ASSERT_TRUE(sema_assignable(one, all) && !sema_assignable(all, one));
    ASSERT_TRUE(reversed[0].tag == y && reversed[1].tag == x);
    SemaVariant conflict[] = {{x, pair, 2}, {x, bad, 2}};
    ASSERT_TRUE(sema_symbol_set(p, conflict, 2) == NULL);
    const SemaType *wrong = sema_symbol_set(p, conflict + 1, 1);
    ASSERT_TRUE(sema_symbol_union(p, all, wrong) == NULL);
    SemaVariant group[] = {{x, pair, 2}, {z, bad, 2}};
    SemaVar remaining = {0};
    SemaArm arms[] = {{.kind = PAT_SET, .set = sema_symbol_set(p, group, 2)},
                      {.kind = PAT_BIND, .binding = &remaining}};
    SemaExpr value = {.type = all},
             match = {.kind = SEMA_EXPR_MATCH,
                      .as.match = {.input = &value, .arms = arms, .count = 2}};
    Diagnostics errors;
    diagnostics_init(&errors, &a);
    sema_match_coverage(p, &match, &errors);
    ASSERT_EQ_INT(0, errors.count);
    ASSERT_TRUE(arms[0].cases == one && arms[1].cases == two &&
                remaining.type == two);
    group[0].payloads = bad;
    arms[0].set = sema_symbol_set(p, group, 2);
    diagnostics_init(&errors, &a);
    sema_match_coverage(p, &match, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(0, strcmp("Symbol tag has conflicting payload types.",
                            errors.items[0]->message));
    ASSERT_TRUE(arms[0].cases == NULL && remaining.type == all);
    arms[0] = (SemaArm){.kind = PAT_SYMBOL, .tag = x, .payload_count = 1};
    diagnostics_init(&errors, &a);
    sema_match_coverage(p, &match, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(0, strcmp("Symbol pattern has the wrong payload shape.",
                            errors.items[0]->message));
    SemaVar first = {0}, second = {0};
    SemaVar *bindings[] = {&first, &second};
    arms[0].payload_count = 2;
    arms[0].payload_bindings = bindings;
    diagnostics_init(&errors, &a);
    sema_match_coverage(p, &match, &errors);
    ASSERT_EQ_INT(0, errors.count);
    ASSERT_TRUE(first.type == pair[0] && second.type == pair[1]);
    ASSERT_TRUE(arms[0].binding == NULL && arms[0].cases == one);
    ASSERT_TRUE(remaining.type == two);
    arms[0].payload_bindings = NULL; // Both positions are wildcards.
    diagnostics_init(&errors, &a);
    sema_match_coverage(p, &match, &errors);
    ASSERT_EQ_INT(0, errors.count);
  }
  TEST("equatability follows late positions and struct-indirected cycles") {
    SemaProgram *p =
        valid(&a, "Node { next: :end | :more[Node] }\nmain() {}").program;
    SemaTag *tag = sema_tag(p, p->module, "tuple");
    const SemaType *items[] = {&p->types[SEMA_INT], &p->structs[0].type};
    SemaVariant v = {tag, items, 2};
    const SemaType *type = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(!sema_equatable(type));
    items[1] = type;
    const SemaType *nested = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(!sema_equatable(nested));
    items[1] = &p->types[SEMA_UNKNOWN];
    const SemaType *unknown = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(!sema_equatable(unknown));
    p->structs[0].fields[0].type = &p->types[SEMA_INT];
    sema_update_equatable(p);
    ASSERT_TRUE(sema_equatable(type) && sema_equatable(nested));
    ASSERT_TRUE(!sema_equatable(unknown));
  }
  TEST("permissions follow owned objects in late and nested positions") {
    Result r =
        invalid(&a,
                "P { n: int }\nread(value: :item[P]) -> P { P { n = 0 } "
                "}\nuse(value: :item[P]) { read(value).n = 1 }\nmain() {}",
                "Cannot mutate through an ordinary parameter");
    SemaProgram *p = r.program;
    SemaFunction *use = &p->functions[1];
    SemaExpr *assignment = use->body->as.block.items[0];
    ASSERT_EQ_INT(SEMA_EXPR_ASSIGN, assignment->kind);
    SemaExpr *call = assignment->as.binding.target->as.field.object;
    ASSERT_EQ_INT(SEMA_EXPR_CALL, call->kind);
    const SemaType *items[] = {&p->types[SEMA_STR], &p->structs[0].type};
    SemaVariant v = {sema_tag(p, p->module, "item"), items, 2};
    for (unsigned nested = 0; nested < 2; nested++) {
      const SemaType *type = sema_symbol_set(p, &v, 1);
      for (size_t i = 0; i < 2; i++)
        p->functions[i].symbol.params[0]->type =
            p->functions[i].symbol.params[0]->annotation = type;
      call->as.call.args[0]->type = call->as.call.args[0]->raw_type = type;
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      sema_check_permissions(p, &errors);
      ASSERT_EQ_INT(1, errors.count);
      ASSERT_EQ_INT(
          0, strcmp("Cannot mutate through an ordinary parameter; use &.",
                    errors.items[0]->message));
      items[1] = type;
    }
  }
  TEST("native lowering and emission retain unused positional type metadata") {
    SemaProgram *p = valid(&a, "main() {}").program;
    IrProgram *ir = ir_lower(&a, p);
    ASSERT_TRUE(ir != NULL);
    ir_insert_rc(&a, ir);
    const SemaType *items[] = {&p->types[SEMA_STR], &p->types[SEMA_INT]};
    SemaVariant v = {sema_tag(p, p->module, "pending"), items, 2};
    const SemaType *type = sema_symbol_set(p, &v, 1);
    ASSERT_TRUE(ir_lower(&a, p) != NULL);
    const SemaSymSet *sets[] = {type->symbols};
    ir->symbol_sets = sets;
    ir->symbol_set_count = 1;
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(emit_c(out, ir));
      ASSERT_TRUE(ftell(out) > 0);
      fclose(out);
    }
  }
  TEST("semantic payload positions contextualize literals and nested unions") {
    SemaProgram *p =
        valid(&a, "Pair = :row[int]\nf() -> Pair { let small = 1\nlet decimal "
                  "= 0.1\nlet inner = :first\n:row[small] }\nmain() {}")
            .program;
    SemaFunction *f = &p->functions[0];
    SemaExpr **body = f->body->as.block.items;
    SemaExpr *items[] = {copy_expr(&a, body[0]->as.binding.value),
                         copy_expr(&a, body[1]->as.binding.value),
                         copy_expr(&a, body[2]->as.binding.value)};
    SemaExpr *row = body[3];
    row->as.symbol.payloads = items;
    row->as.symbol.count = 3;
    const SemaType *first = sema_symbol_type(p, items[2]->as.symbol.tag, NULL);
    const SemaType *second =
        sema_symbol_type(p, sema_tag(p, p->module, "second"), NULL);
    const SemaType *nested = sema_symbol_union(p, first, second);
    const SemaType *types[] = {&p->types[SEMA_SHRT], &p->types[SEMA_FLT],
                               nested};
    SemaVariant variant = {row->as.symbol.tag, types, 3};
    f->written_type = sema_symbol_set(p, &variant, 1);
    for (unsigned pass = 0; pass < 3; pass++) {
      Diagnostics errors;
      recheck_program(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      ASSERT_TRUE(lower(&a, p) != NULL);
      ASSERT_TRUE(row->type == f->written_type);
      ASSERT_TRUE(items[0]->type == types[0] && items[1]->type == types[1]);
      ASSERT_TRUE(items[1]->real == (double)(float)0.1);
      ASSERT_TRUE(sema_variant(row->type, variant.tag)->payloads[2] == nested);
    }
    types[1] = &p->types[SEMA_BOOL];
    f->written_type = sema_symbol_set(p, &variant, 1);
    Diagnostics errors;
    recheck_program(&a, p, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(
        0, strcmp("Function body does not match the written return type.",
                  errors.items[0]->message));
    ASSERT_TRUE(!p->checked && ir_lower(&a, p) == NULL);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
