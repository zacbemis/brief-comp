#include "ast/ast.h"

typedef struct {
  int (*visit)(const Expr *child, void *context);
  void *context;
} Walk;

static int child(const Walk *w, const Expr *e) {
  return e && w->visit(e, w->context);
}

static int block_item(const Walk *w, BlockItem item) {
  if (item.kind == BLOCK_ITEM_EXPR)
    return child(w, item.as.expr);
  if (item.kind == BLOCK_ITEM_DECL)
    return child(w, item.as.decl->as.binding.value) ||
           child(w, item.as.decl->as.binding.else_body);
  const Stmt *s = item.as.stmt;
  if (s->kind == STMT_ASSIGN)
    return child(w, s->as.assign.target) || child(w, s->as.assign.value);
  if (s->kind == STMT_BREAK || s->kind == STMT_CONTINUE)
    return 0;
  return child(w, s->as.value);
}

int ast_expr_children(const Expr *e,
                      int (*visit)(const Expr *child, void *context),
                      void *context) {
  Walk w = {visit, context};
  switch (e->kind) {
  case EXPR_SYMBOL:
    for (size_t i = 0; i < e->as.symbol.count; i++)
      if (child(&w, e->as.symbol.payloads[i]))
        return 1;
    return 0;
  case EXPR_UNARY:
    return child(&w, e->as.unary.value);
  case EXPR_BINARY:
    return child(&w, e->as.binary.left) || child(&w, e->as.binary.right);
  case EXPR_CALL:
    if (child(&w, e->as.call.callee))
      return 1;
    for (size_t i = 0; i < e->as.call.count; i++)
      if (child(&w, e->as.call.args[i]))
        return 1;
    return 0;
  case EXPR_FIELD:
    return child(&w, e->as.field.object);
  case EXPR_INDEX:
    return child(&w, e->as.index.object) || child(&w, e->as.index.index);
  case EXPR_COLLECTION:
    if (child(&w, e->as.collection.constructor))
      return 1;
    for (size_t i = 0; i < e->as.collection.count; i++)
      if (child(&w, e->as.collection.items[i]))
        return 1;
    return 0;
  case EXPR_STRUCT_LIT:
    if (child(&w, e->as.structure.type))
      return 1;
    for (size_t i = 0; i < e->as.structure.count; i++)
      if (child(&w, e->as.structure.fields[i].value))
        return 1;
    return 0;
  case EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++)
      if (block_item(&w, e->as.block.items[i]))
        return 1;
    return 0;
  case EXPR_IF:
    return child(&w, e->as.conditional.condition) ||
           child(&w, e->as.conditional.then_branch) ||
           child(&w, e->as.conditional.else_branch);
  case EXPR_FOR:
    return child(&w, e->as.for_loop.iterable) || child(&w, e->as.for_loop.body);
  case EXPR_WHILE:
    return child(&w, e->as.while_loop.condition) ||
           child(&w, e->as.while_loop.body);
  case EXPR_MATCH:
    for (size_t i = 0; i < e->as.match.count; i++)
      if (child(&w, e->as.match.arms[i].value))
        return 1;
    return 0;
  case EXPR_TRY:
  case EXPR_PANIC:
    return child(&w, e->as.postfix);
  case EXPR_PIPE:
    return child(&w, e->as.pipe.value) || child(&w, e->as.pipe.stage);
  default:
    return 0;
  }
}
