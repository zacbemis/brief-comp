#include "sema/sema.h"
#include <stdlib.h>
#include <string.h>

SemaTag *sema_tag(SemaProgram *p, const char *name) {
  for (SemaTag *tag = p->tags; tag; tag = tag->next)
    if (!strcmp(tag->name, name))
      return tag;
  // The emitted tag representation has 32 bits; zero is an empty ARC slot.
  if (p->tag_count == UINT32_MAX)
    return NULL;
  SemaTag *tag = arena_alloc(p->arena, sizeof(*tag));
  *tag = (SemaTag){name, ++p->tag_count, p->tags};
  p->tags = tag;
  return tag;
}
const SemaVariant *sema_variant(const SemaType *type, const SemaTag *tag) {
  if (!type || type->kind != SEMA_SYM || !tag)
    return NULL;
  const SemaSymSet *set = type->symbols;
  size_t lo = 0, hi = set->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (set->variants[mid].tag->id < tag->id)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo < set->count && set->variants[lo].tag == tag ? &set->variants[lo]
                                                         : NULL;
}
const SemaVariant *sema_ok(const SemaType *type) {
  if (type && type->kind == SEMA_SYM)
    for (size_t i = 0; i < type->symbols->count; i++)
      if (!strcmp(type->symbols->variants[i].tag->name, "ok"))
        return &type->symbols->variants[i];
  return NULL;
}
const SemaType *sema_success(SemaProgram *p, const SemaType *payload) {
  SemaVariant v = {p->ok, payload->kind == SEMA_UNIT ? NULL : payload};
  return sema_symbol_set(p, &v, 1);
}
const SemaType *sema_result(SemaProgram *p, const SemaType *type) {
  const SemaVariant *ok = sema_ok(type);
  return ok && type->symbols->count == 1
             ? (ok->payload ? ok->payload : &p->types[SEMA_UNIT])
             : type;
}
const SemaType *sema_failures(SemaProgram *p, const SemaType *type) {
  const SemaVariant *ok = sema_ok(type);
  if (!ok || type->symbols->count == 1)
    return NULL;
  size_t n = type->symbols->count;
  SemaVariant *v = arena_calloc(p->arena, n, sizeof(*v));
  size_t count = 0;
  for (size_t i = 0; i < n; i++)
    if (type->symbols->variants[i].tag != ok->tag)
      v[count++] = type->symbols->variants[i];
  return sema_symbol_set(p, v, count);
}
int sema_assignable(const SemaType *actual, const SemaType *wanted) {
  if (actual == wanted)
    return 1;
  if (!actual || !wanted || actual->kind != SEMA_SYM ||
      wanted->kind != SEMA_SYM)
    return 0;
  for (size_t i = 0; i < actual->symbols->count; i++) {
    const SemaVariant *a = &actual->symbols->variants[i];
    const SemaVariant *b = sema_variant(wanted, a->tag);
    if (!b || a->payload != b->payload)
      return 0;
  }
  return 1;
}
int sema_equatable(const SemaType *type) {
  if (!type || type->kind <= SEMA_ERROR)
    return 0;
  if (type->kind == SEMA_STRUCT)
    return type->structure->equatable;
  if (type->kind == SEMA_SYM)
    return type->symbols->equatable;
  return 1;
}
static int set_equatable(const SemaSymSet *set) {
  for (size_t i = 0; i < set->count; i++)
    if (set->variants[i].payload && !sema_equatable(set->variants[i].payload))
      return 0;
  return 1;
}
void sema_update_equatable(SemaProgram *p) {
  // Bottom-up and nonrecursive, including struct -> symbol -> struct cycles.
  for (size_t i = 0; i < p->struct_count; i++)
    p->structs[i].equatable = 0;
  for (SemaSymSet *s = p->symbol_sets; s; s = s->next)
    s->equatable = 0;
  int changed;
  do {
    changed = 0;
    for (SemaSymSet *s = p->symbol_sets; s; s = s->next)
      if (!s->equatable && set_equatable(s)) {
        s->equatable = 1;
        changed = 1;
      }
    for (size_t i = 0; i < p->struct_count; i++) {
      SemaStruct *s = &p->structs[i];
      if (s->equatable)
        continue;
      int ready = 1;
      for (size_t j = 0; j < s->count; j++)
        if (!sema_equatable(s->fields[j].type))
          ready = 0;
      if (ready) {
        s->equatable = 1;
        changed = 1;
      }
    }
  } while (changed);
}
static int compare_variants(const void *a, const void *b) {
  size_t x = ((const SemaVariant *)a)->tag->id;
  size_t y = ((const SemaVariant *)b)->tag->id;
  return (x > y) - (x < y);
}
const SemaType *sema_symbol_set(SemaProgram *p, const SemaVariant *variants,
                                size_t count) {
  if (!count)
    return NULL;
  const SemaVariant *sorted = variants;
  int ordered = 1;
  for (size_t i = 1; i < count; i++)
    if (variants[i - 1].tag->id >= variants[i].tag->id)
      ordered = 0;
  size_t n = count;
  if (!ordered) {
    SemaVariant *copy = arena_calloc(p->arena, count, sizeof(*copy));
    memcpy(copy, variants, count * sizeof(*copy));
    qsort(copy, count, sizeof(*copy), compare_variants);
    n = 0;
    for (size_t i = 0; i < count; i++) {
      if (n && copy[n - 1].tag == copy[i].tag) {
        if (copy[n - 1].payload != copy[i].payload)
          return NULL;
      } else
        copy[n++] = copy[i];
    }
    sorted = copy;
  }
  for (SemaSymSet *s = p->symbol_sets; s; s = s->next) {
    if (s->count != n)
      continue;
    size_t i = 0;
    while (i < n && s->variants[i].tag == sorted[i].tag &&
           s->variants[i].payload == sorted[i].payload)
      i++;
    if (i == n)
      return &s->type;
  }
  SemaSymSet *s = arena_calloc(p->arena, 1, sizeof(*s));
  s->type = (SemaType){.kind = SEMA_SYM, .name = "Sym", .symbols = s};
  s->variants = arena_calloc(p->arena, n, sizeof(*s->variants));
  memcpy(s->variants, sorted, n * sizeof(*s->variants));
  s->count = n;
  s->id = p->symbol_set_count++;
  s->equatable = set_equatable(s);
  if (p->last_symbol_set)
    p->last_symbol_set->next = s;
  else
    p->symbol_sets = s;
  p->last_symbol_set = s;
  return &s->type;
}
const SemaType *sema_symbol_union(SemaProgram *p, const SemaType *a,
                                  const SemaType *b) {
  if (a == b)
    return a;
  if (a->kind != SEMA_SYM || b->kind != SEMA_SYM)
    return NULL;
  if (sema_assignable(a, b))
    return b;
  if (sema_assignable(b, a))
    return a;
  size_t n = a->symbols->count, m = b->symbols->count;
  // Check the summed count; arena_calloc checks multiplication by element size.
  if (m > SIZE_MAX - n)
    return NULL;
  SemaVariant *variants = arena_calloc(p->arena, n + m, sizeof(*variants));
  memcpy(variants, a->symbols->variants, n * sizeof(*variants));
  memcpy(variants + n, b->symbols->variants, m * sizeof(*variants));
  return sema_symbol_set(p, variants, n + m);
}
static void error(Diagnostics *errors, Span span, const char *message) {
  if (errors)
    diagnostics_error(errors, span, message);
}
void sema_symbol_coverage(SemaProgram *p, SemaExpr *e, Diagnostics *errors) {
  const SemaType *type = e->as.match.input->type;
  const SemaSymSet *input = type->symbols;
  unsigned char *covered = arena_calloc(p->arena, input->count, 1);
  SemaVariant *remaining =
      arena_calloc(p->arena, input->count, sizeof(*remaining));
  int valid = 1;
  for (size_t i = 0; i < e->as.match.count; i++) {
    SemaArm *a = &e->as.match.arms[i];
    a->any = a->kind == PAT_BIND || a->kind == PAT_WILDCARD;
    a->useful = 0;
    a->cases = NULL;
    if (a->binding)
      a->binding->type = &p->types[SEMA_ERROR];
    if (a->invalid) {
      valid = 0;
      continue;
    }
    if (a->kind == PAT_SYMBOL) {
      const SemaVariant *v = sema_variant(type, a->tag);
      if (!v) {
        error(errors, a->span, "Symbol pattern tag is not in the input set.");
        valid = 0;
        continue;
      }
      if ((v->payload != NULL) != a->payload) {
        error(errors, a->span, "Symbol pattern has the wrong payload shape.");
        valid = 0;
        continue;
      }
      if (a->binding)
        a->binding->type = v->payload;
    } else if (a->kind == PAT_SET) {
      if (!a->set || a->set->kind != SEMA_SYM) {
        valid = 0;
        continue;
      }
      // An overlapping tag must have the exact payload type. Disjoint tags in
      // a group are harmless: only its intersection with the input can match.
      for (size_t j = 0; j < a->set->symbols->count; j++) {
        const SemaVariant *v = &a->set->symbols->variants[j];
        const SemaVariant *in = sema_variant(type, v->tag);
        if (in && in->payload != v->payload) {
          error(errors, a->span, "Symbol tag has conflicting payload types.");
          valid = 0;
          goto next_arm;
        }
      }
    } else if (!a->any) {
      error(errors, a->span,
            "Symbol matches require symbol or named-set patterns.");
      valid = 0;
      continue;
    }
    size_t n = 0;
    for (size_t j = 0; j < input->count; j++) {
      const SemaVariant *v = &input->variants[j];
      if (!covered[j] &&
          (a->any || a->tag == v->tag ||
           (a->kind == PAT_SET && sema_variant(a->set, v->tag)))) {
        remaining[n++] = *v;
        covered[j] = 1;
      }
    }
    a->useful = n != 0;
    if (!n) {
      error(errors, a->span, "Unreachable match arm.");
      continue;
    }
    a->cases = sema_symbol_set(p, remaining, n);
    if (a->kind == PAT_BIND) {
      a->set = a->cases;
      a->binding->type = a->set;
    }
  next_arm:;
  }
  if (valid)
    for (size_t i = 0; i < input->count; i++)
      if (!covered[i]) {
        error(errors, e->span, "Match is not exhaustive.");
        break;
      }
}
