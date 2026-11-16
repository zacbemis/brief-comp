#include "symbols_support.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("positional arm bindings have independent types, wildcards and slots") {
    SemaProgram *p = valid(&a, "f(value: :row[int]) { value |> { :row[first] "
                               "=> first } }\nmain() {}")
                         .program;
    SemaFunction *f = &p->functions[0];
    SemaExpr *pipe = f->body->as.block.items[0];
    SemaExpr *match = pipe->as.block.items[1];
    SemaArm *arm = &match->as.match.arms[0];
    ASSERT_TRUE(arm->binding == NULL && arm->payload_count == 1);
    SemaVar *first = arm->payload_bindings[0];
    ASSERT_TRUE(!first->mutable);
    SemaVar *last = extra_binding(&a, p, f, "last", first->span);
    SemaVar *bindings[] = {first, NULL, last};
    arm->payload_count = 3;
    arm->payload_bindings = bindings;
    SemaExpr *first_read = arm->body;
    SemaExpr *last_read = copy_expr(&a, first_read);
    last_read->as.variable = last;
    arm->body = last_read;
    const SemaType *types[] = {&p->types[SEMA_INT], &p->types[SEMA_UNIT],
                               &p->types[SEMA_STR]};
    SemaVariant v = {arm->tag, types, 3};
    const SemaType *input = sema_symbol_set(p, &v, 1);
    f->symbol.params[0]->type = f->symbol.params[0]->annotation = input;
    for (unsigned pass = 0; pass < 3; pass++) {
      Diagnostics errors;
      recheck(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      ASSERT_TRUE(first->type == types[0] && last->type == types[2]);
      ASSERT_TRUE(first->id != last->id && !last->mutable);
      ASSERT_TRUE(f->symbol.return_type == types[2]);
      ASSERT_TRUE(owned_ir(&a, p) != NULL);
    }
    // A malformed arm must clear every binder, not retain probe-time types.
    arm->payload_count = 2;
    arm->payload_bindings = (SemaVar *[]){first, last};
    arm->body = arena_calloc(&a, 1, sizeof(*arm->body));
    arm->body->kind = SEMA_EXPR_UNIT;
    f->written = 1;
    f->written_type = &p->types[SEMA_UNIT];
    Diagnostics errors;
    recheck(&a, p, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(0, strcmp("Symbol pattern has the wrong payload shape.",
                            errors.items[0]->message));
    ASSERT_TRUE(first->type == &p->types[SEMA_ERROR] &&
                last->type == &p->types[SEMA_ERROR]);
  }
  TEST("positional packing never erases readonly access from an earlier item") {
    Result r = invalid(&a,
                       "P { n: int }\nf(p: P) { let packed = :row[p]\npacked "
                       "|> { :row[item] => item.n = 1 } }\nmain() {}",
                       "Cannot mutate through an ordinary parameter");
    SemaProgram *p = r.program;
    SemaFunction *f = &p->functions[0];
    SemaExpr *pack = f->body->as.block.items[0]->as.binding.value;
    SemaExpr *match = f->body->as.block.items[1]->as.block.items[1];
    SemaArm *arm = &match->as.match.arms[0];
    SemaVar *item = arm->payload_bindings[0];
    SemaExpr *object = pack->as.symbol.payloads[0];
    SemaExpr unit = {.kind = SEMA_EXPR_UNIT};
    SemaExpr *items[] = {object, &unit};
    SemaVar *bindings[] = {item, NULL};
    pack->as.symbol.payloads = items;
    pack->as.symbol.count = 2;
    arm->payload_bindings = bindings;
    arm->payload_count = 2;
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      for (unsigned pass = 0; pass < 2; pass++) {
        Diagnostics errors;
        recheck(&a, p, &errors);
        ASSERT_EQ_INT(1, errors.count);
        ASSERT_EQ_INT(
            0, strcmp("Cannot mutate through an ordinary parameter; use &.",
                      errors.items[0]->message));
        ASSERT_TRUE(item->type == &p->structs[0].type);
        ASSERT_TRUE(!p->checked && ir_lower(&a, p) == NULL);
      }
      items[0] = &unit;
      items[1] = object;
      bindings[0] = NULL;
      bindings[1] = item;
    }
  }
  TEST("later payload returns anchor inference, dead returns do not") {
    SemaProgram *p = valid(&a, "f() { :row[if true { ret 7 } else { ret 8 }] "
                               "}\ng() { ret \"dead\" }\nmain() {}")
                         .program;
    SemaFunction *f = &p->functions[0];
    SemaExpr *row = f->body->as.block.items[0];
    SemaExpr *returning = row->as.symbol.payloads[0];
    SemaExpr unit = {.kind = SEMA_EXPR_UNIT};
    SemaExpr *dead = copy_expr(&a, p->functions[1].body->as.block.items[0]);
    SemaExpr *items[] = {&unit, returning, dead};
    row->as.symbol.payloads = items;
    row->as.symbol.count = 3;
    for (unsigned pass = 0; pass < 3; pass++) {
      Diagnostics errors;
      recheck(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      ASSERT_TRUE(f->symbol.return_type == &p->types[SEMA_INT]);
      ASSERT_TRUE(returning->reachable && !dead->reachable);
      ASSERT_TRUE(!(row->flow & SEMA_NEXT) && (row->flow & SEMA_RETURN));
      // No positional type exists here: the value never finishes packing.
      for (const SemaSymSet *s = p->symbol_sets; s; s = s->next)
        for (size_t i = 0; i < s->count; i++)
          ASSERT_TRUE(s->variants[i].payload_count <= 1);
      ASSERT_TRUE(owned_ir(&a, p) != NULL);
    }
  }
  TEST("positional values survive recursive return probes without losing "
       "slots") {
    SemaProgram *p = valid(&a, "f(n: int) { if n == 0 { ret :row[1] }\ng(n - "
                               "1) }\ng(n: int) { f(n) }\nmain() {}")
                         .program;
    SemaExpr *conditional = p->functions[0].body->as.block.items[0];
    SemaExpr *row =
        conditional->as.conditional.yes->as.block.items[0]->as.value;
    SemaExpr *items[] = {row->as.symbol.payloads[0],
                         copy_expr(&a, row->as.symbol.payloads[0])};
    row->as.symbol.payloads = items;
    row->as.symbol.count = 2;
    for (unsigned pass = 0; pass < 3; pass++) {
      Diagnostics errors;
      recheck(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      const SemaType *type = p->functions[0].symbol.return_type;
      ASSERT_TRUE(type == p->functions[1].symbol.return_type);
      const SemaVariant *v = sema_variant(type, row->as.symbol.tag);
      ASSERT_TRUE(v != NULL);
      if (v) {
        ASSERT_EQ_INT(2, v->payload_count);
        ASSERT_TRUE(v->payloads[0] == &p->types[SEMA_INT] &&
                    v->payloads[1] == &p->types[SEMA_INT]);
      }
      ASSERT_TRUE(owned_ir(&a, p) != NULL);
    }
  }
  TEST("wide semantic values and wildcard arms preserve the final unit slot") {
    SemaProgram *p = valid(&a, "f() { :row[1] }\ng(value: :row[int]) { value "
                               "|> { :row[last] => last } }\nmain() {}")
                         .program;
    enum { WIDTH = 512 };
    SemaExpr *row = p->functions[0].body->as.block.items[0];
    SemaExpr *leaf = row->as.symbol.payloads[0];
    SemaExpr **values = arena_calloc(&a, WIDTH, sizeof(*values));
    const SemaType **types = arena_calloc(&a, WIDTH, sizeof(*types));
    for (size_t i = 0; i < WIDTH; i++) {
      values[i] = copy_expr(&a, leaf);
      types[i] = &p->types[SEMA_INT];
    }
    values[WIDTH - 1] = arena_calloc(&a, 1, sizeof(**values));
    values[WIDTH - 1]->kind = SEMA_EXPR_UNIT;
    types[WIDTH - 1] = &p->types[SEMA_UNIT];
    row->as.symbol.payloads = values;
    row->as.symbol.count = WIDTH;
    SemaVariant v = {row->as.symbol.tag, types, WIDTH};
    const SemaType *type = sema_symbol_set(p, &v, 1);
    SemaFunction *g = &p->functions[1];
    g->symbol.params[0]->type = g->symbol.params[0]->annotation = type;
    SemaExpr *match = g->body->as.block.items[0]->as.block.items[1];
    SemaArm *arm = &match->as.match.arms[0];
    SemaVar *last = arm->payload_bindings[0];
    arm->payload_bindings =
        arena_calloc(&a, WIDTH, sizeof(*arm->payload_bindings));
    arm->payload_bindings[WIDTH - 1] = last;
    arm->payload_count = WIDTH;
    for (unsigned pass = 0; pass < 2; pass++) {
      Diagnostics errors;
      recheck(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      ASSERT_TRUE(p->functions[0].symbol.return_type == type);
      ASSERT_TRUE(last->type == types[WIDTH - 1] &&
                  g->symbol.return_type == types[WIDTH - 1]);
      ASSERT_EQ_INT(WIDTH, sema_variant(type, v.tag)->payload_count);
      ASSERT_TRUE(owned_ir(&a, p) != NULL);
    }
  }
  TEST("malformed positional IR is rejected before any C is published") {
    SemaProgram *p =
        valid(&a, "main() { :pair[1, \"x\"] |> { :pair[a, b] => print(b) } }")
            .program;
    IrProgram *ir = owned_ir(&a, p);
    if (ir)
      for (size_t i = 0; i < ir->functions[0].count; i++) {
        IrInstruction *in = &ir->functions[0].instructions[i];
        if (in->op != IR_SYMBOL && in->op != IR_SYMBOL_PAYLOAD)
          continue;
        size_t saved = in->op == IR_SYMBOL ? in->as.symbol.count : in->position;
        if (in->op == IR_SYMBOL)
          in->as.symbol.count = 1;
        else
          in->position = 2;
        FILE *out = tmpfile();
        ASSERT_TRUE(out != NULL);
        if (out) {
          ASSERT_TRUE(!emit_c(out, ir));
          ASSERT_EQ_INT(0, ftell(out));
          fclose(out);
        }
        if (in->op == IR_SYMBOL)
          in->as.symbol.count = saved;
        else
          in->position = saved;
      }
  }
  TEST("multiple implicit success positions keep their explicit internal "
       "boundary") {
    SemaProgram *p = valid(&a, "f() -> :ok[int] { :ok[1] }\nmain() {}").program;
    SemaExpr *ok = p->functions[0].body->as.block.items[0];
    SemaExpr *items[] = {ok->as.symbol.payloads[0],
                         copy_expr(&a, ok->as.symbol.payloads[0])};
    ok->as.symbol.payloads = items;
    ok->as.symbol.count = 2;
    Diagnostics errors;
    recheck(&a, p, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(0, strcmp("Multiple ':ok' payloads are not implemented yet.",
                            errors.items[0]->message));
    ASSERT_TRUE(!p->checked && ir_lower(&a, p) == NULL);
    p = valid(&a, "f(value: :ok[int] | :!bad) { value! }\nmain() {}").program;
    const SemaType *types[] = {&p->types[SEMA_INT], &p->types[SEMA_INT]};
    SemaVariant variants[] = {{p->ok, types, 2},
                              {sema_tag(p, p->module, "bad"), NULL, 0}};
    const SemaType *input = sema_symbol_set(p, variants, 2);
    p->functions[0].symbol.params[0]->type =
        p->functions[0].symbol.params[0]->annotation = input;
    recheck(&a, p, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(0, strcmp("Multiple ':ok' payloads are not implemented yet.",
                            errors.items[0]->message));
    ASSERT_TRUE(!p->checked && ir_lower(&a, p) == NULL);
  }
  TEST("payload lists preserve zero/unit; only multi-ok remains scoped") {
    valid(&a,
          "Empty = :empty[]\nItem = :item[int,]\n"
          "main() { let e: Empty = :empty[]\ne |> { :empty[] => () }\n"
          ":unit[()] |> { :unit[_] => () }\n:item[1,] |> { :item[x,] => x } }");
    invalid(&a, "main() { let value: :empty[] = :empty[()] }",
            "does not match");
    const char *cases[] = {"E = :ok[Missing, Other]\nmain() {}",
                           "main() { :ok[unknown, missing()] }",
                           "main() { 1 |> { :ok[a, b] => unknown(a, b) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && !r.program->resolved);
      if (r.errors.count)
        ASSERT_EQ_INT(0,
                      strcmp("Multiple ':ok' payloads are not implemented yet.",
                             r.errors.items[0]->message));
    }
  }
  arena_destroy(&a);
  RUN_TESTS();
}
