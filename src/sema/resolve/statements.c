#include "sema/resolve/internal.h"

SemaExpr *resolve_stmt(Resolver *r, const Stmt *s) {
  if (s->kind == STMT_EXPR)
    return resolve_expr(r, s->as.value);
  if (s->kind == STMT_INC || s->kind == STMT_DEC) {
    if (s->as.value->kind == EXPR_INDEX)
      return unsupported(r, s->span, "Index updates are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_UPDATE, s->span);
    if (s->as.value->kind == EXPR_FIELD)
      e->as.binding.target = resolve_expr(r, s->as.value);
    else {
      e->as.binding.variable = resolve_local(r, s->as.value->as.name);
      if (!e->as.binding.variable)
        diagnostics_error(r->errors, s->as.value->span, "Unknown binding.");
    }
    e->as.binding.op = s->kind == STMT_INC ? ASSIGN_ADD : ASSIGN_SUB;
    return e;
  }
  if (s->kind == STMT_ASSIGN) {
    if (s->as.assign.target->kind == EXPR_INDEX)
      return unsupported(r, s->span,
                         "Index assignments are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_ASSIGN, s->span);
    if (s->as.assign.target->kind == EXPR_FIELD)
      e->as.binding.target = resolve_expr(r, s->as.assign.target);
    else {
      e->as.binding.variable = resolve_local(r, s->as.assign.target->as.name);
      if (!e->as.binding.variable)
        diagnostics_error(r->errors, s->as.assign.target->span,
                          "Unknown binding.");
    }
    e->as.binding.value = resolve_expr(r, s->as.assign.value);
    e->as.binding.op = s->as.assign.op;
    return e;
  }
  SemaExprKind kind = s->kind == STMT_RET     ? SEMA_EXPR_RETURN
                      : s->kind == STMT_BREAK ? SEMA_EXPR_BREAK
                                              : SEMA_EXPR_CONTINUE;
  SemaExpr *e = node(r, kind, s->span);
  if (s->kind == STMT_RET && s->as.value)
    e->as.value = resolve_expr(r, s->as.value);
  if (s->kind != STMT_RET && !r->loops)
    diagnostics_error(r->errors, s->span,
                      "break and continue are only allowed inside loops.");
  return e;
}

// Let-else is a two-arm match: the success arm yields the hidden $else
// projection; the else body handles everything else.
static SemaExpr *let_else(Resolver *r, const Decl *d, SemaExpr *input) {
  resolve_handled(input);
  Span span = input->span;
  span.end = d->as.binding.else_body->span.end;
  SemaExpr *match = node(r, SEMA_EXPR_MATCH, span);
  match->as.match.input = input;
  match->as.match.count = 2;
  match->as.match.arms = arena_calloc(r->arena, 2, sizeof(SemaArm));
  Local *saved = r->locals;
  r->depth++;
  SemaVar *remaining = resolve_variable(r, "$else", input->span,
                                        &r->program->types[SEMA_UNKNOWN], 0);
  match->as.match.remaining = remaining;
  SemaArm *success = &match->as.match.arms[0];
  success->span = input->span;
  success->payload_bindings = arena_calloc(r->arena, 1, sizeof(SemaVar *));
  success->body = node(r, SEMA_EXPR_VAR, input->span);
  success->body->as.variable = remaining;
  SemaArm *failure = &match->as.match.arms[1];
  failure->kind = PAT_WILDCARD;
  failure->span = d->as.binding.else_body->span;
  failure->body = resolve_expr(r, d->as.binding.else_body);
  resolve_restore(r, saved);
  r->depth--;
  return match;
}

static SemaExpr *binding(Resolver *r, const Decl *d) {
  SemaExpr *e = node(r, SEMA_EXPR_BIND, d->span);
  e->as.binding.declaration = 1;
  if (d->as.binding.value)
    e->as.binding.value = resolve_expr(r, d->as.binding.value);
  if (d->as.binding.else_body)
    e->as.binding.value = let_else(r, d, e->as.binding.value);
  e->as.binding.variable = resolve_variable(
      r, d->name, d->span, resolve_type(r, d->as.binding.annotation),
      d->as.binding.kind == BIND_VAR);
  e->as.binding.variable->deferred = !d->as.binding.value;
  return e;
}

static SemaExpr *item(Resolver *r, BlockItem item) {
  if (item.kind == BLOCK_ITEM_EXPR)
    return resolve_expr(r, item.as.expr);
  if (item.kind == BLOCK_ITEM_STMT)
    return resolve_stmt(r, item.as.stmt);
  if (item.as.decl->kind != DECL_BINDING)
    return unsupported(
        r, item.span,
        "Local declarations other than bindings are not implemented yet.");
  if (item.as.decl->as.binding.kind == BIND_CONST)
    return unsupported(r, item.span, "const bindings are not implemented yet.");
  return binding(r, item.as.decl);
}

SemaExpr *resolve_block(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_BLOCK, s->span);
  e->as.block.count = s->as.block.count;
  e->as.block.items =
      arena_calloc(r->arena, e->as.block.count, sizeof(SemaExpr *));
  Local *saved = r->locals;
  r->depth++;
  for (size_t i = 0; i < e->as.block.count; i++)
    e->as.block.items[i] = item(r, s->as.block.items[i]);
  resolve_restore(r, saved);
  r->depth--;
  return e;
}
