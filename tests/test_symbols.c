#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
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
  SourceFile *s = source_create(a, "symbols.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  ASSERT_TRUE(r.program->module == s);
  ASSERT_EQ_INT(1, r.program->ok->id);
  size_t id = r.program->tag_count;
  for (const SemaTag *tag = r.program->tags; tag; tag = tag->next) {
    if (!strstr(text, ":!")) {
      if (tag->module && !tag->kind_span.file) {
        ASSERT_TRUE(!strcmp(tag->name, "invalid") ||
                    !strcmp(tag->name, "overflow"));
        ASSERT_TRUE(tag->kind_declared);
        ASSERT_EQ_INT(SEMA_TAG_ERROR, tag->kind);
      } else
        ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, tag->kind);
    }
    ASSERT_EQ_INT(id, tag->id);
    id--;
    ASSERT_TRUE(
        tag->module ==
        ((!strcmp(tag->name, "ok") || !strcmp(tag->name, "none")) ? NULL : s));
  }
  ASSERT_EQ_INT(0, id);
  return r;
}
static Result valid(Arena *a, const char *text) {
  Result r = check(a, text);
  if (r.errors.count)
    diagnostics_print(&r.errors, stderr);
  ASSERT_EQ_INT(0, r.errors.count);
  ASSERT_TRUE(r.program->checked);
  if (r.program->checked) {
    IrProgram *ir = ir_lower(a, r.program);
    ASSERT_TRUE(ir != NULL);
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL);
    if (out) {
      ASSERT_TRUE(!emit_c(out, ir));
      ir_insert_rc(a, ir);
      size_t count = ir->functions[0].count;
      ir_insert_rc(a, ir);
      ASSERT_EQ_INT(count, ir->functions[0].count);
      ASSERT_TRUE(emit_c(out, ir));
      fclose(out);
    }
  }
  return r;
}
static Result invalid(Arena *a, const char *text, const char *message) {
  Result r = check(a, text);
  int found = 0;
  for (size_t i = 0; i < r.errors.count; i++)
    found |= strstr(r.errors.items[i]->message, message) != NULL;
  if (!found)
    diagnostics_print(&r.errors, stderr);
  ASSERT_TRUE(found);
  ASSERT_TRUE(!r.program->checked);
  ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  return r;
}
static IrProgram *owned_ir(Arena *a, SemaProgram *p) {
  ASSERT_TRUE(p->checked);
  IrProgram *ir = ir_lower(a, p);
  ASSERT_TRUE(ir != NULL);
  if (!ir)
    return NULL;
  ir_insert_rc(a, ir);
  FILE *out = tmpfile();
  ASSERT_TRUE(out != NULL);
  if (out) {
    ASSERT_TRUE(emit_c(out, ir));
    fclose(out);
  }
  size_t count = ir->functions[0].count;
  ir_insert_rc(a, ir);
  ASSERT_EQ_INT(count, ir->functions[0].count);
  return ir;
}
// Build internal semantic trees for independent checking and lowering tests.
static SemaExpr *copy_expr(Arena *a, const SemaExpr *source) {
  SemaExpr *copy = arena_alloc(a, sizeof(*copy));
  *copy = *source;
  return copy;
}
static SemaVar *extra_binding(Arena *a, SemaProgram *p, SemaFunction *f,
                              const char *name, Span span) {
  SemaVar *v = arena_calloc(a, 1, sizeof(*v));
  v->name = name;
  v->span = span;
  v->id = f->variable_count;
  v->type = v->annotation = &p->types[SEMA_UNKNOWN];
  v->data_mutable = 1; // Local binding; flow carries read-only provenance.
  SemaVar **variables =
      arena_calloc(a, f->variable_count + 1, sizeof(*variables));
  memcpy(variables, f->variables, f->variable_count * sizeof(*variables));
  variables[f->variable_count++] = v;
  f->variables = variables;
  f->variable_capacity = f->variable_count;
  return v;
}
static void recheck(Arena *a, SemaProgram *p, Diagnostics *errors) {
  diagnostics_init(errors, a);
  sema_check(p, errors);
}
// Permission diagnostics must identify the argument/RHS, not the call or
// destination field, and report once after a loop fixpoint on every check.
static void permission_diagnostic(Arena *a, const char *text, const char *site,
                                  const char *value, const char *message) {
  const char *at = strstr(text, site), *within = strstr(site, value);
  ASSERT_TRUE(at != NULL && within != NULL);
  if (!at || !within)
    return;
  ASSERT_TRUE(strstr(at + strlen(site), site) == NULL);
  size_t start = (size_t)(at - text) + (size_t)(within - site);
  Result r = check(a, text);
  ASSERT_TRUE(r.program->resolved);
  for (unsigned pass = 0; pass < 3; pass++) {
    Diagnostics again;
    diagnostics_init(&again, a);
    const Diagnostics *errors = &r.errors;
    if (pass == 1) {
      sema_check_permissions(r.program, &again);
      errors = &again;
    } else if (pass == 2) {
      sema_check(r.program, &again);
      errors = &again;
    }
    if (errors->count != 1) {
      fprintf(stderr, "permission case: %s\n", text);
      diagnostics_print(errors, stderr);
    }
    ASSERT_EQ_INT(1, errors->count);
    if (errors->count) {
      ASSERT_EQ_STRN(message, errors->items[0]->message,
                     strlen(errors->items[0]->message));
      ASSERT_EQ_INT(start, errors->items[0]->span.start);
      ASSERT_EQ_INT(start + strlen(value), errors->items[0]->span.end);
    }
    ASSERT_TRUE(!r.program->checked);
    ASSERT_TRUE(ir_lower(a, r.program) == NULL);
  }
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("aliases flatten, deduplicate and intern independently of listing "
       "order") {
    Result r = valid(&a, "All = Other | :b[int] | :a\n"
                         "Other = :a | :b[int] | :a\n"
                         "Again = :b[int] | :a\nmain() {}\n");
    const SemaType *type = r.program->aliases[0].type;
    ASSERT_TRUE(type == r.program->aliases[1].type);
    ASSERT_TRUE(type == r.program->aliases[2].type);
    ASSERT_EQ_INT(2, type->symbols->count);
    ASSERT_EQ_INT(3, r.program->tag_count); // Includes the program's :ok tag.
    ASSERT_TRUE(type->symbols->variants[0].tag->id <
                type->symbols->variants[1].tag->id);
    ASSERT_EQ_INT(
        0, sema_variant(type, sema_tag(r.program, r.program->module, "a"))
               ->payload_count);
    ASSERT_TRUE(sema_single_payload(sema_variant(
                    type, sema_tag(r.program, r.program->module, "b"))) ==
                &r.program->types[SEMA_INT]);
  }
  TEST(
      "tag identity includes its module while canonical sets keep global IDs") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *other = source_create(&a, "other.brf", "", 0);
    ASSERT_EQ_INT(1, p->tag_count); // Do not eagerly create :none or shift IDs.
    SemaTag *local = sema_tag(p, p->module, "found");
    SemaTag *remote = sema_tag(p, other, "found");
    ASSERT_TRUE(local != remote);
    ASSERT_TRUE(local->module == p->module && remote->module == other);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, local->kind);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, remote->kind);
    ASSERT_EQ_INT(2, local->id);
    ASSERT_EQ_INT(3, remote->id);
    ASSERT_TRUE(p->tags == remote && remote->next == local);
    ASSERT_TRUE(sema_tag(p, p->module, arena_strdup(&a, "found")) == local);
    ASSERT_TRUE(sema_tag(p, other, arena_strdup(&a, "found")) == remote);
    ASSERT_EQ_INT(3, p->tag_count);
    const SemaType *number = &p->types[SEMA_INT], *text = &p->types[SEMA_STR];
    SemaVariant x = {local, &number, 1}, y = {remote, &text, 1};
    const SemaType *left = sema_symbol_set(p, &x, 1);
    const SemaType *right = sema_symbol_set(p, &y, 1);
    ASSERT_TRUE(left != right);
    ASSERT_TRUE(!sema_assignable(left, right));
    ASSERT_TRUE(!sema_assignable(right, left));
    ASSERT_TRUE(sema_variant(left, remote) == NULL);
    const SemaType *both = sema_symbol_union(p, left, right);
    ASSERT_TRUE(both != NULL);
    if (both) {
      ASSERT_EQ_INT(2, both->symbols->count);
      ASSERT_TRUE(both->symbols->variants[0].tag == local);
      ASSERT_TRUE(both->symbols->variants[1].tag == remote);
      ASSERT_TRUE(sema_single_payload(sema_variant(both, local)) == number);
      ASSERT_TRUE(sema_single_payload(sema_variant(both, remote)) == text);
      ASSERT_TRUE(sema_assignable(left, both));
      ASSERT_TRUE(sema_assignable(right, both));
      SemaVariant reversed[] = {y, x, y};
      ASSERT_TRUE(sema_symbol_set(p, reversed, 3) == both);
      ASSERT_TRUE(sema_symbol_union(p, right, left) == both);
      ASSERT_TRUE(sema_ok(both) == NULL);
      ASSERT_TRUE(sema_failures(p, both) == NULL);
    }
    SemaVariant conflict[] = {x, {local, &text, 1}};
    ASSERT_TRUE(sema_symbol_set(p, conflict, 2) == NULL);
  }
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
    ASSERT_TRUE(sema_result(p, sema_symbol_set(p, &ok, 1)) == number);
    ok.payloads = same;
    ok.payload_count = 3;
    ASSERT_TRUE(sema_result(p, sema_symbol_set(p, &ok, 1)) ==
                &p->types[SEMA_ERROR]);
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
      recheck(&a, p, &errors);
      if (errors.count)
        diagnostics_print(&errors, stderr);
      ASSERT_EQ_INT(0, errors.count);
      ASSERT_TRUE(owned_ir(&a, p) != NULL);
      ASSERT_TRUE(row->type == f->written_type);
      ASSERT_TRUE(items[0]->type == types[0] && items[1]->type == types[1]);
      ASSERT_TRUE(items[1]->real == (double)(float)0.1);
      ASSERT_TRUE(sema_variant(row->type, variant.tag)->payloads[2] == nested);
    }
    types[1] = &p->types[SEMA_BOOL];
    f->written_type = sema_symbol_set(p, &variant, 1);
    Diagnostics errors;
    recheck(&a, p, &errors);
    ASSERT_EQ_INT(1, errors.count);
    ASSERT_EQ_INT(
        0, strcmp("Function body does not match the written return type.",
                  errors.items[0]->message));
    ASSERT_TRUE(!p->checked && ir_lower(&a, p) == NULL);
  }
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
  TEST(
      "typed payload patterns stop before type, binder and arm-body cascades") {
    const char *cases[] = {
        "main() { :item[1] |> { :item[value: Missing] => unknown(value) } }",
        "main() { 1 |> { :item[value: int] => value } }",
        ("main() { :outer[:inner[1]] |> { :outer[:inner[x: Missing]] => f(x) } "
         "}"),
        "main() { :item[1] |> { :item[(x: :a | :b)] => f(x) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      if (r.errors.count) {
        ASSERT_EQ_INT(
            0, strcmp("Typed symbol payload patterns are not implemented yet.",
                      r.errors.items[0]->message));
        ASSERT_TRUE(r.errors.items[0]->span.file == r.program->module);
        ASSERT_TRUE(r.errors.items[0]->fixes == NULL);
      }
    }
    Result many =
        check(&a, "main() { 1 |> { :pair[x: Missing, y: Other] => f(x, y) } }");
    ASSERT_EQ_INT(1, many.errors.count);
    if (many.errors.count)
      ASSERT_EQ_INT(
          0, strcmp("Typed symbol payload patterns are not implemented yet.",
                    many.errors.items[0]->message));
  }
  TEST("late payload aliases are resolved and cycles still need a struct") {
    valid(&a, "A = :pair[int, B]\nB = :leaf[str]\n"
              "main() { let a: A = :pair[1, :leaf[\"value\"]]\n"
              "a |> { :pair[_, b] => b |> { :leaf[s] => print(s) } } }");
    invalid(&a, "A = :pair[int, A]\nmain() {}", "struct indirection");
    invalid(&a, "A = :pair[int, B]\nB = :other[(), A]\nmain() {}",
            "struct indirection");
    invalid(&a, "A = :pair[int, Missing]\nmain() {}", "type is unknown");
    const char *cases[] = {
        "main() { :pair[1, 2] |> { :pair[a, 2] => missing(a) } }",
        "main() { :pair[1, 2] |> { :pair[a, :nested[x]] => missing(a) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      if (r.errors.count)
        ASSERT_EQ_INT(
            0,
            strcmp("Refutable symbol payload patterns are not implemented yet.",
                   r.errors.items[0]->message));
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
    }
  }
  TEST("qualified symbols stop before unavailable payloads and arm bodies") {
    const char *cases[] = {
        "A = Db.:!bad[Missing]\nmain() {}",
        "A = Db.:next[A]\nmain() {}",
        "A = Db.:pair[Missing, Other]\nmain() {}",
        "main() { Db.:!bad[missing()] }",
        "main() { 1 |> { Db.:!bad[x: Missing] => missing(x) } }",
        "main() { 1 |> { :outer[Db.:!bad[x]] => missing(x) } }"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      Result r = check(&a, cases[i]);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->resolved && !r.program->checked);
      ASSERT_EQ_INT(1, r.program->tag_count); // Only the eager shared :ok.
      if (r.errors.count) {
        ASSERT_EQ_INT(
            0, strcmp("Module-qualified symbols are not implemented yet.",
                      r.errors.items[0]->message));
        ASSERT_TRUE(!r.errors.items[0]->fixes);
      }
    }
    Result local = check(&a, "main() { :bad[1]\nDb.:!bad[unknown] }");
    ASSERT_EQ_INT(1, local.errors.count);
    ASSERT_EQ_INT(2, local.program->tag_count);
    ASSERT_EQ_INT(SEMA_TAG_NEUTRAL,
                  sema_tag(local.program, local.program->module, "bad")->kind);
  }
  TEST("union syntax and ordinary Sym names preserve checked set identity") {
    Result r = valid(&a, "A = :a | :b[str]\nB = A | :c\nSym = B\n"
                         "main() { let value: Sym = :b[\"heap\"]\nvalue |> { "
                         ":a => ()\n:b[s] => print(s)\n:c => () } }");
    ASSERT_EQ_INT(3, r.program->aliases[1].type->symbols->count);
    ASSERT_TRUE(r.program->aliases[1].type == r.program->aliases[2].type);
    valid(&a, "Sym { value: int }\nmain() { Sym { value = 1 } }");
    invalid(&a, "f() -> _ {}\nmain() {}", "must declare :ok");
    invalid(&a, "A = _\nmain() {}", "only allowed as function return types");
    invalid(&a, "A = int | :a\nmain() {}", "inclusion requires");
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
  TEST("conversion failure kinds reject the old neutral characterization") {
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
        {"Bad = :overflow\nf(n: lng) { if n == 0 { ret 1 }\nint(g(n)?)? }\n"
         "g(n: lng) { if n == 0 { ret lng(1) }\nlng(f(n)?) }\nmain() {}",
         "int(g(n)?)", ":overflow"}};
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
  TEST("module-qualified tag interning stays stable across map growth") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *other = source_create(&a, "other.brf", "", 0);
    enum { COUNT = 300 };
    SemaTag *tags[COUNT][2];
    SemaVariant variants[COUNT * 2];
    const SemaType *number = &p->types[SEMA_INT], *text = &p->types[SEMA_STR];
    for (size_t i = 0; i < COUNT; i++) {
      char *name = arena_alloc(&a, 16);
      snprintf(name, 16, "tag%zu", i);
      tags[i][0] = sema_tag(p, p->module, name);
      tags[i][1] = sema_tag(p, other, name);
      variants[i * 2] = (SemaVariant){tags[i][0], &number, 1};
      variants[i * 2 + 1] = (SemaVariant){tags[i][1], &text, 1};
    }
    ASSERT_EQ_INT(COUNT * 2 + 1, p->tag_count);
    const SemaType *set = sema_symbol_set(p, variants, COUNT * 2);
    for (size_t i = 0; i < COUNT; i++) {
      char name[16];
      snprintf(name, sizeof(name), "tag%zu", i);
      ASSERT_TRUE(sema_tag(p, p->module, name) == tags[i][0]);
      ASSERT_TRUE(sema_tag(p, other, name) == tags[i][1]);
      ASSERT_EQ_INT(i * 2 + 2, tags[i][0]->id);
      ASSERT_EQ_INT(i * 2 + 3, tags[i][1]->id);
      ASSERT_TRUE(sema_single_payload(sema_variant(set, tags[i][0])) ==
                  &p->types[SEMA_INT]);
      ASSERT_TRUE(sema_single_payload(sema_variant(set, tags[i][1])) ==
                  &p->types[SEMA_STR]);
    }
    ASSERT_EQ_INT(COUNT * 2 + 1, p->tag_names.count);
  }
  TEST("tag ID exhaustion preserves prior identities and never creates ID "
       "zero") {
    SemaProgram *p = valid(&a, "main() {}").program;
    SourceFile *other = source_create(&a, "other.brf", "", 0);
    p->tag_count = UINT32_MAX - 1;
    SemaTag *last = sema_tag(p, other, "last");
    ASSERT_TRUE(last != NULL);
    if (last) {
      ASSERT_EQ_INT(UINT32_MAX, last->id);
      ASSERT_TRUE(last->module == other);
      ASSERT_EQ_INT(SEMA_TAG_NEUTRAL, last->kind);
    }
    size_t entries = p->tag_names.count;
    ASSERT_TRUE(sema_tag(p, other, "last") == last);
    ASSERT_TRUE(sema_tag(p, other, "ok") == p->ok);
    ASSERT_TRUE(sema_tag(p, p->module, "last") == NULL);
    ASSERT_TRUE(sema_tag(p, other, "extra") == NULL);
    ASSERT_TRUE(sema_tag(p, other, "none") == NULL);
    ASSERT_EQ_INT(UINT32_MAX, p->tag_count);
    ASSERT_EQ_INT(entries, p->tag_names.count);
    ASSERT_TRUE(p->tags == last && last->next == p->ok);
  }
  TEST("all payload types, forward structs, nested symbols and unit payloads") {
    valid(&a, "Choice = :object[Node] | :nested[:yes | :no] | "
              ":unit[()] | :empty\n"
              "Node { choice: Choice }\n"
              "main() { let n = Node { choice = :empty }\n"
              "let c: Choice = :object[n]\nc |> { :object[x] => x.choice\n"
              "other => other }\n() }");
    valid(&a, "main() { let a: :value[lng] = :value[4294967296]\n"
              "let b: :value[flt] = :value[0.1]\n"
              "let c: :value[()] = :value[()]\n"
              "c |> { :value[x] => x } }");
    valid(&a, "main() { :outer[:inner[\"a\" + \"b\"]] |> { :outer[x] => "
              "x |> { :inner[s] => print(s) } } }");
    valid(
        &a,
        "Inner = :a[str] | :b\n"
        "f(b: bool) -> :outer[Inner] { if b { :outer[:a[\"x\"]] } "
        "else { :outer[:b] } }\n"
        "main() { let x: :outer[Inner] = :outer[:a[\"x\"]]\n"
        "let y = :b\nlet z: :outer[Inner] = :outer[y]\n"
        "f(true) |> { :outer[inner] => inner |> { :a[_] => ()\n:b => () } } }");
    invalid(&a, "main() { let a: :a = :a[()] }", "does not match");
    invalid(&a, "main() { let a: :a[()] = :a }", "does not match");
    invalid(&a, "main() { let a: :a[lng] = :a[true] }", "does not match");
  }
  TEST("packing, widening and payload reads independently acquire owners") {
    Result r = valid(
        &a, "make() -> :a[str] | :b { :a[\"x\" + \"y\"] }\n"
            "read(s: :a[str] | :b) { s |> { :a[text] => text\n:b => \"\" } }\n"
            "main() { read(make()) }");
    IrProgram *ir = ir_lower(&a, r.program);
    ir_insert_rc(&a, ir);
    unsigned seen = 0;
    for (size_t i = 0; i < ir->count; i++) {
      IrFunction *f = &ir->functions[i];
      for (size_t j = 0; j < f->count; j++) {
        IrInstruction in = f->instructions[j];
        if (in.op != IR_SYMBOL && in.op != IR_SYMBOL_CAST &&
            in.op != IR_SYMBOL_PAYLOAD)
          continue;
        seen |= in.op == IR_SYMBOL ? 1u : in.op == IR_SYMBOL_CAST ? 2u : 4u;
        if (!j || j + 1 >= f->count) {
          FAIL("missing ownership operations around symbol definition");
          continue;
        }
        ASSERT_EQ_INT(IR_DROP_TEMP, f->instructions[j - 1].op);
        ASSERT_EQ_INT(in.result, f->instructions[j - 1].a);
        ASSERT_EQ_INT(IR_RETAIN, f->instructions[j + 1].op);
        ASSERT_EQ_INT(in.result, f->instructions[j + 1].a);
      }
    }
    ASSERT_EQ_INT(7, seen);
  }
  TEST("subset assignment, fields, parameters, returns and branch joins") {
    Result r = valid(
        &a, "Tier = :bronze | :silver | :gold\n"
            "Box { tier: Tier }\n"
            "choose(b: bool) { if b { :bronze } else { :gold } }\n"
            "forward(b: bool) -> Tier { if b { ret :silver }\nchoose(b) }\n"
            "same(x: Tier) -> Tier { x }\n"
            "main() -> () { var x: Tier = :bronze\nx = :gold\n"
            "let box = Box { tier = :silver }\nbox.tier = choose(false)\n"
            "forward(true) |> same() |> { Tier => () } }");
    ASSERT_EQ_INT(2,
                  r.program->functions[0].symbol.return_type->symbols->count);
    invalid(&a, "f(x: :a) {}\nmain() { f(:b) }", "argument has the wrong type");
    invalid(&a, "f(b: bool) -> :a { if b { ret :b }\n:a }\nmain() {}",
            "Return value");
    invalid(&a, "f() -> :a { :b }\nmain() {}", "body does not match");
    invalid(&a, "main() { var x = :a\nx = :b }", "Assignment value");
    invalid(&a, "B { x: :a }\nmain() { B { x = :b } }",
            "initializer has the wrong type");
  }
  TEST("sets grow across recursive SCCs without locking out later tags") {
    Result r = valid(
        &a, "f(n: int) { if n == 0 { :a } else { g(n - 1) } }\n"
            "g(n: int) { if n == 0 { :b } else { f(n - 1) } }\nmain() {}\n");
    const SemaType *type = r.program->functions[0].symbol.return_type;
    ASSERT_TRUE(type == r.program->functions[1].symbol.return_type);
    ASSERT_EQ_INT(2, type->symbols->count);
    size_t count = r.program->symbol_set_count;
    sema_check(r.program, &r.errors);
    ASSERT_EQ_INT(0, r.errors.count);
    ASSERT_TRUE(type == r.program->functions[0].symbol.return_type);
    ASSERT_EQ_INT(count, r.program->symbol_set_count);
    valid(&a, "f(b: bool) { if b { ret :a[1] }\n:a[wide()] }\n"
              "wide() -> lng { 4294967296 }\nmain() {}\n");
    valid(&a, "f() { ret :a\n:b }\nmain() {}\n");
    invalid(&a, "f(b: bool) { if b { :a[1] } else { :a[false] } }\nmain() {}",
            "conflicting payload");
    valid(&a, "f(b: bool) { if b { :a } else { 1 } }\nmain() {}");
    invalid(&a, "main() { let x = if true { :a } else { 1 } }",
            "compatible types");
    invalid(
        &a,
        "f(n: int) { if n == 0 { :end } else { :next[f(n - 1)] } }\nmain() {}",
        "consistent return type");
  }
  TEST("large recursive symbol components use the graph's explicit stacks") {
    size_t capacity = 250000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length +=
          (size_t)snprintf(text + length, capacity - length,
                           "f%zu(b: bool) { if b { :%s } else { f%zu(b) } }\n",
                           i, i == 2049 ? "last" : "first", (i + 1) % 2050);
    snprintf(text + length, capacity - length, "main() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->count);
    for (size_t i = 0; i < 2050; i++)
      ASSERT_EQ_INT(2,
                    r.program->functions[i].symbol.return_type->symbols->count);
  }
  TEST("hashed interning keeps sets canonical across growth and conflicts") {
    Result r = valid(&a, "main() {}");
    SemaProgram *p = r.program;
    enum { COUNT = 300 }; // Several interning-table resizes.
    SemaTag *tags[COUNT];
    const SemaType *single[COUNT];
    for (size_t i = 0; i < COUNT; i++) {
      char *name = arena_alloc(&a, 16);
      snprintf(name, 16, "t%zu", i);
      tags[i] = sema_tag(p, p->module, name);
      SemaVariant v = {tags[i], NULL, 0};
      single[i] = sema_symbol_set(p, &v, 1);
      ASSERT_TRUE(sema_symbol_set(p, &v, 1) == single[i]);
    }
    const SemaType *forward = single[0], *backward = single[COUNT - 1];
    for (size_t i = 1; i < COUNT; i++) {
      forward = sema_symbol_union(p, forward, single[i]);
      backward = sema_symbol_union(p, backward, single[COUNT - 1 - i]);
    }
    ASSERT_TRUE(forward == backward);
    ASSERT_EQ_INT(COUNT, forward->symbols->count);
    SemaVariant reversed[COUNT];
    for (size_t i = 0; i < COUNT; i++)
      reversed[i] = (SemaVariant){tags[COUNT - 1 - i], NULL, 0};
    ASSERT_TRUE(sema_symbol_set(p, reversed, COUNT) == forward);
    const SemaType *number = &p->types[SEMA_INT];
    SemaVariant conflict = {tags[7], &number, 1};
    ASSERT_TRUE(sema_symbol_union(p, forward,
                                  sema_symbol_set(p, &conflict, 1)) == NULL);
    SemaVariant duplicate[] = {
        {tags[3], NULL, 0}, {tags[2], NULL, 0}, {tags[3], &number, 1}};
    ASSERT_TRUE(sema_symbol_set(p, duplicate, 3) == NULL);
    SemaVariant outcome[] = {
        {tags[4], NULL, 0}, {p->ok, &number, 1}, {tags[5], NULL, 0}};
    const SemaType *full = sema_symbol_set(p, outcome, 3);
    ASSERT_TRUE(sema_ok(full) != NULL &&
                sema_single_payload(sema_ok(full)) == number);
    ASSERT_TRUE(sema_failures(p, full) ==
                sema_symbol_union(p, single[4], single[5]));
    ASSERT_TRUE(sema_ok(sema_failures(p, full)) == NULL);
  }
  TEST("emission declares only referenced symbol sets, in dependency order") {
    // Each ? grows h's inferred error set by one variant. Inference interns
    // every intermediate union, but no lowered value has those types.
    char text[16384];
    size_t used = 0;
    for (int i = 0; i < 40; i++)
      used += (size_t)snprintf(text + used, sizeof(text) - used,
                               "g%d(x: int) -> :ok[int] | :!e%d[int] { x }\n",
                               i, i);
    used += (size_t)snprintf(text + used, sizeof(text) - used, "h(x: int) {\n");
    for (int i = 0; i < 40; i++)
      used += (size_t)snprintf(text + used, sizeof(text) - used,
                               "let y%d = g%d(x)?\n", i, i);
    snprintf(text + used, sizeof(text) - used,
             "x }\nmain() { h(1) |> { :ok[v] => print(str(v))\n"
             "_ => print(\"e\") } }\n");
    Result r = valid(&a, text);
    for (const SemaTag *tag = r.program->tags; tag; tag = tag->next)
      if (tag != r.program->ok)
        ASSERT_EQ_INT(SEMA_TAG_ERROR, tag->kind);
    IrProgram *ir = ir_lower(&a, r.program);
    ASSERT_TRUE(ir != NULL);
    if (ir) {
      ASSERT_TRUE(r.program->symbol_set_count > ir->symbol_set_count + 30);
      unsigned char *declared =
          arena_calloc(&a, r.program->symbol_set_count, 1);
      for (size_t i = 0; i < ir->symbol_set_count; i++) {
        const SemaSymSet *s = ir->symbol_sets[i];
        for (size_t j = 0; j < s->count; j++) {
          const SemaType *payload = sema_single_payload(&s->variants[j]);
          if (payload && payload->kind == SEMA_SYM)
            ASSERT_TRUE(declared[payload->symbols->id]);
        }
        declared[s->id] = 1;
      }
      for (size_t i = 0; i < ir->count; i++)
        for (size_t j = 0; j < ir->functions[i].temporaries; j++) {
          const SemaType *t = ir->functions[i].temporary_types[j];
          if (t && t->kind == SEMA_SYM)
            ASSERT_TRUE(declared[t->symbols->id]);
        }
    }
    // Nested payload sets are declared even when only their container is used.
    valid(&a, "f() -> :a[:b[:c[str]]] { :a[:b[:c[\"x\"]]] }\n"
              "main() { f() |> { :a[inner] => inner |> {\n"
              ":b[deep] => deep |> { :c[s] => print(s) } } } }");
  }
  TEST("global tags may have distinct payload types until their sets meet") {
    Result r = valid(&a, "A = :value[int]\nB = :value[str]\nmain() {}\n");
    ASSERT_TRUE(r.program->aliases[0].type != r.program->aliases[1].type);
    ASSERT_TRUE(r.program->aliases[0].type->symbols->variants[0].tag ==
                r.program->aliases[1].type->symbols->variants[0].tag);
    invalid(&a, "A = :value[int]\nB = A | :value[str]\nmain() {}",
            "conflicting payload");
    invalid(&a, "A = :value | :value[()]\nmain() {}", "conflicting payload");
    invalid(&a, "A = int | :included\nmain() {}", "inclusion requires");
    Result scalar = valid(&a, "A = int\nmain() {}");
    ASSERT_TRUE(scalar.program->aliases[0].type ==
                &scalar.program->types[SEMA_INT]);
    invalid(&a, "A = int\nB = A | :included\nmain() {}", "inclusion requires");
  }
  TEST("symbol patterns, named groups, residual binders and lexical scope") {
    Result r = valid(&a, "Pair = :a | :b[str]\n"
                         "f(x: Pair | :c) { x |> { :a => ret :handled\n"
                         "other => other } }\n"
                         "main() { let x: Pair | :c = :b[\"B\"]\n"
                         "x |> { :a => ()\nPair => ()\n:c => () } }");
    const SemaType *ft = r.program->functions[0].symbol.return_type;
    ASSERT_TRUE(!sema_variant(ft, sema_tag(r.program, r.program->module, "a")));
    ASSERT_EQ_INT(3, ft->symbols->count);
    invalid(&a, "main() { :a[1] |> { :a[x] => x++ } }", "immutable binding");
    invalid(&a, "main() { :a[1] |> { :a[x] => x }\nx }", "Unknown binding");
    invalid(&a, "main() { let x: :a | :b = :a\nx |> { :a => () } }",
            "not exhaustive");
    invalid(&a, "main() { :a |> { :a => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "A = :a\nmain() { :b |> { A => ()\n_ => () } }",
            "Unreachable match arm");
    invalid(&a, "main() { :a |> { :b => ()\n_ => () } }", "tag is not in");
    invalid(&a, "main() { :a[1] |> { :a => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { :a[_] => () } }", "wrong payload shape");
    invalid(&a, "main() { :a |> { 1 => ()\n_ => () } }", "require symbol or");
    invalid(&a, "main() { 1 |> { :a => ()\n_ => () } }",
            "require a symbol input");
    invalid(&a, "A = :a[str]\nmain() { :a[1] |> { A => () } }",
            "conflicting payload");
    invalid(&a, "main() { :a[1] |> { :a[1] => () } }",
            "Refutable symbol payload");
    invalid(&a, "A = :a\nmain() { :outer[:a] |> { :outer[A] => () } }",
            "Refutable symbol payload");
  }
  TEST("equality extends through finite symbols, never recursive shapes") {
    valid(&a, "B { value: :text[str] | :unit[()] }\n"
              "main() { let a = B { value = :text[\"a\"] }\n"
              "let b = B { value = :unit[()] }\nprint(str(a == b))\n"
              "print(str(:a == :b))\nprint(str(:n[1] == :n[2])) }");
    Result r = valid(&a, "Link = :none | :next[Node]\nNode { link: "
                         "Link }\nmain() {}\n");
    ASSERT_TRUE(!r.program->structs[0].equatable);
    ASSERT_TRUE(!r.program->aliases[0].type->symbols->equatable);
    invalid(&a,
            "Link = :none | :next[Node]\nNode { link: Link }\n"
            "f(x: Link, y: Link) { x == y }\nmain() {}",
            "can contain a reference cycle have no == or !=");
    invalid(&a, "main() { :a < :b }", "Comparison of this type");
    invalid(&a, "main() { :a[1] == :a[false] }", "conflicting payload");
  }
  TEST("permissions traverse payloads, calls, narrowing, aliases and stores") {
    const char *prefix = "P { x: int }\nChoice = :some[P] | :none\n";
    char text[4096];
#define CASE(body)                                                             \
  (snprintf(text, sizeof(text), "%s%s\nmain() {}", prefix, body), text)
    invalid(&a,
            CASE("f(s: Choice) { s |> { :some[p] => p.x = 1\n:none => () } }"),
            "Cannot mutate");
    valid(&a,
          CASE("f(s: &Choice) { s |> { :some[p] => p.x = 1\n:none => () } }"));
    invalid(&a, CASE("f(p: P) { :some[p] |> { :some[q] => q.x = 1 } }"),
            "Cannot mutate");
    invalid(
        &a,
        CASE("get(s: Choice) { s |> { :some[p] => p\n:none => P { x = 0 } } }\n"
             "f(s: Choice) { get(s).x = 1 }"),
        "Cannot mutate");
    invalid(&a,
            CASE("wrap(p: P) { :some[p] }\n"
                 "f(p: P) { wrap(p) |> { :some[q] => q.x = 1 } }"),
            "Cannot mutate");
    invalid(&a, CASE("set(s: &Choice) {}\nf(s: Choice) { set(s) }"),
            "read-only value cannot be passed");
    invalid(&a, CASE("set(s: &Choice) {}\nf(p: P) { set(:some[p]) }"),
            "read-only value cannot be passed");
    invalid(&a, CASE("H { s: Choice }\nf(h: &H, p: P) { h.s = :some[p] }"),
            "cannot be stored in a field");
    valid(&a, CASE("H { s: Choice }\nf(h: &H, p: &P) { h.s = :some[p] }"));
    invalid(&a,
            CASE("f(s: Choice) { s |> { :none => ()\nother => other |> { "
                 ":some[p] => p.x = 1 } } }"),
            "Cannot mutate");
    valid(&a, CASE("f(s: Choice) { var x = s\nx = :some[P { x = 1 }]\n"
                   "x |> { :some[p] => p.x = 2\n:none => () } }"));
#undef CASE
  }
  TEST(
      "permission messages distinguish read-only values from mutable targets") {
    const char *prefix =
        "P { n: int\n bump(&self) { self.n++ } }\n"
        "Choice = :some[P] | :none\nNested = :outer[Choice] | :empty\n"
        "Scalar = :text[str] | :number[int]\nWrapper { part: P\nmarker: int }\n"
        "Holder { item: P\nchoice: Choice\nnested: Nested\nwrapper: Wrapper\n"
        "text: str\ncount: int\nscalar: Scalar }\n"
        "mut(p: &P) { p.n++ }\n"
        "mutChoice(s: &Choice) {}\nmutNested(s: &Nested) {}\n"
        "mutScalar(s: &Scalar) {}\nsameChoice(s: Choice) -> Choice { s }\n";
    const char *borrow =
        "A read-only value cannot be passed to an & parameter; "
        "take the parameter it comes from as &.";
    const char *store = "A read-only value cannot be stored in a field; take "
                        "the parameter it comes from as &.";
    struct {
      const char *type, *body, *site, *value;
      int storing;
    } cases[] = {
        {"P", "mut(value)", "mut(value)", "value", 0},
        {"P", "value.bump()", "value.bump()", "value", 0},
        {"P", "value |> mut()", "value |> mut()", "value", 0},
        {"P", "let alias = value\nmut(alias)", "mut(alias)", "alias", 0},
        {"P", "mutChoice(:some[value])", "mutChoice(:some[value])",
         ":some[value]", 0},
        {"P", "mutNested(:outer[:some[value]])",
         "mutNested(:outer[:some[value]])", ":outer[:some[value]]", 0},
        {"Choice", "mutChoice(value)", "mutChoice(value)", "value", 0},
        {"Choice", "mutChoice(sameChoice(value))",
         "mutChoice(sameChoice(value))", "sameChoice(value)", 0},
        {"Choice", "value |> { :none => ()\nrest => mutChoice(rest) }",
         "mutChoice(rest)", "rest", 0},
        {"Choice", "value |> { :some[p] => mut(p)\n:none => () }", "mut(p)",
         "p", 0},
        {"P", "holder.item = value", "holder.item = value", "value", 1},
        {"Choice", "holder.choice = value", "holder.choice = value", "value",
         1},
        {"P", "holder.choice = :some[value]", "holder.choice = :some[value]",
         ":some[value]", 1},
        {"P", "holder.nested = :outer[:some[value]]",
         "holder.nested = :outer[:some[value]]", ":outer[:some[value]]", 1},
        {"Choice", "holder.choice = sameChoice(value)",
         "holder.choice = sameChoice(value)", "sameChoice(value)", 1},
        {"P", "holder.wrapper = Wrapper { part = value, marker = 1 }",
         "holder.wrapper = Wrapper { part = value, marker = 1 }",
         "Wrapper { part = value, marker = 1 }", 1},
        {"P",
         "var alias = P { n = 0 }\nwhile flag { holder.item = alias\n"
         "alias = value }",
         "holder.item = alias", "alias", 1},
        {"P",
         "var alias = P { n = 0 }\nwhile flag { mut(alias)\nalias = value }",
         "mut(alias)", "alias", 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[4096];
      snprintf(
          text, sizeof(text),
          "%sf(value: %s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
          prefix, cases[i].type, cases[i].body);
      permission_diagnostic(&a, text, cases[i].site, cases[i].value,
                            cases[i].storing ? store : borrow);
      // Changing the source permission, not its ownership or active tag,
      // makes the same operations legal. The parameter binding stays immutable.
      snprintf(
          text, sizeof(text),
          "%sf(value: &%s, holder: &Holder, flag: bool) {\n%s\n}\nmain() {}",
          prefix, cases[i].type, cases[i].body);
      valid(&a, text);
    }
    char text[4096];
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: P, holder: &Holder, text: str, scalar: Scalar) {\n"
             "holder.count = value.n\nholder.text = text\n"
             "holder.scalar = scalar\nmutScalar(scalar)\n"
             "var alias = value\nalias = P { n = 1 }\nmut(alias)\n"
             "holder.item = alias\n}\nmain() {}");
    valid(
        &a,
        text); // Copied scalars/immutable data and fresh rebinding stay legal.
    snprintf(text, sizeof(text), "%s%s", prefix,
             "f(value: Choice) { mut(value) }\nmain() {}");
    Result wrong_type =
        invalid(&a, text, "Function argument has the wrong type.");
    ASSERT_EQ_INT(1, wrong_type.errors.count);
    Diagnostics permissions_only;
    diagnostics_init(&permissions_only, &a);
    sema_check_permissions(wrong_type.program, &permissions_only);
    ASSERT_EQ_INT(0, permissions_only.count); // Do not add a borrow cascade.
  }
  TEST("alias dependencies are iterative, cycles diagnosed, struct indirection "
       "allowed") {
    invalid(&a, "A = B\nB = A\nmain() {}", "Recursive named types");
    invalid(&a, "A = :next[A]\nmain() {}", "Recursive named types");
    invalid(&a, "A = :a\nA() {}\nmain() {}", "cannot share a name");
    invalid(&a, "A { unit: () }\nA = :a\nmain() {}", "reserved named type");
    invalid(&a, "type int = :a\nmain() {}", "Expected a named type");
    // Lowercase builtin names cannot form an uppercase alias declaration.
    invalid(&a, "print = :a\nmain() {}", "Statements are only allowed");
    size_t capacity = 160000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "A%zu = A%zu\n", i, i + 1);
    snprintf(text + length, capacity - length, "A2050 = :a\nmain() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->alias_count);
    ASSERT_TRUE(r.program->aliases[0].type == r.program->aliases[2050].type);
  }
  TEST("native aliases preserve scalar, unit and symbol payload identities") {
    struct {
      const char *spelling;
      SemaTypeKind kind;
    } cases[] = {
        {"bool", SEMA_BOOL},   {"chr", SEMA_CHR},   {"shrt", SEMA_SHRT},
        {"ushrt", SEMA_USHRT}, {"int", SEMA_INT},   {"uint", SEMA_UINT},
        {"lng", SEMA_LNG},     {"ulng", SEMA_ULNG}, {"flt", SEMA_FLT},
        {"dbl", SEMA_DUB},     {"str", SEMA_STR},   {"()", SEMA_UNIT}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[512];
      snprintf(text, sizeof(text),
               "Alias = Forward\nForward = %s\nA = :item[Alias]\nB = "
               ":item[%s]\nf(n: Alias) -> Forward { n }\nmain() {}",
               cases[i].spelling, cases[i].spelling);
      Result r = valid(&a, text);
      ASSERT_EQ_INT(4, r.program->alias_count);
      ASSERT_TRUE(r.program->aliases[0].type ==
                  &r.program->types[cases[i].kind]);
      ASSERT_TRUE(r.program->aliases[0].type == r.program->aliases[1].type);
      ASSERT_TRUE(r.program->aliases[2].type == r.program->aliases[3].type);
      ASSERT_TRUE(r.program->functions[0].symbol.params[0]->type ==
                  r.program->aliases[0].type);
    }
    valid(&a, "Num = lng\nText = str\nf(s: str) { Num(s)? }\nmain() { "
              "Text(Num(4294967296)) }");
    invalid(&a, "Small = shrt\nmain() { Small(32768) }", "outside the range");
    invalid(&a, "Flag = bool\nmain() { Flag(1) }", "Unknown function");
    invalid(&a, "Text = str\nmain() { Text(\"already text\") }",
            "str expects a number or bool");
    invalid(&a, "Unit = ()\nf(n: &Unit) {}\nmain() {}", "Mutable borrowing");
    invalid(&a, "Unit = ()\nf() -> Unit { ret 1 }\nmain() {}",
            "written return type");
    invalid(&a, "A = :next[B]\nB = A\nmain() {}", "Recursive named types");
    Result pattern = invalid(&a, "A = int\nmain() { :a |> { A => missing() } }",
                             "A named symbol pattern requires a symbol set.");
    ASSERT_EQ_INT(1, pattern.errors.count);
    size_t capacity = 160000;
    char *text = arena_alloc(&a, capacity);
    size_t length = 0;
    for (size_t i = 0; i < 2050; i++)
      length += (size_t)snprintf(text + length, capacity - length,
                                 "A%zu = A%zu\n", i, i + 1);
    snprintf(text + length, capacity - length, "A2050 = lng\nmain() {}\n");
    Result r = valid(&a, text);
    ASSERT_EQ_INT(2051, r.program->alias_count);
    for (size_t i = 0; i < r.program->alias_count; i++)
      ASSERT_TRUE(r.program->aliases[i].type == &r.program->types[SEMA_LNG]);
  }
  TEST("symbol coverage agrees with an independent five-tag bitset oracle") {
    Result r = valid(&a, "All = :a | :b | :c | :d | :e\nmain() {}\n");
    const SemaType *type = r.program->aliases[0].type;
    uint32_t random = UINT32_C(1234567);
    for (size_t trial = 0; trial < 500; trial++) {
      SemaExpr input = {.type = type};
      SemaExpr match = {.kind = SEMA_EXPR_MATCH};
      SemaArm arms[8] = {{0}};
      SemaVar bindings[8] = {{0}};
      unsigned masks[8], covered = 0;
      size_t expected_errors = 0;
      match.as.match.input = &input;
      match.as.match.arms = arms;
      match.as.match.count = 8;
      for (size_t i = 0; i < 8; i++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned kind = (random >> 16) % 4;
        if (kind == 0) {
          arms[i].kind = PAT_SYMBOL;
          unsigned tag = random % 5;
          arms[i].tag = type->symbols->variants[tag].tag;
          masks[i] = 1u << tag;
        } else if (kind == 1) {
          arms[i].kind = PAT_SET;
          unsigned group = (random % 31) + 1;
          SemaVariant variants[5];
          size_t n = 0;
          for (size_t j = 0; j < 5; j++)
            if (group & (1u << j))
              variants[n++] = type->symbols->variants[j];
          arms[i].set = sema_symbol_set(r.program, variants, n);
          masks[i] = group;
        } else {
          arms[i].kind = kind == 2 ? PAT_BIND : PAT_WILDCARD;
          if (kind == 2)
            arms[i].binding = &bindings[i];
          masks[i] = 31;
        }
      }
      Diagnostics errors;
      diagnostics_init(&errors, &a);
      sema_match_coverage(r.program, &match, &errors);
      for (size_t i = 0; i < 8; i++) {
        unsigned useful = masks[i] & ~covered;
        ASSERT_EQ_INT(useful != 0, arms[i].useful);
        if (!useful)
          expected_errors++;
        unsigned actual = 0;
        for (size_t j = 0; j < 5; j++)
          if (arms[i].cases &&
              sema_variant(arms[i].cases, type->symbols->variants[j].tag))
            actual |= 1u << j;
        ASSERT_EQ_INT(useful, actual);
        if (useful && arms[i].kind == PAT_BIND)
          ASSERT_TRUE(arms[i].binding->type == arms[i].cases);
        covered |= masks[i];
      }
      expected_errors += covered != 31;
      ASSERT_EQ_INT(expected_errors, errors.count);
    }
  }
  TEST("error-kind classification is shallow and agrees with subset masks") {
    ASSERT_TRUE(!sema_has_errors(NULL));
    for (unsigned kinds = 0; kinds < 8; kinds++) {
      SemaProgram *p = valid(&a, "main() {}").program;
      for (size_t i = 0; i < SEMA_SYM; i++)
        ASSERT_TRUE(!sema_has_errors(&p->types[i]));
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
        ASSERT_TRUE(type != NULL);
        size_t sets = p->symbol_set_count, tag_count = p->tag_count;
        for (unsigned repeat = 0; repeat < 3; repeat++) {
          ASSERT_EQ_INT((mask & (kinds << 2)) != 0, sema_has_errors(type));
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
        !sema_has_errors(sema_success(r.program, r.program->aliases[0].type)));
  }
  TEST("error-only calls require handling in every call position and form") {
    const char *prefix =
        "Error = :!problem[str]\n"
        "fail() -> Error { :!problem[\"owned\"] }\n"
        "take(value: Error) {}\n"
        "Box { value: Error }\n"
        "Source { n: int\nfail() -> Error { :!problem[str(self.n)] }\n"
        "create() -> Error { :!problem[\"owned\"] } }\n"
        "main() { let source = Source { n = 7 }\n";
    const char *bodies[] = {"fail()",
                            "let result = fail()",
                            "take(fail())",
                            "Box { value = fail() }",
                            ":box[fail()]",
                            "ret fail()",
                            "if false { fail() }",
                            "fail() |> take()",
                            "source.fail()",
                            "Source.create()",
                            "source |> fail()"};
    const char *required =
        "A call that can fail must be handled with ?, !, or a match.";
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[2048], source[4096];
        snprintf(lf, sizeof(lf), "%s%s\n}", prefix, bodies[i]);
        size_t length = 0;
        for (const char *s = lf; *s; s++) {
          if (crlf && *s == '\n')
            source[length++] = '\r';
          source[length++] = *s;
        }
        source[length] = 0;
        Result r = check(&a, source);
        const char *site = strstr(strstr(source, "main() {"), bodies[i]);
        ASSERT_TRUE(site != NULL);
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
          ASSERT_TRUE(!memcmp(source, r.program->module->text, length));
          if (r.errors.count) {
            Diagnostic *d = r.errors.items[0];
            ASSERT_EQ_INT(0, strcmp(required, d->message));
            ASSERT_TRUE(d->span.start >= (size_t)(site - source));
            ASSERT_TRUE(d->span.end <=
                        (size_t)(site - source) + strlen(bodies[i]));
            ASSERT_TRUE(!d->fixes && !d->notes);
          }
        }
      }
    valid(&a, "f() -> :!problem[str] { :!problem[\"owned\"] }\n"
              "main() { f() |> { :!problem[text] => print(text) } }");
    Result boundary =
        invalid(&a, "f() -> :!problem { :!problem }\nmain() { f()? }",
                "require a symbol outcome with an :ok");
    ASSERT_EQ_INT(1, boundary.errors.count);
    boundary =
        invalid(&a, "f() -> :ok[:!problem] { :ok[:!problem] }\nmain() { f() }",
                required);
    ASSERT_EQ_INT(1, boundary.errors.count);
  }
  TEST("kind-based call diagnostics survive SCC inference and recovery") {
    const char *source =
        "f(n: int) { if n == 0 { ret :!problem[\"owned\"] }\n"
        "g(n - 1) |> { other => other } }\n"
        "g(n: int) { f(n) |> { other => other } }\nmain() { f(1) }";
    Result r = check(&a, source);
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
      for (size_t i = 0; i < r.program->count; i++)
        ASSERT_TRUE(!r.program->functions[i].inference_conflict);
      if (r.errors.count)
        ASSERT_EQ_INT(
            0,
            strcmp(
                "A call that can fail must be handled with ?, !, or a match.",
                r.errors.items[0]->message));
    }
    valid(&a, "Health = :ok | :degraded\n"
              "f(b: bool) -> Health { if b { ret :ok }\ng(true) }\n"
              "g(b: bool) -> Health { f(b) }\nmain() { f(false) }");
    r = check(&a, "fail() -> :!problem { :!problem }\n"
                  "main() { missing()\nfail() }");
    ASSERT_EQ_INT(2, r.errors.count);
    ASSERT_TRUE(!r.program->checked && ir_lower(&a, r.program) == NULL);
  }
  TEST("neutral-only results including ok and none are ordinary call values") {
    valid(&a, "Health = :ok | :degraded[str] | :down\n"
              "Maybe = :ok[str] | :none\nNested = :box[:!problem[str]]\n"
              "Box { value: Nested }\n"
              "health() -> Health { :degraded[\"owned\"] }\n"
              "find() -> Maybe { :none }\n"
              "nested() -> Nested { :box[:!problem[\"owned\"]] }\n"
              "object() -> Box { Box { value = nested() } }\n"
              "take(value: Health) {}\n"
              "main() { health()\nlet h = health()\ntake(h)\ntake(health())\n"
              "find()\nlet missing = find()\nnested()\nobject() }");
  }
  TEST("outcome handling extends data symbols without changing their rules") {
    valid(&a, "main() { :ok[1] }");
    // Retain neutral data: the legacy postfix helper still uses :ok.
    Result legacy = valid(&a, "f() -> :ok | :bad { :bad }\nmain() {}");
    ASSERT_EQ_INT(
        SEMA_TAG_NEUTRAL,
        sema_tag(legacy.program, legacy.program->module, "bad")->kind);
    ASSERT_TRUE(sema_failures(
        legacy.program, legacy.program->functions[0].symbol.outcome_type));
    // The legacy postfix helper remains, but it no longer classifies calls.
    ASSERT_TRUE(
        !sema_has_errors(legacy.program->functions[0].symbol.return_type));
    invalid(&a, "f() -> :a | _ { :a }\nmain() {}", "must declare :ok");
    valid(&a, "main() { :a }");
  }
  arena_destroy(&a);
  RUN_TESTS();
}
