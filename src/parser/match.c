#include "parser/internal.h"

#include <string.h>

static BinaryOp compare_op(TokenType t) {
  switch (t) {
  case TOK_EQUAL_EQUAL:
    return BINARY_EQ;
  case TOK_BANG_EQUAL:
    return BINARY_NE;
  case TOK_LESS_THAN:
    return BINARY_LT;
  case TOK_LESS_THAN_EQUAL:
    return BINARY_LE;
  case TOK_GREATER_THAN:
    return BINARY_GT;
  default:
    return BINARY_GE;
  }
}

static Expr *pattern_value(Parser *p) {
  Token t = peek(p);
  if (t.type == TOK_MINUS) {
    take(p);
    if (!at(p, TOK_INT_LIT) && !at(p, TOK_FLOAT_LIT)) {
      error(p, "Expected a number after '-' in pattern.");
      return NULL;
    }
    Expr *value = parse_primary(p, 0);
    Expr *e = new_expr(p, EXPR_UNARY, through(t.span, value->span));
    e->as.unary.op = UNARY_NEG;
    e->as.unary.value = value;
    return e;
  }
  if (token_is_identifier(t.type)) {
    const char *text = parser_path(p, 0);
    if (!text)
      return NULL;
    Expr *e = new_expr(p, EXPR_NAME, through(t.span, p->previous.span));
    e->as.name = text;
    return e;
  }
  if (t.type >= TOK_INT_LIT && t.type <= TOK_BOOL_LIT)
    return parse_primary(p, 0);
  error(p, "Expected a literal or constant in pattern.");
  return NULL;
}

static Pattern *range_pattern(Parser *p, Token start, Expr *lower) {
  Token range = take(p);
  Expr *upper = pattern_value(p);
  if (!upper)
    return NULL;
  Pattern *v = new_pattern(p, PAT_RANGE, through(start.span, upper->span));
  v->as.range.lower = lower;
  v->as.range.upper = upper;
  v->as.range.inclusive = range.type == TOK_DOT_DOT_EQUAL;
  return v;
}

static Pattern *pattern(Parser *p, int payload);

static Pattern *symbol_pattern(Parser *p, Token start) {
  SymbolHead head;
  if (!parser_symbol_head(p, &head, "Expected a symbol tag in pattern."))
    return NULL;
  Pattern *v = new_pattern(p, PAT_SYMBOL, start.span);
  v->as.symbol.module = head.module;
  v->as.symbol.module_span = head.module_span;
  v->as.symbol.kind = head.kind;
  v->as.symbol.tag = head.tag;
  v->as.symbol.tag_span = head.tag_span;
  if (match(p, TOK_LBRACKET)) {
    List payloads = {0};
    if (!at(p, TOK_RBRACKET))
      do {
        Pattern *payload = pattern(p, 1);
        if (!payload)
          return NULL;
        PUSH(p, payloads, payload);
        if (!match(p, TOK_COMMA) || at(p, TOK_RBRACKET))
          break;
      } while (!at(p, TOK_EOF));
    if (!expect(p, TOK_RBRACKET, "Expected ']' after payload patterns."))
      return NULL;
    v->as.symbol.payloads = payloads.data;
    v->as.symbol.count = payloads.count;
  }
  parser_check_symbol_head(p, &head);
  v->span = through(start.span, p->previous.span);
  return v;
}

static Pattern *pattern_inner(Parser *p, int payload_context) {
  Token start = peek(p);
  if (match(p, TOK_LPAREN)) {
    if (match(p, TOK_RPAREN)) {
      Span span = through(start.span, p->previous.span);
      Pattern *unit = new_pattern(p, PAT_LITERAL, span);
      unit->as.literal = new_expr(p, EXPR_UNIT, span);
      return unit;
    }
    Pattern *v = pattern(p, payload_context);
    if (!v || !expect(p, TOK_RPAREN, "Expected ')' after pattern."))
      return NULL;
    v->span = through(start.span, p->previous.span);
    return v;
  }
  if (parser_at_symbol(p))
    return symbol_pattern(p, start);
  if (is_comparison(start.type)) {
    take(p);
    Pattern *v = new_pattern(p, PAT_COMPARE, start.span);
    v->as.compare.op = compare_op(start.type);
    v->as.compare.value = pattern_value(p);
    if (!v->as.compare.value)
      return NULL;
    v->span = through(start.span, v->as.compare.value->span);
    return v;
  }
  if (token_is_identifier(start.type)) {
    const char *text = parser_path(p, 0);
    if (!text)
      return NULL;
    if (match(p, TOK_COLON)) {
      Token colon = p->previous;
      if (!payload_context) {
        error_at(p, colon.span,
                 "Typed bindings are only allowed in symbol payload patterns.");
        return NULL;
      }
      if (start.type == TOK_WILDCARD || strchr(text, '.')) {
        error_at(p, through(start.span, colon.span),
                 "Typed symbol payload patterns require a binding name.");
        return NULL;
      }
      size_t mark = p->diagnostics->count;
      Type *annotation = parse_type(p);
      if (!annotation)
        return NULL;
      Pattern *v =
          new_pattern(p, PAT_TYPED_BIND, through(start.span, annotation->span));
      v->as.typed_binding.name = text;
      v->as.typed_binding.name_span = start.span;
      v->as.typed_binding.annotation = annotation;
      // Keep malformed payload syntax primary if its boundary is missing.
      if (at(p, TOK_COMMA) || at(p, TOK_RBRACKET) || at(p, TOK_RPAREN))
        parser_check_name_case(
            p, start, mark, TOK_LOWER_NAME,
            "Payload binding names must start with a lowercase letter.");
      return v;
    }
    if (!at(p, TOK_DOT_DOT) && !at(p, TOK_DOT_DOT_EQUAL)) {
      PatternKind kind = strcmp(text, "_") == 0 ? PAT_WILDCARD
                         : start.type == TOK_UPPER_NAME || strchr(text, '.')
                             ? PAT_SET
                             : PAT_BIND;
      Pattern *v = new_pattern(p, kind, through(start.span, p->previous.span));
      v->as.name = text;
      return v;
    }
    Expr *lower = new_expr(p, EXPR_NAME, through(start.span, p->previous.span));
    lower->as.name = text;
    return range_pattern(p, start, lower);
  }
  Expr *lower = pattern_value(p);
  if (!lower)
    return NULL;
  if (at(p, TOK_DOT_DOT) || at(p, TOK_DOT_DOT_EQUAL)) {
    return range_pattern(p, start, lower);
  }
  Pattern *v = new_pattern(p, PAT_LITERAL, lower->span);
  v->as.literal = lower;
  return v;
}

static Pattern *pattern(Parser *p, int payload) {
  if (!parser_enter(p))
    return NULL;
  Pattern *result = pattern_inner(p, payload);
  p->depth--;
  return result;
}

static Expr *arm_value(Parser *p) {
  if (at(p, TOK_LBRACE)) {
    if (!parser_enter(p))
      return NULL;
    Expr *result = parse_block(p);
    p->depth--;
    return result;
  }
  if (!at(p, TOK_RET) && !at(p, TOK_BREAK) && !at(p, TOK_CONTINUE)) {
    BlockItem item = {0};
    if (!parse_simple_item(p, &item))
      return NULL;
    if (item.kind == BLOCK_ITEM_EXPR)
      return item.as.expr;
    Expr *e = new_expr(p, EXPR_BLOCK, item.span);
    e->as.block.items = arena_alloc(p->arena, sizeof(item));
    e->as.block.items[0] = item;
    e->as.block.count = 1;
    return parser_checked_expr(p, e);
  }
  Stmt *s = parse_control_statement(p);
  if (!s)
    return NULL;
  // An arm with a control-flow statement has the same shape as an explicit
  // one-statement block; later stages need no special return-in-arm rule.
  BlockItem *item = arena_calloc(p->arena, 1, sizeof(*item));
  *item = (BlockItem){.kind = BLOCK_ITEM_STMT, .span = s->span, .as.stmt = s};
  Expr *e = new_expr(p, EXPR_BLOCK, s->span);
  e->as.block.items = item;
  e->as.block.count = 1;
  return parser_checked_expr(p, e);
}

Expr *parse_match_block(Parser *p) {
  Token start = take(p); // {
  List arms = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    size_t checkpoint = p->position;
    MatchArm arm = {.span = peek(p).span};
    arm.pattern = pattern(p, 0);
    if (arm.pattern && expect(p, TOK_FAT_ARROW, "Expected '=>' after pattern."))
      arm.value = arm_value(p);
    if (arm.value) {
      arm.span = through(arm.span, arm.value->span);
      PUSH(p, arms, arm);
    }
    if (!arm.value || !parser_separator(p, TOK_RBRACE))
      parser_recover(p, checkpoint, 1);
    newlines(p);
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' after match arms."))
    return NULL;
  Expr *e = new_expr(p, EXPR_MATCH, through(start.span, p->previous.span));
  e->as.match.arms = arms.data;
  e->as.match.count = arms.count;
  return parser_checked_expr(p, e);
}
