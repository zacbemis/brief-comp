#ifndef BRIEF_SEMA_RESOLVE_INTERNAL_H
#define BRIEF_SEMA_RESOLVE_INTERNAL_H

#include "sema/sema.h"

typedef struct Local Local;
struct Local {
  SemaVar *variable;
  unsigned depth;
  Local *next;
  Local *shadowed; // The visible binding with this name it hides, if any.
};
typedef struct {
  SemaSymbol *first, *last;
} MethodChain;
typedef struct {
  Arena *arena;
  SemaProgram *program;
  SemaFunction *function;
  SemaStruct *owner;
  Diagnostics *errors;
  Local *locals;
  NameMap local_names; // Each visible name's innermost binding.
  unsigned depth, loops;
  int pipeline_call;
  const Expr *call_source;
} Resolver;

static inline SemaStruct *structure(SemaProgram *p, const char *name) {
  return name_map_get(&p->struct_names, NULL, name);
}
static inline SemaAlias *alias(SemaProgram *p, const char *name) {
  return name_map_get(&p->alias_names, NULL, name);
}
static inline SemaExpr *node(Resolver *r, SemaExprKind kind, Span span) {
  SemaExpr *e = arena_calloc(r->arena, 1, sizeof(*e));
  e->kind = kind;
  e->span = span;
  return e;
}
static inline SemaExpr *unsupported(Resolver *r, Span span,
                                    const char *message) {
  diagnostics_error(r->errors, span, message);
  return node(r, SEMA_EXPR_ERROR, span);
}

// scope.c: names, lexical scopes and variables.

SemaStruct *resolve_named_structure(Resolver *r, const Expr *name);
// Functions, builtins and alias conversions visible by plain name.
SemaSymbol *resolve_lookup(SemaProgram *p, const char *name);
SemaVar *resolve_local(Resolver *r, const char *name);
void resolve_push_local(Resolver *r, SemaVar *v);
// Leaves the scopes entered since saved, making hidden bindings visible again.
void resolve_restore(Resolver *r, Local *saved);
SemaVar *resolve_variable(Resolver *r, const char *name, Span span,
                          const SemaType *annotation, int mutable);
// Marks a call (or a pipeline's final call) as an explicitly handled outcome.
void resolve_handled(SemaExpr *e);

// types.c: written types, aliases and symbol tags.

SemaTag *resolve_tag(Resolver *r, const char *name, SymbolKind kind, Span span);
int resolve_none_payload(const char *name, SymbolKind kind, size_t count);
int resolve_reject_none_payload(Resolver *r, const char *name, SymbolKind kind,
                                size_t count, Span span);
const SemaType *resolve_type(Resolver *r, const Type *syntax);
void resolve_aliases(Resolver *r);
const SemaType *resolve_return_type(Resolver *r, const Type *syntax,
                                    SemaFunction *f);

// Expressions.

SemaExpr *resolve_expr(Resolver *r, const Expr *syntax);
SemaExpr *resolve_call(Resolver *r, const Expr *syntax);
SemaExpr *resolve_pipe(Resolver *r, const Expr *syntax);
// Fills a pipeline match arm inside its own scope.
void resolve_arm(Resolver *r, SemaArm *arm, MatchArm source);
SemaExpr *resolve_stmt(Resolver *r, const Stmt *s);
SemaExpr *resolve_block(Resolver *r, const Expr *syntax);

#endif
