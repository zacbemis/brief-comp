#include "parser/internal.h"

Decl *parse_binding(Parser *p, Visibility visibility) {
  Token start = take(p);
  Decl *d = new_decl(p, DECL_BINDING, start.span, visibility);
  d->as.binding.kind = start.type == TOK_LET   ? BIND_LET
                       : start.type == TOK_VAR ? BIND_VAR
                                               : BIND_CONST;
  if (!parser_expect_identifier(p, "Expected a binding name."))
    return NULL;
  Token declared = p->previous;
  size_t mark = p->diagnostics->count;
  d->name = name(p, declared);
  if (match(p, TOK_COLON)) {
    d->as.binding.annotation = parse_type(p);
    if (!d->as.binding.annotation)
      return NULL;
  }
  if (d->as.binding.annotation && d->as.binding.kind != BIND_CONST &&
      (at(p, TOK_NEWLINE) || at(p, TOK_RBRACE) || at(p, TOK_EOF)))
    d->span = through(start.span, d->as.binding.annotation->span);
  else {
    if (!expect(p, TOK_EQUAL, "Expected '=' in binding."))
      return NULL;
    d->as.binding.value = parse_expression(p, 0, 1);
    if (!d->as.binding.value)
      return NULL;
    d->span = through(start.span, d->as.binding.value->span);
    Parser saved = *p;
    newlines(p);
    if (match(p, TOK_ELSE)) {
      if (d->as.binding.kind != BIND_LET) {
        error_at(p, p->previous.span,
                 "Only let bindings can have an else block.");
        return NULL;
      }
      if (!parser_enter(p))
        return NULL;
      d->as.binding.else_body = parse_block(p);
      p->depth--;
      if (!d->as.binding.else_body)
        return NULL;
      d->span = through(start.span, d->as.binding.else_body->span);
    } else
      *p = saved;
  }
  parser_check_name_case(p, declared, mark, TOK_LOWER_NAME,
                         "Binding names must start with a lowercase letter.");
  return d;
}

Stmt *parse_control_statement(Parser *p) {
  Token keyword = take(p);
  Stmt *s = new_stmt(p,
                     keyword.type == TOK_RET     ? STMT_RET
                     : keyword.type == TOK_BREAK ? STMT_BREAK
                                                 : STMT_CONTINUE,
                     keyword.span);
  if (s->kind == STMT_RET && !at(p, TOK_NEWLINE) && !at(p, TOK_RBRACE) &&
      !at(p, TOK_EOF)) {
    s->as.value = parse_expression(p, 0, 1);
    if (!s->as.value)
      return NULL;
    s->span = through(keyword.span, s->as.value->span);
  }
  return s;
}

Expr *parse_block(Parser *p) {
  Token start = peek(p);
  if (!expect(p, TOK_LBRACE, "Expected '{' to start a block."))
    return NULL;
  List items = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    size_t checkpoint = p->position;
    BlockItem item = {.span = peek(p).span};
    int valid = 1;
    if (at(p, TOK_LET) || at(p, TOK_VAR) || at(p, TOK_CONST)) {
      item.kind = BLOCK_ITEM_DECL;
      item.as.decl = parse_binding(p, VISIBILITY_PRIV);
      valid = item.as.decl != NULL;
      if (valid)
        item.span = item.as.decl->span;
    } else if (at(p, TOK_RET) || at(p, TOK_BREAK) || at(p, TOK_CONTINUE)) {
      item.kind = BLOCK_ITEM_STMT;
      item.as.stmt = parse_control_statement(p);
      valid = item.as.stmt != NULL;
      if (valid)
        item.span = item.as.stmt->span;
    } else if (match(p, TOK_ERR)) {
      valid = 0; // Already diagnosed by the lexer.
    } else {
      valid = parse_simple_item(p, &item);
    }
    if (valid)
      PUSH(p, items, item);
    if (!valid || !parser_separator(p, TOK_RBRACE))
      parser_recover(p, checkpoint, 1);
    newlines(p);
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' to close block."))
    return NULL;
  Expr *e = new_expr(p, EXPR_BLOCK, through(start.span, p->previous.span));
  e->as.block.items = items.data;
  e->as.block.count = items.count;
  return parser_checked_expr(p, e);
}
