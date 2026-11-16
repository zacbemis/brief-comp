#include "parser/internal.h"

int parser_enter(Parser *p) {
  if (p->depth >= 128) {
    error(p, "Syntax nesting limit exceeded.");
    return 0;
  }
  p->depth++;
  return 1;
}

Expr *parser_checked_expr(Parser *p, Expr *e) {
  unsigned depth = 1;
#define CHILD(child)                                                           \
  do {                                                                         \
    const Expr *v = (child);                                                   \
    if (v && depth <= v->depth)                                                \
      depth = v->depth + 1;                                                    \
  } while (0)
  switch (e->kind) {
  case EXPR_SYMBOL:
    for (size_t i = 0; i < e->as.symbol.count; i++)
      CHILD(e->as.symbol.payloads[i]);
    break;
  case EXPR_UNARY:
    CHILD(e->as.unary.value);
    break;
  case EXPR_BINARY:
    CHILD(e->as.binary.left);
    CHILD(e->as.binary.right);
    break;
  case EXPR_CALL:
    CHILD(e->as.call.callee);
    for (size_t i = 0; i < e->as.call.count; i++)
      CHILD(e->as.call.args[i]);
    break;
  case EXPR_FIELD:
    CHILD(e->as.field.object);
    break;
  case EXPR_INDEX:
    CHILD(e->as.index.object);
    CHILD(e->as.index.index);
    break;
  case EXPR_COLLECTION:
    CHILD(e->as.collection.constructor);
    for (size_t i = 0; i < e->as.collection.count; i++)
      CHILD(e->as.collection.items[i]);
    break;
  case EXPR_STRUCT_LIT:
    CHILD(e->as.structure.type);
    for (size_t i = 0; i < e->as.structure.count; i++)
      CHILD(e->as.structure.fields[i].value);
    break;
  case EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++) {
      BlockItem item = e->as.block.items[i];
      if (item.kind == BLOCK_ITEM_EXPR) {
        CHILD(item.as.expr);
      } else if (item.kind == BLOCK_ITEM_STMT) {
        if (item.as.stmt->kind == STMT_ASSIGN) {
          CHILD(item.as.stmt->as.assign.target);
          CHILD(item.as.stmt->as.assign.value);
        } else {
          CHILD(item.as.stmt->as.value);
        }
      } else {
        CHILD(item.as.decl->as.binding.value);
        CHILD(item.as.decl->as.binding.else_body);
      }
    }
    break;
  case EXPR_IF:
    CHILD(e->as.conditional.condition);
    CHILD(e->as.conditional.then_branch);
    CHILD(e->as.conditional.else_branch);
    break;
  case EXPR_FOR:
    CHILD(e->as.for_loop.iterable);
    CHILD(e->as.for_loop.body);
    break;
  case EXPR_WHILE:
    CHILD(e->as.while_loop.condition);
    CHILD(e->as.while_loop.body);
    break;
  case EXPR_MATCH:
    for (size_t i = 0; i < e->as.match.count; i++)
      CHILD(e->as.match.arms[i].value);
    break;
  case EXPR_TRY:
  case EXPR_PANIC:
    CHILD(e->as.postfix);
    break;
  case EXPR_PIPE:
    CHILD(e->as.pipe.value);
    CHILD(e->as.pipe.stage);
    break;
  default:
    break;
  }
#undef CHILD
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
