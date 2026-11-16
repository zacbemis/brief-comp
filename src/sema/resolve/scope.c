#include "sema/resolve/internal.h"

#include <string.h>

SemaStruct *resolve_named_structure(Resolver *r, const Expr *name) {
  SemaStruct *s = structure(r->program, name->as.name);
  if (!s) {
    SemaAlias *a = alias(r->program, name->as.name);
    if (a && a->type && a->type->kind == SEMA_STRUCT)
      s = a->type->structure;
  }
  return s;
}
void resolve_handled(SemaExpr *e) {
  if (e->kind == SEMA_EXPR_CALL)
    e->as.call.handled = 1;
  else if (e->kind == SEMA_EXPR_BLOCK && e->as.block.pipeline)
    resolve_handled(e->as.block.items[e->as.block.count - 1]);
}
SemaSymbol *resolve_lookup(SemaProgram *p, const char *name) {
  if (!strcmp(name, "print"))
    return &p->print;
  if (!strcmp(name, "str"))
    return &p->stringify;
  for (size_t i = 0; i < sizeof(p->converters) / sizeof(*p->converters); i++)
    if (!strcmp(name, p->converters[i].name))
      return &p->converters[i];
  SemaSymbol *function = name_map_get(&p->function_names, NULL, name);
  if (function)
    return function;
  // Aliases reuse a primitive's existing converter, not a new function or ABI.
  SemaAlias *a = alias(p, name);
  if (a && a->type) {
    if (sema_number(a->type))
      return &p->converters[a->type->kind - SEMA_SHRT];
    if (a->type->kind == SEMA_STR)
      return &p->stringify;
  }
  return NULL;
}
SemaVar *resolve_local(Resolver *r, const char *name) {
  Local *l = name_map_get(&r->local_names, NULL, name);
  return l ? l->variable : NULL;
}

void resolve_push_local(Resolver *r, SemaVar *v) {
  Local *l = arena_alloc(r->arena, sizeof(*l));
  *l = (Local){v, r->depth, r->locals, NULL};
  l->shadowed = name_map_put(&r->local_names, NULL, v->name, l);
  r->locals = l;
}
void resolve_restore(Resolver *r, Local *saved) {
  for (Local *l = r->locals; l != saved; l = l->next)
    name_map_put(&r->local_names, NULL, l->variable->name, l->shadowed);
  r->locals = saved;
}
SemaVar *resolve_variable(Resolver *r, const char *name, Span span,
                          const SemaType *annotation, int mutable) {
  // A visible binding at this depth can only belong to the current scope.
  Local *existing = name_map_get(&r->local_names, NULL, name);
  if (existing && existing->depth == r->depth) {
    Diagnostic *d =
        diagnostics_error(r->errors, span, "Duplicate binding in this scope.");
    diagnostic_note(r->errors, d, existing->variable->span,
                    "First declared here.");
  }
  SemaFunction *f = r->function;
  SemaVar *v = arena_calloc(r->arena, 1, sizeof(*v));
  *v = (SemaVar){.name = name,
                 .span = span,
                 .id = f->variable_count,
                 .mutable = mutable,
                 .data_mutable = 1,
                 .annotation = annotation,
                 .type = annotation};
  ARENA_GROW(r->arena, f->variables, f->variable_count, f->variable_capacity,
             16);
  f->variables[f->variable_count++] = v;
  resolve_push_local(r, v);
  return v;
}
