#include "parser/internal.h"

int parser_enter(Parser *p) {
  if (p->depth >= 128) {
    error(p, "Syntax nesting limit exceeded.");
    return 0;
  }
  p->depth++;
  return 1;
}

static int deepest(const Expr *child, void *context) {
  unsigned *depth = context;
  if (*depth <= child->depth)
    *depth = child->depth + 1;
  return 0;
}

Expr *parser_checked_expr(Parser *p, Expr *e) {
  unsigned depth = 1;
  ast_expr_children(e, deepest, &depth);
  e->depth = depth;
  if (depth > 128) {
    error_at(p, e->span, "Syntax nesting limit exceeded.");
    return NULL;
  }
  return e;
}

Type *parser_checked_type(Parser *p, Type *t) {
  unsigned depth = 1;
#define CHILD(child)                                                           \
  do {                                                                         \
    const Type *v = (child);                                                   \
    if (v && depth <= v->depth)                                                \
      depth = v->depth + 1;                                                    \
  } while (0)
  switch (t->kind) {
  case TYPE_APPLY:
    CHILD(t->as.apply.base);
    for (size_t i = 0; i < t->as.apply.count; i++)
      CHILD(t->as.apply.args[i]);
    break;
  case TYPE_UNION:
    for (size_t i = 0; i < t->as.union_type.count; i++) {
      UnionEntry entry = t->as.union_type.items[i];
      CHILD(entry.type);
      for (size_t j = 0; j < entry.count; j++)
        CHILD(entry.payloads[j]);
    }
    break;
  default:
    break;
  }
#undef CHILD
  t->depth = depth;
  if (depth > 128) {
    error_at(p, t->span, "Syntax nesting limit exceeded.");
    return NULL;
  }
  return t;
}
