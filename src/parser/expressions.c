#include "parser/internal.h"

#include <stdio.h>
#include <string.h>

// strchr would also match the terminating NUL, so a zero byte in the
// source would count as a space.
static int blank(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static int collection_head(const Expr *e) {
  return e->kind == EXPR_NAME &&
         (!strcmp(e->as.name, "Arr") || !strcmp(e->as.name, "Vec"));
}
static Expr *call(Parser *p, Expr *callee, List types) {
  if (!expect(p, TOK_LPAREN, "Expected '(' in call."))
    return NULL;
  Span open = p->previous.span, first_comma = {0};
  List args = {0};
  if (!at(p, TOK_RPAREN)) {
    do {
      Expr *arg = parse_expression(p, 0, 1);
      if (!arg)
        return NULL;
      PUSH(p, args, arg);
      if (!match(p, TOK_COMMA))
        break;
      if (args.count == 1)
        first_comma = p->previous.span;
      if (at(p, TOK_RPAREN))
        break;
    } while (!at(p, TOK_EOF));
  }
  if (!expect(p, TOK_RPAREN, "Expected ')' after call arguments."))
    return NULL;
  Expr *e = new_expr(p, EXPR_CALL, through(callee->span, p->previous.span));
  e->as.call.callee = callee;
  e->as.call.args = args.data;
  e->as.call.count = args.count;
  e->as.call.types = types.data;
  e->as.call.type_count = types.count;
  e->as.call.open_span = open;
  e->as.call.first_comma_span = first_comma;
  return e;
}
static Expr *collection_literal(Parser *p, Expr *head, List types) {
  if (!expect(p, TOK_LBRACE, "Expected '{' before collection items."))
    return NULL;
  List items = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    Expr *item = parse_expression(p, 0, 1);
    if (!item)
      return NULL;
    PUSH(p, items, item);
    newlines(p);
    if (at(p, TOK_RBRACE) || at(p, TOK_EOF))
      break;
    if (!expect(p, TOK_COMMA, "Expected ',' between collection items."))
      return NULL;
    newlines(p);
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' after collection items."))
    return NULL;
  Expr *e = new_expr(p, EXPR_COLLECTION, through(head->span, p->previous.span));
  e->as.collection.constructor = head;
  e->as.collection.types = types.data;
  e->as.collection.type_count = types.count;
  e->as.collection.items = items.data;
  e->as.collection.count = items.count;
  return parser_checked_expr(p, e);
}
static Expr *struct_literal(Parser *p, Expr *head) {
  take(p); // {
  List fields = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    FieldValue field = {.span = peek(p).span};
    if (!parser_expect_identifier(p, "Expected a struct literal's field name."))
      return NULL;
    field.name = name(p, p->previous);
    if (!expect(p, TOK_EQUAL, "Expected '=' after field name."))
      return NULL;
    field.value = parse_expression(p, 0, 1);
    if (!field.value)
      return NULL;
    field.span = through(field.span, field.value->span);
    PUSH(p, fields, field);
    if (match(p, TOK_COMMA)) {
      newlines(p);
      continue;
    }
    if (!parser_separator(p, TOK_RBRACE))
      return NULL;
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' after struct literal."))
    return NULL;
  Expr *e = new_expr(p, EXPR_STRUCT_LIT, through(head->span, p->previous.span));
  e->as.structure.type = head;
  e->as.structure.fields = fields.data;
  e->as.structure.count = fields.count;
  return e;
}
typedef struct {
  TokenType token;
  int power;
  BinaryOp op;
  int right_associative;
} Operator;
static const Operator operators[] = {
    {TOK_STAR_STAR, 140, BINARY_POW, 1},
    {TOK_STAR, 120, BINARY_MUL, 0},
    {TOK_SLASH, 120, BINARY_DIV, 0},
    {TOK_PERCENT, 120, BINARY_REM, 0},
    {TOK_PLUS, 110, BINARY_ADD, 0},
    {TOK_MINUS, 110, BINARY_SUB, 0},
    {TOK_LSHIFT, 100, BINARY_LSHIFT, 0},
    {TOK_RSHIFT, 100, BINARY_RSHIFT, 0},
    {TOK_BIT_AND, 90, BINARY_BIT_AND, 0},
    {TOK_BIT_XOR, 80, BINARY_BIT_XOR, 0},
    {TOK_BIT_OR, 70, BINARY_BIT_OR, 0},
    {TOK_DOT_DOT, 60, BINARY_RANGE, 0},
    {TOK_DOT_DOT_EQUAL, 60, BINARY_RANGE, 0},
    {TOK_PIPE, 50, BINARY_ADD, 0},
    {TOK_EQUAL_EQUAL, 40, BINARY_EQ, 0},
    {TOK_BANG_EQUAL, 40, BINARY_NE, 0},
    {TOK_LESS_THAN, 40, BINARY_LT, 0},
    {TOK_LESS_THAN_EQUAL, 40, BINARY_LE, 0},
    {TOK_GREATER_THAN, 40, BINARY_GT, 0},
    {TOK_GREATER_THAN_EQUAL, 40, BINARY_GE, 0},
    {TOK_AND, 30, BINARY_LOGICAL_AND, 0},
    {TOK_OR, 20, BINARY_LOGICAL_OR, 0},
};
static const Operator *operator_info(TokenType t) {
  for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); i++)
    if (operators[i].token == t)
      return &operators[i];
  return NULL;
}
static int name_path(Expr *e) {
  while (e->kind == EXPR_FIELD)
    e = e->as.field.object;
  return e->kind == EXPR_NAME || e->kind == EXPR_SELF;
}
static int call_stage(Expr *stage) {
  while (stage->kind == EXPR_TRY || stage->kind == EXPR_PANIC)
    stage = stage->as.postfix;
  return stage->kind == EXPR_CALL && name_path(stage->as.call.callee);
}
static int assignment(TokenType t) {
  return t == TOK_EQUAL || t == TOK_PLUS_EQUAL || t == TOK_MINUS_EQUAL ||
         t == TOK_STAR_EQUAL || t == TOK_SLASH_EQUAL || t == TOK_PERCENT_EQUAL;
}
static int update(TokenType t) {
  return t == TOK_PLUS_PLUS || t == TOK_MINUS_MINUS;
}
static const char update_message[] =
    "Increment and decrement are only allowed as standalone statements.";
// Only offer the header hint when a member suffix or second body follows the
// matching brace. Never reinterpret an ordinary `if flag { field = value }`
// body as a literal.
static int header_literal(Parser *p) {
  size_t field = p->position + 1;
  while (field < p->tokens.count && p->tokens.items[field].type == TOK_NEWLINE)
    field++;
  if (field + 1 >= p->tokens.count ||
      !token_is_identifier(p->tokens.items[field].type) ||
      p->tokens.items[field + 1].type != TOK_EQUAL)
    return 0;
  size_t braces = 1;
  for (size_t i = p->position + 1; i < p->tokens.count; i++) {
    TokenType t = p->tokens.items[i].type;
    if (t == TOK_EOF)
      break;
    if (t == TOK_LBRACE)
      braces++;
    else if (t == TOK_RBRACE && !--braces) {
      TokenType next =
          i + 1 < p->tokens.count ? p->tokens.items[i + 1].type : TOK_EOF;
      return next == TOK_PERIOD || next == TOK_DOT_LPAREN || next == TOK_LBRACE;
    }
  }
  return 0;
}
static Expr *index_suffix(Parser *p, Expr *object) {
  Expr *index = parse_expression(p, 0, 1);
  if (!index || !expect(p, TOK_RPAREN, "Expected ')' after index."))
    return NULL;
  Expr *e = new_expr(p, EXPR_INDEX, through(object->span, p->previous.span));
  e->as.index.object = object;
  e->as.index.index = index;
  return e;
}

static Expr *field_suffix(Parser *p, Expr *object) {
  Token dot = p->previous;
  if (!parser_expect_member_name(p,
                                 "Expected a field or method name after '.'."))
    return NULL;
  Expr *e = new_expr(p, EXPR_FIELD, through(object->span, p->previous.span));
  e->as.field.object = object;
  e->as.field.name = name(p, p->previous);
  e->as.field.dot_span = dot.span;
  return e;
}

static Expr *type_argument_suffix(Parser *p, Expr *head, int allow_struct) {
  List types = {0};
  if (!parse_type_arguments(p, &types))
    return NULL;
  if (!at(p, TOK_LBRACE) || !collection_head(head))
    return call(p, head, types);
  if (!allow_struct) {
    error(p, "Wrap collection literals in conditions or iterables in "
             "parentheses.");
    return NULL;
  }
  return collection_literal(p, head, types);
}

// Applies one call, member, index, literal or postfix-operator suffix. Sets
// *applied when a suffix was consumed; returns NULL after an error.
static Expr *suffix(Parser *p, Expr *left, int allow_struct, int *applied) {
  Token token = peek(p);
  int named = left->kind == EXPR_NAME || left->kind == EXPR_FIELD;
  *applied = 1;
  if (token.type == TOK_LPAREN)
    return call(p, left, (List){0});
  if (named && token.type == TOK_LBRACKET)
    return type_argument_suffix(p, left, allow_struct);
  if (match(p, TOK_PERIOD))
    return field_suffix(p, left);
  if (named && token.type == TOK_LBRACE) {
    if (allow_struct)
      return struct_literal(p, left);
    if (header_literal(p)) {
      error(p, "Wrap struct literals in conditions or iterables in "
               "parentheses.");
      return NULL;
    }
  }
  if (match(p, TOK_DOT_LPAREN))
    return index_suffix(p, left);
  if (token.type == TOK_QUESTION || token.type == TOK_BANG) {
    take(p);
    Expr *e = new_expr(p, token.type == TOK_QUESTION ? EXPR_TRY : EXPR_PANIC,
                       through(left->span, token.span));
    e->as.postfix = left;
    return e;
  }
  *applied = 0;
  return left;
}

// The lexer intentionally keeps & and | as single-character bitwise
// operators. Recognize C-style pairs only when their source bytes touch;
// whitespace/comments/newline filtering must not manufacture && or ||.
static int c_style_logical(Parser *p, Token token) {
  if ((token.type != TOK_BIT_AND && token.type != TOK_BIT_OR) ||
      p->position + 1 >= p->tokens.count)
    return 0;
  Token next = p->tokens.items[p->position + 1];
  if (next.type != token.type || next.span.file != token.span.file ||
      next.span.start != token.span.end)
    return 0;
  Span span = through(token.span, next.span);
  Diagnostic *d =
      error_at(p, span,
               token.type == TOK_BIT_AND ? "Use 'and' instead of '&&'."
                                         : "Use 'or' instead of '||'.");
  if (d) {
    // A keyword needs separation even in unspaced input such as a&&b.
    const SourceFile *file = span.file;
    int before = span.start && !blank(file->text[span.start - 1]);
    int after = span.end < file->length && !blank(file->text[span.end]);
    char replacement[8];
    snprintf(replacement, sizeof(replacement), "%s%s%s", before ? " " : "",
             token.type == TOK_BIT_AND ? "and" : "or", after ? " " : "");
    diagnostic_fix(p->diagnostics, d, span, replacement);
  }
  return 1;
}

static Expr *pipe_stage(Parser *p, Expr *left, const Operator *op,
                        Expr *right) {
  const Operator *following = operator_info(peek(p).type);
  if ((right->kind == EXPR_MATCH && following &&
       following->power > op->power) ||
      (right->kind != EXPR_MATCH && !call_stage(right))) {
    error_at(p, right->span,
             "A pipeline stage must be a call to a name or dotted path, or "
             "a match block.");
    return NULL;
  }
  Expr *e = new_expr(p, EXPR_PIPE, through(left->span, right->span));
  e->as.pipe.value = left;
  e->as.pipe.stage = right;
  return e;
}

static Expr *expression_inner(Parser *p, int minimum, int allow_struct) {
  Expr *left = parse_primary(p, allow_struct);
  if (!left)
    return NULL;
  int compared = 0, ranged = 0;
  for (;;) {
    if (!parser_checked_expr(p, left))
      return NULL;
    if (minimum <= 160) {
      int applied;
      left = suffix(p, left, allow_struct, &applied);
      if (!left)
        return NULL;
      if (applied)
        continue;
    }
    Token token = peek(p);
    const Operator *op = operator_info(token.type);
    if (!op || op->power < minimum)
      break;
    if (c_style_logical(p, token))
      return NULL;
    int range = token.type == TOK_DOT_DOT || token.type == TOK_DOT_DOT_EQUAL;
    if ((is_comparison(token.type) && compared) || (range && ranged)) {
      error(p,
            is_comparison(token.type)
                ? "Comparison operators do not chain; use 'and' or parentheses."
                : "Range operators do not chain.");
      return NULL;
    }
    take(p);
    compared |= is_comparison(token.type);
    ranged |= range;
    Expr *right = token.type == TOK_PIPE && at(p, TOK_LBRACE)
                      ? parse_match_block(p)
                      : parse_expression(p, op->power + !op->right_associative,
                                         allow_struct);
    if (!right)
      return NULL;
    if (token.type == TOK_PIPE) {
      left = pipe_stage(p, left, op, right);
      if (!left)
        return NULL;
      continue;
    }
    Expr *e = new_expr(p, EXPR_BINARY, through(left->span, right->span));
    e->as.binary.op = op->op;
    e->as.binary.inclusive = token.type == TOK_DOT_DOT_EQUAL;
    e->as.binary.left = left;
    e->as.binary.right = right;
    left = e;
  }
  return left;
}

Expr *parse_expression(Parser *p, int minimum, int allow_struct) {
  if (!parser_enter(p))
    return NULL;
  Expr *result = expression_inner(p, minimum, allow_struct);
  p->depth--;
  if (result && update(peek(p).type)) {
    error(p, update_message);
    return NULL;
  }
  if (result && assignment(peek(p).type)) {
    error(p,
          "Assignment is only allowed as a standalone, non-chaining statement, "
          "with a name, field, or index on the left.");
    return NULL;
  }
  return result;
}

int parse_simple_item(Parser *p, BlockItem *item) {
  // Only this statement-level entry point permits assignment or ++/-- after
  // the target expression. All nested expressions go through
  // parse_expression().
  if (!parser_enter(p))
    return 0;
  Expr *left = expression_inner(p, 0, 1);
  p->depth--;
  if (!left)
    return 0;
  item->kind = BLOCK_ITEM_EXPR;
  item->as.expr = left;
  item->span = left->span;
  if (update(peek(p).type)) {
    Token op = take(p);
    if (left->kind != EXPR_NAME && left->kind != EXPR_FIELD &&
        left->kind != EXPR_INDEX) {
      error_at(
          p, left->span,
          "Expected a name, field, or index as an increment/decrement target.");
      return 0;
    }
    if (!at(p, TOK_NEWLINE) && !at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
      error(p, update_message);
      return 0;
    }
    Stmt *s = new_stmt(p, op.type == TOK_PLUS_PLUS ? STMT_INC : STMT_DEC,
                       through(left->span, op.span));
    s->as.value = left;
    item->kind = BLOCK_ITEM_STMT;
    item->as.stmt = s;
    item->span = s->span;
    return 1;
  }
  if (!assignment(peek(p).type))
    return 1;
  Token op = take(p);
  if (left->kind != EXPR_NAME && left->kind != EXPR_FIELD &&
      left->kind != EXPR_INDEX) {
    error_at(p, left->span,
             "Expected a name, field, or index as an assignment target.");
    return 0;
  }
  Expr *right = parse_expression(p, 0, 1);
  if (!right)
    return 0;
  Stmt *s = new_stmt(p, STMT_ASSIGN, through(left->span, right->span));
  s->as.assign.op = op.type == TOK_EQUAL         ? ASSIGN_SET
                    : op.type == TOK_PLUS_EQUAL  ? ASSIGN_ADD
                    : op.type == TOK_MINUS_EQUAL ? ASSIGN_SUB
                    : op.type == TOK_STAR_EQUAL  ? ASSIGN_MUL
                    : op.type == TOK_SLASH_EQUAL ? ASSIGN_DIV
                                                 : ASSIGN_REM;
  s->as.assign.target = left;
  s->as.assign.value = right;
  item->kind = BLOCK_ITEM_STMT;
  item->as.stmt = s;
  item->span = s->span;
  return 1;
}
