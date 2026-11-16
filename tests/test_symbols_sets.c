#include "symbols_support.h"

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
  TEST("alias dependencies are iterative, cycles diagnosed, struct indirection "
       "allowed") {
    invalid(&a, "A = B\nB = A\nmain() {}", "Recursive named types");
    invalid(&a, "A = :next[A]\nmain() {}", "Recursive named types");
    invalid(&a, "A = :a\nA() {}\nmain() {}", "cannot share a name");
    invalid(&a, "A { unit: () }\nA = :a\nmain() {}", "reserved named type");
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
  arena_destroy(&a);
  RUN_TESTS();
}
