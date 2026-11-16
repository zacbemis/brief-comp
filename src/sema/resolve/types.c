#include "sema/resolve/internal.h"

#include <string.h>

SemaTag *resolve_tag(Resolver *r, const char *name, SymbolKind kind,
                     Span span) {
  return sema_declare_tag(r->program, r->program->module, name,
                          kind == SYMBOL_ERROR ? SEMA_TAG_ERROR
                                               : SEMA_TAG_NEUTRAL,
                          span, r->errors);
}
int resolve_none_payload(const char *name, SymbolKind kind, size_t count) {
  return kind == SYMBOL_NEUTRAL && count && !strcmp(name, "none");
}
int resolve_reject_none_payload(Resolver *r, const char *name, SymbolKind kind,
                                size_t count, Span span) {
  if (!resolve_none_payload(name, kind, count))
    return 0;
  // Dropping payload syntax could drop effects or change a pattern's meaning.
  diagnostics_error(r->errors, span, "':none' cannot carry a payload.");
  return 1;
}
static const SemaType *resolve_type_ok(Resolver *r, const Type *syntax,
                                       const SemaVariant **written_ok) {
  if (!syntax)
    return &r->program->types[SEMA_UNKNOWN];
  if (syntax->kind == TYPE_UNIT)
    return &r->program->types[SEMA_UNIT];
  if (syntax->kind == TYPE_NAME) {
    for (size_t i = SEMA_BOOL; i < SEMA_STRUCT; i++)
      if (!strcmp(syntax->as.name, r->program->types[i].name))
        return &r->program->types[i];
    SemaStruct *s = structure(r->program, syntax->as.name);
    if (s)
      return &s->type;
    SemaAlias *a = alias(r->program, syntax->as.name);
    if (a)
      return a->type ? a->type : &r->program->types[SEMA_ERROR];
  }
  if (syntax->kind == TYPE_INFER) {
    diagnostics_error(
        r->errors, syntax->span,
        "Partial symbol sets are only allowed as function return types.");
    return &r->program->types[SEMA_ERROR];
  }
  if (syntax->kind == TYPE_UNION) {
    size_t entries = syntax->as.union_type.count, count = 0;
    const SemaVariant **parts = arena_calloc(r->arena, entries, sizeof(*parts));
    size_t *counts = arena_calloc(r->arena, entries, sizeof(*counts));
    NameMap schemas;
    name_map_init(&schemas, r->arena);
    int valid = 1;
    for (size_t i = 0; i < syntax->as.union_type.count; i++) {
      UnionEntry entry = syntax->as.union_type.items[i];
      if (entry.tag) {
        if (entry.module) {
          diagnostics_error(
              r->errors, entry.span,
              "Module-qualified symbols are not implemented yet.");
          valid = 0;
          continue;
        }
        if (resolve_reject_none_payload(r, entry.tag, entry.kind, entry.count,
                                        entry.tag_span.file ? entry.tag_span
                                                            : entry.span)) {
          valid = 0;
          continue;
        }
        if (entry.count > 1 && !strcmp(entry.tag, "ok")) {
          diagnostics_error(r->errors, entry.span,
                            "Multiple ':ok' payloads are not implemented yet.");
          valid = 0;
          continue;
        }
        SemaTag *name =
            resolve_tag(r, entry.tag, entry.kind,
                        entry.tag_span.file ? entry.tag_span : entry.span);
        const SemaType **payloads =
            arena_calloc(r->arena, entry.count, sizeof(*payloads));
        int complete = name != NULL;
        for (size_t j = 0; j < entry.count; j++) {
          payloads[j] = resolve_type(r, entry.payloads[j]);
          if (payloads[j]->kind == SEMA_ERROR)
            complete = 0;
        }
        if (!complete) {
          valid = 0;
          continue;
        }
        SemaVariant *variant = arena_alloc(r->arena, sizeof(*variant));
        *variant = (SemaVariant){name, payloads, entry.count};
        parts[i] = variant;
        counts[i] = 1;
      } else if (entry.type->kind == TYPE_INFER) {
        diagnostics_error(
            r->errors, entry.span,
            "Partial symbol sets are only allowed as function return types.");
        valid = 0;
        continue;
      } else {
        const SemaType *part = resolve_type(r, entry.type);
        if (part->kind != SEMA_SYM) {
          if (part->kind != SEMA_ERROR)
            diagnostics_error(r->errors, entry.span,
                              "A symbol-set inclusion requires a symbol set.");
          valid = 0;
          continue;
        }
        parts[i] = part->symbols->variants;
        counts[i] = part->symbols->count;
      }
      for (size_t j = 0; j < counts[i]; j++) {
        const SemaVariant *v = &parts[i][j];
        const SemaVariant *prior =
            name_map_get(&schemas, v->tag->module, v->tag->name);
        if (prior && !sema_payloads_equal(prior, v)) {
          diagnostics_error(r->errors, entry.span,
                            "Symbol tag has conflicting payload types.");
          return &r->program->types[SEMA_ERROR];
        }
        name_map_put(&schemas, v->tag->module, v->tag->name, (void *)v);
      }
      count += counts[i];
    }
    if (!valid || !count)
      return &r->program->types[SEMA_ERROR];
    // Normalize only the complete union, never each written tag in isolation.
    SemaVariant *variants = arena_calloc(r->arena, count, sizeof(*variants));
    size_t next = 0;
    for (size_t i = 0; i < entries; i++)
      for (size_t j = 0; j < counts[i]; j++) {
        variants[next++] = parts[i][j];
        if (written_ok && parts[i][j].tag == r->program->ok)
          *written_ok = &parts[i][j];
      }
    if (sema_ambiguous_endings(variants, count)) {
      diagnostics_error(r->errors, syntax->span,
                        "A union cannot contain both payload-less ':ok' and "
                        "':none'.");
      return &r->program->types[SEMA_ERROR];
    }
    const SemaType *result = sema_symbol_set(r->program, variants, count);
    if (!result)
      diagnostics_error(r->errors, syntax->span,
                        "Symbol tag has conflicting payload types.");
    return result ? result : &r->program->types[SEMA_ERROR];
  }
  diagnostics_error(r->errors, syntax->span,
                    "This type is unknown or not implemented yet.");
  return &r->program->types[SEMA_ERROR];
}
const SemaType *resolve_type(Resolver *r, const Type *syntax) {
  return resolve_type_ok(r, syntax, NULL);
}
typedef struct AliasEdge AliasEdge;
struct AliasEdge {
  SemaAlias *target;
  Span span;
  AliasEdge *next;
};
static void alias_dependencies(Resolver *r, const Type *syntax,
                               AliasEdge **head) {
  // Syntax depth is parser-limited. Alias chains themselves use no C recursion.
  if (syntax->kind == TYPE_NAME) {
    SemaAlias *a = alias(r->program, syntax->as.name);
    if (a) {
      AliasEdge *edge = arena_alloc(r->arena, sizeof(*edge));
      *edge = (AliasEdge){a, syntax->span, *head};
      *head = edge;
    }
  } else if (syntax->kind == TYPE_UNION) {
    for (size_t i = 0; i < syntax->as.union_type.count; i++) {
      UnionEntry entry = syntax->as.union_type.items[i];
      if (entry.module ||
          (entry.tag && !strcmp(entry.tag, "ok") && entry.count > 1) ||
          (entry.tag &&
           resolve_none_payload(entry.tag, entry.kind, entry.count)))
        continue; // Unavailable/invalid payloads create no local alias edges.
      if (entry.type)
        alias_dependencies(r, entry.type, head);
      for (size_t j = 0; j < entry.count; j++)
        alias_dependencies(r, entry.payloads[j], head);
    }
  }
}
void resolve_aliases(Resolver *r) {
  SemaProgram *p = r->program;
  AliasEdge **edges = arena_calloc(r->arena, p->alias_count, sizeof(*edges));
  unsigned char *state = arena_calloc(r->arena, p->alias_count, 1);
  typedef struct {
    SemaAlias *alias;
    AliasEdge *edge;
  } Frame;
  Frame *stack = arena_calloc(r->arena, p->alias_count, sizeof(*stack));
  for (size_t i = 0; i < p->alias_count; i++)
    alias_dependencies(r, p->aliases[i].syntax->as.alias, &edges[i]);
  for (size_t i = 0; i < p->alias_count; i++) {
    if (state[i])
      continue;
    size_t top = 0;
    state[i] = 1;
    stack[top++] = (Frame){&p->aliases[i], edges[i]};
    while (top) {
      Frame *frame = &stack[top - 1];
      if (frame->edge) {
        AliasEdge *edge = frame->edge;
        frame->edge = edge->next;
        size_t id = (size_t)(edge->target - p->aliases);
        if (!state[id]) {
          state[id] = 1;
          stack[top++] = (Frame){edge->target, edges[id]};
        } else if (state[id] == 1) {
          diagnostics_error(
              r->errors, edge->span,
              "Recursive named types require a struct indirection.");
          edge->target->type = &p->types[SEMA_ERROR];
        }
      } else {
        SemaAlias *a = frame->alias;
        if (!a->type)
          a->type = resolve_type(r, a->syntax->as.alias);
        state[a - p->aliases] = 2;
        top--;
      }
    }
  }
}
const SemaType *resolve_return_type(Resolver *r, const Type *syntax,
                                    SemaFunction *f) {
  if (syntax && syntax->kind == TYPE_INFER) {
    f->partial = 1;
    diagnostics_error(r->errors, syntax->span,
                      "A partial return type must declare :ok.");
    return &r->program->types[SEMA_ERROR];
  }
  if (!syntax || syntax->kind != TYPE_UNION)
    return resolve_type(r, syntax);
  Type copy = *syntax;
  copy.as.union_type.items =
      arena_calloc(r->arena, syntax->as.union_type.count, sizeof(UnionEntry));
  copy.as.union_type.count = 0;
  for (size_t i = 0; i < syntax->as.union_type.count; i++) {
    UnionEntry e = syntax->as.union_type.items[i];
    if (!e.tag && e.type->kind == TYPE_INFER) {
      if (f->partial)
        diagnostics_error(r->errors, e.span,
                          "Duplicate partial return marker.");
      f->partial = 1;
    } else
      copy.as.union_type.items[copy.as.union_type.count++] = e;
  }
  if (f->partial && !copy.as.union_type.count) {
    diagnostics_error(r->errors, syntax->span,
                      "A partial return type must declare :ok.");
    return &r->program->types[SEMA_ERROR];
  }
  const SemaType *result = resolve_type_ok(r, &copy, &f->written_ok);
  if (f->partial && result->kind != SEMA_ERROR && !f->written_ok)
    diagnostics_error(r->errors, syntax->span,
                      "A partial return type must declare :ok.");
  return result;
}
