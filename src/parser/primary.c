#include "parser/internal.h"

static Expr *conditional(Parser *p) {
  Token start = take(p);
  Expr *condition = parse_expression(p, 0, 0);
  if (!condition)
    return NULL;
  Expr *yes = parse_block(p);
  if (!yes)
    return NULL;
  Expr *no = NULL;
  // A newline belongs to the surrounding block unless an else follows it.
  Parser saved = *p;
  newlines(p);
  if (match(p, TOK_ELSE)) {
    if (at(p, TOK_IF)) {
      if (!parser_enter(p))
        return NULL;
      no = conditional(p);
      p->depth--;
    } else {
      no = parse_block(p);
    }
    if (!no)
      return NULL;
  } else {
    *p = saved;
  }
  Expr *e =
      new_expr(p, EXPR_IF, through(start.span, no ? no->span : yes->span));
  e->as.conditional.condition = condition;
  e->as.conditional.then_branch = yes;
  e->as.conditional.else_branch = no;
  return parser_checked_expr(p, e);
}

static Expr *while_loop(Parser *p, Token keyword) {
  Expr *e = new_expr(p, EXPR_WHILE, keyword.span);
  e->as.while_loop.condition = parse_expression(p, 0, 0);
  if (!e->as.while_loop.condition)
    return NULL;
  e->as.while_loop.body = parse_block(p);
  if (!e->as.while_loop.body)
    return NULL;
  e->span = through(keyword.span, e->as.while_loop.body->span);
  return e;
}

static Expr *for_loop(Parser *p, Token keyword) {
  Expr *e = new_expr(p, EXPR_FOR, keyword.span);
  if (!parser_expect_identifier(p, "Expected a loop variable."))
    return NULL;
  Token declared = p->previous;
  size_t mark = p->diagnostics->count;
  e->as.for_loop.name = name(p, declared);
  if (match(p, TOK_COLON)) {
    e->as.for_loop.annotation = parse_type(p);
    if (!e->as.for_loop.annotation)
      return NULL;
  }
  if (!expect(p, TOK_IN, "Expected 'in' after loop variable."))
    return NULL;
  e->as.for_loop.iterable = parse_expression(p, 0, 0);
  if (!e->as.for_loop.iterable)
    return NULL;
  e->as.for_loop.body = parse_block(p);
  if (!e->as.for_loop.body)
    return NULL;
  e->span = through(keyword.span, e->as.for_loop.body->span);
  parser_check_name_case(p, declared, mark, TOK_LOWER_NAME,
                         "Loop variables must start with a lowercase letter.");
  return e;
}

static Expr *symbol(Parser *p, Token start) {
  SymbolHead head;
  if (!parser_symbol_head(p, &head, "Expected a symbol tag after ':'."))
    return NULL;
  Expr *e = new_expr(p, EXPR_SYMBOL, start.span);
  e->as.symbol.module = head.module;
  e->as.symbol.module_span = head.module_span;
  e->as.symbol.kind = head.kind;
  e->as.symbol.tag = head.tag;
  e->as.symbol.tag_span = head.tag_span;
  if (match(p, TOK_LBRACKET)) {
    List payloads = {0};
    if (!at(p, TOK_RBRACKET))
      do {
        Expr *payload = parse_expression(p, 0, 1);
        if (!payload ||
            parser_typed_binding_elsewhere(p, payload->kind == EXPR_NAME))
          return NULL;
        PUSH(p, payloads, payload);
        if (!match(p, TOK_COMMA) || at(p, TOK_RBRACKET))
          break;
      } while (!at(p, TOK_EOF));
    if (!expect(p, TOK_RBRACKET, "Expected ']' after symbol payloads."))
      return NULL;
    e->as.symbol.payloads = payloads.data;
    e->as.symbol.count = payloads.count;
  }
  parser_check_symbol_head(p, &head);
  e->span = through(start.span, p->previous.span);
  return e;
}

static Expr *grouping(Parser *p, Token open) {
  if (match(p, TOK_RPAREN))
    return new_expr(p, EXPR_UNIT, through(open.span, p->previous.span));
  Expr *e = parse_expression(p, 0, 1);
  if (!e || !expect(p, TOK_RPAREN, "Expected ')' after expression."))
    return NULL;
  // Keep grouping in the span, but not as an otherwise redundant AST node.
  e->span = through(open.span, p->previous.span);
  return e;
}

static Expr *unary(Parser *p, Token op, int allow_struct) {
  Expr *value = parse_expression(p, 130, allow_struct);
  if (!value)
    return NULL;
  Expr *e = new_expr(p, EXPR_UNARY, through(op.span, value->span));
  e->as.unary.op = op.type == TOK_MINUS   ? UNARY_NEG
                   : op.type == TOK_TILDE ? UNARY_BIT_NOT
                                          : UNARY_NOT;
  e->as.unary.value = value;
  return e;
}

static int literal_kind(TokenType type, ExprKind *kind) {
  switch (type) {
  case TOK_INT_LIT:
    *kind = EXPR_INT;
    return 1;
  case TOK_FLOAT_LIT:
    *kind = EXPR_FLOAT;
    return 1;
  case TOK_STR_LIT:
    *kind = EXPR_STRING;
    return 1;
  case TOK_CHAR_LIT:
    *kind = EXPR_CHAR;
    return 1;
  case TOK_BOOL_LIT:
    *kind = EXPR_BOOL;
    return 1;
  default:
    return 0;
  }
}

// Rejects forms from other languages before trying a real expression.
static int misplaced_form(Parser *p, TokenType type) {
  switch (type) {
  case TOK_LBRACKET:
    error(
        p,
        "Collection literals use 'Arr[Type] { ... }' or 'Vec[Type] { ... }'.");
    return 1;
  case TOK_IMPORT:
    error(p, "Imports are only allowed at the top level.");
    return 1;
  case TOK_PLUS_PLUS:
  case TOK_MINUS_MINUS:
    error(p, "Increment and decrement must be postfix standalone statements "
             "(x++ or x--).");
    return 1;
  case TOK_LBRACE:
    error(p, "Bare blocks are not allowed as expressions or statements.");
    return 1;
  default:
    return 0;
  }
}

Expr *parse_primary(Parser *p, int allow_struct) {
  Token token = peek(p);
  if (misplaced_form(p, token.type))
    return NULL;
  if (match(p, TOK_SELF)) {
    Expr *e = new_expr(p, EXPR_SELF, token.span);
    e->name_span = token.span;
    return e;
  }
  if (parser_type_name_token(token.type) && !parser_at_symbol(p)) {
    take(p);
    Expr *e = new_expr(p, EXPR_NAME, token.span);
    e->name_span = token.span;
    e->as.name = name(p, token);
    return e;
  }
  ExprKind kind;
  if (literal_kind(token.type, &kind)) {
    take(p);
    Expr *e = new_expr(p, kind, token.span);
    e->as.literal = token;
    return e;
  }
  if (parser_at_symbol(p))
    return symbol(p, token);
  if (at(p, TOK_IF))
    return conditional(p);
  if (match(p, TOK_LPAREN))
    return grouping(p, token);
  if (match(p, TOK_WHILE))
    return while_loop(p, token);
  if (match(p, TOK_FOR))
    return for_loop(p, token);
  if (match(p, TOK_MINUS) || match(p, TOK_NOT) || match(p, TOK_TILDE))
    return unary(p, token, allow_struct);
  error(p, "Expected an expression.");
  return NULL;
}
