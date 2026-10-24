#include "parser/parser.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  Arena *arena;
  InternTable *names;
  TokenArray tokens;
  Diagnostics *diagnostics;
  size_t position;
  Token previous;
  int split_gt;
  unsigned probing;
  unsigned depth;
  size_t errors;
} Parser;

typedef struct {
  void *data;
  size_t count, capacity;
} List;

static void push(Parser *p, List *list, const void *item, size_t size) {
  if (list->count == list->capacity) {
    size_t capacity = list->capacity ? list->capacity * 2 : 4;
    if (capacity < list->capacity)
      capacity = SIZE_MAX;
    void *data = arena_calloc(p->arena, capacity, size);
    if (list->count)
      memcpy(data, list->data, list->count * size);
    list->data = data;
    list->capacity = capacity;
  }
  memcpy((char *)list->data + list->count++ * size, item, size);
}
#define PUSH(p, list, item) push(p, &(list), &(item), sizeof(item))

static Token peek(Parser *p) {
  Token token = p->tokens.items[p->position];
  if (p->split_gt) {
    token.type = TOK_GREATER_THAN;
    token.start++;
    token.length = 1;
    token.span.start++;
  }
  return token;
}

static Token take(Parser *p) {
  Token token = peek(p);
  if (token.type != TOK_EOF) {
    p->position++;
    p->split_gt = 0;
  }
  p->previous = token;
  return token;
}
static int at(Parser *p, TokenType type) { return peek(p).type == type; }
static int match(Parser *p, TokenType type) {
  if (!at(p, type))
    return 0;
  take(p);
  return 1;
}
static void error_at(Parser *p, Span span, const char *message) {
  p->errors++;
  if (!p->probing)
    diagnostics_error(p->diagnostics, span, message);
}
static void error(Parser *p, const char *message) {
  // Still mark speculative parses as failed, but do not repeat a lexer error
  // as an additional "Expected ..." syntax diagnostic at the same token.
  if (at(p, TOK_ERR)) {
    p->errors++;
    return;
  }
  error_at(p, peek(p).span, message);
}
static int expect(Parser *p, TokenType type, const char *message) {
  if (match(p, type))
    return 1;
  error(p, message);
  return 0;
}
static void newlines(Parser *p) {
  while (match(p, TOK_NEWLINE)) {
  }
}
static Span through(Span first, Span last) {
  return span_make(first.file, first.start, last.end);
}
static const char *name(Parser *p, Token token) {
  return intern_n(p->names, token.start, token.length);
}
static int type_name_token(TokenType t) {
  return t == TOK_IDENT || (t >= TOK_BOOL && t <= TOK_STR) || t == TOK_SELF;
}
static Expr *new_expr(Parser *p, ExprKind kind, Span span) {
  Expr *e = arena_calloc(p->arena, 1, sizeof(*e));
  e->kind = kind;
  e->span = span;
  e->depth = 1;
  return e;
}
static Type *new_type(Parser *p, TypeKind kind, Span span) {
  Type *t = arena_calloc(p->arena, 1, sizeof(*t));
  t->kind = kind;
  t->span = span;
  t->depth = 1;
  return t;
}
static Decl *new_decl(Parser *p, DeclKind kind, Span span,
                      Visibility visibility) {
  Decl *d = arena_calloc(p->arena, 1, sizeof(*d));
  d->kind = kind;
  d->span = span;
  d->visibility = visibility;
  return d;
}
static Pattern *new_pattern(Parser *p, PatternKind kind, Span span) {
  Pattern *v = arena_calloc(p->arena, 1, sizeof(*v));
  v->kind = kind;
  v->span = span;
  return v;
}

static Expr *checked_expr(Parser *p, Expr *e) {
  unsigned depth = 1;
#define CHILD(child)                                                           \
  do {                                                                         \
    const Expr *v = (child);                                                   \
    if (v && depth <= v->depth)                                                \
      depth = v->depth + 1;                                                    \
  } while (0)
  switch (e->kind) {
  case EXPR_SYMBOL:
    CHILD(e->as.symbol.payload);
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

static Type *checked_type(Parser *p, Type *t) {
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
  case TYPE_ARRAY:
    CHILD(t->as.array.element);
    break;
  case TYPE_SYM:
    for (size_t i = 0; i < t->as.sym.count; i++)
      CHILD(t->as.sym.items[i].type);
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

static Expr *expression(Parser *p, int minimum, int allow_struct);
static int simple_item(Parser *p, BlockItem *item);
static Expr *block(Parser *p);
static Type *type(Parser *p);
static Pattern *pattern(Parser *p);
static Decl *declaration(Parser *p, int in_impl);

static int enter(Parser *p) {
  if (p->depth >= 128) {
    error(p, "Syntax nesting limit exceeded.");
    return 0;
  }
  p->depth++;
  return 1;
}

static const char *path(Parser *p) {
  if (!type_name_token(peek(p).type)) {
    error(p, "Expected a name.");
    return NULL;
  }
  const char *result = name(p, take(p));
  while (match(p, TOK_PERIOD)) {
    if (!expect(p, TOK_IDENT, "Expected a name after '.'."))
      return NULL;
    const char *part = name(p, p->previous);
    size_t a = strlen(result), b = strlen(part);
    char *joined = arena_alloc(p->arena, a + b + 2);
    memcpy(joined, result, a);
    joined[a] = '.';
    memcpy(joined + a + 1, part, b + 1);
    result = intern(p->names, joined);
  }
  return result;
}

// Split >> virtually. Neither speculation nor a successful parse mutates the
// token array, and each virtual > has its own one-byte source span.
static int close_angle(Parser *p) {
  if (at(p, TOK_RSHIFT)) {
    Token token = peek(p);
    token.type = TOK_GREATER_THAN;
    token.length = 1;
    token.span.end = token.span.start + 1;
    p->previous = token;
    p->split_gt = 1;
    return 1;
  }
  return expect(p, TOK_GREATER_THAN, "Expected '>' after type arguments.");
}
static int angle_end(Parser *p) {
  return at(p, TOK_GREATER_THAN) || at(p, TOK_RSHIFT);
}
static int type_arguments(Parser *p, List *args) {
  if (!expect(p, TOK_LESS_THAN, "Expected '<'."))
    return 0;
  do {
    Type *arg = type(p);
    if (!arg)
      return 0;
    PUSH(p, *args, arg);
    if (!match(p, TOK_COMMA))
      break;
    if (angle_end(p))
      break;
  } while (!at(p, TOK_EOF));
  return close_angle(p);
}

static Type *type_inner(Parser *p) {
  Token start = peek(p);
  Type *t;
  if (match(p, TOK_LPAREN)) {
    if (!expect(p, TOK_RPAREN,
                "Expected ')' in the empty type; named tuples are not in the "
                "first version."))
      return NULL;
    t = new_type(p, TYPE_UNIT, through(start.span, p->previous.span));
  } else if (match(p, TOK_SYM)) {
    t = new_type(p, TYPE_SYM, start.span);
    if (!expect(p, TOK_LESS_THAN, "Expected '<' after Sym."))
      return NULL;
    List entries = {0};
    do {
      SymEntry entry = {.span = peek(p).span};
      if (match(p, TOK_COLON)) {
        if (!expect(p, TOK_IDENT, "Expected a symbol tag after ':'."))
          return NULL;
        entry.tag = name(p, p->previous);
        if (match(p, TOK_LPAREN)) {
          entry.type = type(p);
          if (!entry.type ||
              !expect(p, TOK_RPAREN, "Expected ')' after symbol payload type."))
            return NULL;
        }
      } else if (at(p, TOK_IDENT) && peek(p).length == 1 &&
                 *peek(p).start == '_') {
        entry.type = new_type(p, TYPE_INFER, take(p).span);
      } else {
        entry.type = type(p);
        if (!entry.type)
          return NULL;
      }
      entry.span = through(entry.span, p->previous.span);
      PUSH(p, entries, entry);
      if (!match(p, TOK_COMMA))
        break;
      if (angle_end(p))
        break;
    } while (!at(p, TOK_EOF));
    if (!close_angle(p))
      return NULL;
    t->as.sym.items = entries.data;
    t->as.sym.count = entries.count;
    t->span = through(start.span, p->previous.span);
  } else if (type_name_token(start.type)) {
    const char *text = path(p);
    if (!text)
      return NULL;
    if (strcmp(text, "_") == 0) {
      error_at(p, start.span, "'_' is only allowed as an entry in Sym<...>.");
      return NULL;
    }
    t = new_type(p, start.type == TOK_SELF ? TYPE_SELF : TYPE_NAME,
                 through(start.span, p->previous.span));
    t->as.name = text;
    if (at(p, TOK_LESS_THAN)) {
      List args = {0};
      if (!type_arguments(p, &args))
        return NULL;
      Type *applied =
          new_type(p, TYPE_APPLY, through(start.span, p->previous.span));
      applied->as.apply.base = t;
      applied->as.apply.args = args.data;
      applied->as.apply.count = args.count;
      t = applied;
    }
  } else {
    error(p, "Expected a type.");
    return NULL;
  }
  if (!checked_type(p, t))
    return NULL;
  while (match(p, TOK_LBRACKET)) {
    if (!expect(p, TOK_INT_LIT, "Expected a fixed array size."))
      return NULL;
    Token size = p->previous;
    if (!expect(p, TOK_RBRACKET, "Expected ']' after array size."))
      return NULL;
    Type *array = new_type(p, TYPE_ARRAY, through(t->span, p->previous.span));
    array->as.array.element = t;
    array->as.array.size = size;
    t = checked_type(p, array);
    if (!t)
      return NULL;
  }
  return t;
}
static Type *type(Parser *p) {
  if (!enter(p))
    return NULL;
  Type *result = type_inner(p);
  p->depth--;
  return result;
}

// Resume at the starting item's nesting level. An enclosing } is also a
// recovery boundary when an inner ( or [ was never closed.
static void recover(Parser *p, size_t start, int enclosed) {
  size_t braces = 0, parens = 0, brackets = 0;
  for (size_t i = start; i < p->tokens.count; i++) {
    TokenType t = p->tokens.items[i].type;
    if (i >= p->position) {
      if (t == TOK_EOF)
        return;
      if (t == TOK_RBRACE && !braces) {
        if (!enclosed)
          take(p);
        return;
      }
      if (t == TOK_NEWLINE && !braces && !parens && !brackets) {
        take(p);
        return;
      }
      take(p);
    }
    switch (t) {
    case TOK_LBRACE:
      braces++;
      break;
    case TOK_RBRACE:
      if (braces)
        braces--;
      break;
    case TOK_LPAREN:
      parens++;
      break;
    case TOK_RPAREN:
      if (parens)
        parens--;
      break;
    case TOK_LBRACKET:
      brackets++;
      break;
    case TOK_RBRACKET:
      if (brackets)
        brackets--;
      break;
    default:
      break;
    }
  }
}
static int separator(Parser *p, TokenType end) {
  if (at(p, end) || at(p, TOK_EOF))
    return 1;
  if (match(p, TOK_NEWLINE)) {
    newlines(p);
    return 1;
  }
  error(p, "Expected a newline between items.");
  return 0;
}

static Decl *binding(Parser *p, Visibility visibility) {
  Token start = take(p);
  Decl *d = new_decl(p, DECL_BINDING, start.span, visibility);
  d->as.binding.kind = start.type == TOK_LET   ? BIND_LET
                       : start.type == TOK_VAR ? BIND_VAR
                                               : BIND_CONST;
  if (!expect(p, TOK_IDENT, "Expected a binding name."))
    return NULL;
  d->name = name(p, p->previous);
  if (match(p, TOK_COLON)) {
    d->as.binding.annotation = type(p);
    if (!d->as.binding.annotation)
      return NULL;
  }
  if (!expect(p, TOK_EQUAL, "Expected '=' in binding."))
    return NULL;
  d->as.binding.value = expression(p, 0, 1);
  if (!d->as.binding.value)
    return NULL;
  d->span = through(start.span, d->as.binding.value->span);
  return d;
}

static Expr *block(Parser *p) {
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
      item.as.decl = binding(p, VISIBILITY_PRIV);
      valid = item.as.decl != NULL;
      if (valid)
        item.span = item.as.decl->span;
    } else if (at(p, TOK_RET) || at(p, TOK_BREAK) || at(p, TOK_CONTINUE)) {
      Token keyword = take(p);
      item.kind = BLOCK_ITEM_STMT;
      Stmt *s = arena_calloc(p->arena, 1, sizeof(*s));
      s->span = keyword.span;
      s->kind = keyword.type == TOK_RET     ? STMT_RET
                : keyword.type == TOK_BREAK ? STMT_BREAK
                                            : STMT_CONTINUE;
      if (s->kind == STMT_RET && !at(p, TOK_NEWLINE) && !at(p, TOK_RBRACE) &&
          !at(p, TOK_EOF)) {
        s->as.value = expression(p, 0, 1);
        valid = s->as.value != NULL;
        if (valid)
          s->span = through(keyword.span, s->as.value->span);
      }
      item.as.stmt = s;
      item.span = s->span;
    } else if (match(p, TOK_ERR)) {
      valid = 0; // Already diagnosed by the lexer.
    } else {
      valid = simple_item(p, &item);
    }
    if (valid)
      PUSH(p, items, item);
    if (!valid || !separator(p, TOK_RBRACE))
      recover(p, checkpoint, 1);
    newlines(p);
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' to close block."))
    return NULL;
  Expr *e = new_expr(p, EXPR_BLOCK, through(start.span, p->previous.span));
  e->as.block.items = items.data;
  e->as.block.count = items.count;
  return checked_expr(p, e);
}

static int parameter(Parser *p, Parameter *param, int field) {
  *param = (Parameter){.span = peek(p).span};
  if (field && match(p, TOK_PUB))
    param->visibility = VISIBILITY_PUB;
  if (!expect(p, TOK_IDENT,
              field ? "Expected a field name." : "Expected a parameter name."))
    return 0;
  param->name = name(p, p->previous);
  if (!expect(p, TOK_COLON, "Expected ':' before type."))
    return 0;
  if (!field && match(p, TOK_BIT_AND))
    param->borrow = BORROW_MUT;
  param->type = type(p);
  if (!param->type)
    return 0;
  param->span = through(param->span, param->type->span);
  return 1;
}

static const char *top_level_statement_message =
    "Statements are only allowed inside a function.";

// Only diagnose statement-shaped input. Typed parameters, a following body or
// return annotation still select declaration parsing, including its specific
// malformed-header errors. This lookahead never consumes or splits tokens.
static int top_level_statement(Parser *p) {
  TokenType first = peek(p).type;
  if (first >= TOK_INT_LIT && first <= TOK_BOOL_LIT)
    return 1;
  int type_keyword = first >= TOK_BOOL && first <= TOK_STR;
  switch (first) {
  case TOK_LET:
  case TOK_VAR:
  case TOK_RET:
  case TOK_BREAK:
  case TOK_CONTINUE:
  case TOK_IF:
  case TOK_WHILE:
  case TOK_FOR:
  case TOK_LPAREN:
  case TOK_COLON:
  case TOK_MINUS:
  case TOK_BANG:
  case TOK_TILDE:
    return 1;
  case TOK_IDENT:
    break;
  default:
    if (!type_keyword)
      return 0;
    break;
  }
  TokenType next = p->tokens.items[p->position + 1].type;
  // `Name<` starts a (later-version) generic declaration, not a comparison.
  if (next != TOK_LPAREN)
    return !type_keyword && next != TOK_LESS_THAN &&
           (next == TOK_NEWLINE || next == TOK_EOF || next == TOK_PERIOD ||
            next == TOK_LBRACKET || next == TOK_QUESTION || next == TOK_PIPE ||
            (next >= TOK_EQUAL && next <= TOK_RSHIFT));
  size_t parens = 0, braces = 0, brackets = 0;
  for (size_t i = p->position + 1; i < p->tokens.count; i++) {
    TokenType t = p->tokens.items[i].type;
    if (t == TOK_EOF || t == TOK_ERR)
      return 0;
    if (t == TOK_COLON && parens == 1 && !braces && !brackets &&
        p->tokens.items[i - 1].type == TOK_IDENT)
      return 0;
    if (t == TOK_LPAREN)
      parens++;
    else if (t == TOK_RPAREN) {
      if (!--parens) {
        if (braces || brackets)
          return 0;
        // A body/annotation on the wrong line is still a declaration error.
        do {
          next = p->tokens.items[++i].type;
        } while (next == TOK_NEWLINE);
        return next != TOK_LBRACE && next != TOK_COLON;
      }
    } else if (t == TOK_LBRACE)
      braces++;
    else if (t == TOK_RBRACE) {
      if (!braces)
        return 0;
      braces--;
    } else if (t == TOK_LBRACKET)
      brackets++;
    else if (t == TOK_RBRACKET) {
      if (!brackets)
        return 0;
      brackets--;
    }
  }
  return 0;
}

static Decl *declaration(Parser *p, int in_impl) {
  Token start = peek(p);
  Visibility visibility = match(p, TOK_PUB) ? VISIBILITY_PUB : VISIBILITY_PRIV;
  if (at(p, TOK_CONST)) {
    Decl *d = binding(p, visibility);
    if (d)
      d->span = through(start.span, d->span);
    return d;
  }
  if (!in_impl && match(p, TOK_TYPE)) {
    if (!expect(p, TOK_IDENT, "Expected a named type's name."))
      return NULL;
    Decl *d = new_decl(p, DECL_TYPE, start.span, visibility);
    d->name = name(p, p->previous);
    if (!expect(p, TOK_EQUAL, "Expected '=' in named type declaration."))
      return NULL;
    d->as.alias = type(p);
    if (!d->as.alias)
      return NULL;
    d->span = through(start.span, d->as.alias->span);
    return d;
  }
  if (!in_impl && match(p, TOK_IMPL)) {
    Decl *d = new_decl(p, DECL_IMPL, start.span, visibility);
    d->as.impl.target = type(p);
    if (!d->as.impl.target || !expect(p, TOK_LBRACE,
                                      "Expected '{' after impl type; traits "
                                      "are not in the first version."))
      return NULL;
    List methods = {0};
    newlines(p);
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
      size_t checkpoint = p->position;
      Decl *method = declaration(p, 1);
      if (method)
        PUSH(p, methods, method);
      if (!method || !separator(p, TOK_RBRACE))
        recover(p, checkpoint, 1);
      newlines(p);
    }
    if (!expect(p, TOK_RBRACE, "Expected '}' after impl block."))
      return NULL;
    d->as.impl.methods = methods.data;
    d->as.impl.count = methods.count;
    d->span = through(start.span, p->previous.span);
    return d;
  }
  if (!in_impl && visibility == VISIBILITY_PRIV && top_level_statement(p)) {
    error(p, top_level_statement_message);
    return NULL;
  }
  if (!expect(p, TOK_IDENT,
              in_impl ? "Expected a method declaration."
                      : "Expected a declaration (function, struct, named "
                        "type, or impl)."))
    return NULL;
  const char *decl_name = name(p, p->previous);
  if (!in_impl && match(p, TOK_LBRACE)) {
    Decl *d = new_decl(p, DECL_STRUCT, start.span, visibility);
    d->name = decl_name;
    List fields = {0};
    newlines(p);
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
      size_t checkpoint = p->position;
      Parameter field;
      int valid = parameter(p, &field, 1);
      if (valid)
        PUSH(p, fields, field);
      if (!valid || !separator(p, TOK_RBRACE))
        recover(p, checkpoint, 1);
      newlines(p);
    }
    if (!expect(p, TOK_RBRACE, "Expected '}' after struct fields."))
      return NULL;
    d->as.structure.fields = fields.data;
    d->as.structure.count = fields.count;
    d->span = through(start.span, p->previous.span);
    return d;
  }
  if (!expect(p, TOK_LPAREN,
              "Expected '(' after function name (or '{' for a struct)."))
    return NULL;
  Decl *d = new_decl(p, DECL_FN, start.span, visibility);
  d->name = decl_name;
  List params = {0};
  if (!at(p, TOK_RPAREN)) {
    do {
      Parameter param;
      if (!parameter(p, &param, 0))
        return NULL;
      PUSH(p, params, param);
      if (!match(p, TOK_COMMA))
        break;
      if (at(p, TOK_RPAREN))
        break;
    } while (!at(p, TOK_EOF));
  }
  if (!expect(p, TOK_RPAREN, "Expected ')' after parameters."))
    return NULL;
  if (match(p, TOK_COLON)) {
    d->as.function.return_type = type(p);
    if (!d->as.function.return_type)
      return NULL;
  }
  d->as.function.params = params.data;
  d->as.function.count = params.count;
  d->as.function.body = block(p);
  if (!d->as.function.body)
    return NULL;
  d->span = through(start.span, d->as.function.body->span);
  return d;
}

static Expr *call(Parser *p, Expr *callee, List types) {
  if (!expect(p, TOK_LPAREN, "Expected '(' in call."))
    return NULL;
  List args = {0};
  if (!at(p, TOK_RPAREN)) {
    do {
      Expr *arg = expression(p, 0, 1);
      if (!arg)
        return NULL;
      PUSH(p, args, arg);
      if (!match(p, TOK_COMMA))
        break;
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
  return e;
}
static Expr *struct_literal(Parser *p, Expr *head) {
  take(p); // {
  List fields = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    FieldValue field = {.span = peek(p).span};
    if (!expect(p, TOK_IDENT, "Expected a struct literal's field name."))
      return NULL;
    field.name = name(p, p->previous);
    if (!expect(p, TOK_COLON, "Expected ':' after field name."))
      return NULL;
    field.value = expression(p, 0, 1);
    if (!field.value)
      return NULL;
    field.span = through(field.span, field.value->span);
    PUSH(p, fields, field);
    if (match(p, TOK_COMMA)) {
      newlines(p);
      continue;
    }
    if (!separator(p, TOK_RBRACE))
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

static Expr *conditional(Parser *p) {
  Token start = take(p);
  Expr *condition = expression(p, 0, 0);
  if (!condition)
    return NULL;
  Expr *yes = block(p);
  if (!yes)
    return NULL;
  Expr *no = NULL;
  // A newline belongs to the surrounding block unless an else follows it.
  Parser saved = *p;
  newlines(p);
  if (match(p, TOK_ELSE)) {
    if (at(p, TOK_IF)) {
      if (!enter(p))
        return NULL;
      no = conditional(p);
      p->depth--;
    } else {
      no = block(p);
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
  return checked_expr(p, e);
}

static Expr *primary(Parser *p, int allow_struct) {
  Token token = peek(p);
  if (token.type == TOK_PLUS_PLUS || token.type == TOK_MINUS_MINUS) {
    error(p, "Increment and decrement must be postfix standalone statements "
             "(x++ or x--).");
    return NULL;
  }
  if (type_name_token(token.type)) {
    take(p);
    Expr *e = new_expr(p, EXPR_NAME, token.span);
    e->as.name = name(p, token);
    return e;
  }
  ExprKind kind;
  switch (token.type) {
  case TOK_INT_LIT:
    kind = EXPR_INT;
    break;
  case TOK_FLOAT_LIT:
    kind = EXPR_FLOAT;
    break;
  case TOK_STR_LIT:
    kind = EXPR_STRING;
    break;
  case TOK_CHAR_LIT:
    kind = EXPR_CHAR;
    break;
  case TOK_BOOL_LIT:
    kind = EXPR_BOOL;
    break;
  default:
    kind = EXPR_UNIT;
    break;
  }
  if (kind != EXPR_UNIT) {
    take(p);
    Expr *e = new_expr(p, kind, token.span);
    e->as.literal = token;
    return e;
  }
  if (match(p, TOK_LPAREN)) {
    if (match(p, TOK_RPAREN))
      return new_expr(p, EXPR_UNIT, through(token.span, p->previous.span));
    Expr *e = expression(p, 0, 1);
    if (!e || !expect(p, TOK_RPAREN, "Expected ')' after expression."))
      return NULL;
    // Keep grouping in the span, but not as an otherwise redundant AST node.
    e->span = through(token.span, p->previous.span);
    return e;
  }
  if (match(p, TOK_COLON)) {
    if (!expect(p, TOK_IDENT, "Expected a symbol tag after ':'."))
      return NULL;
    Expr *e = new_expr(p, EXPR_SYMBOL, token.span);
    e->as.symbol.tag = name(p, p->previous);
    if (match(p, TOK_LPAREN)) {
      e->as.symbol.payload = expression(p, 0, 1);
      if (!e->as.symbol.payload ||
          !expect(p, TOK_RPAREN, "Expected ')' after symbol payload."))
        return NULL;
    }
    e->span = through(token.span, p->previous.span);
    return e;
  }
  if (at(p, TOK_LBRACE)) {
    error(p, "Bare blocks are not allowed as expressions or statements.");
    return NULL;
  }
  if (at(p, TOK_IF))
    return conditional(p);
  if (match(p, TOK_WHILE)) {
    Expr *e = new_expr(p, EXPR_WHILE, token.span);
    e->as.while_loop.condition = expression(p, 0, 0);
    if (!e->as.while_loop.condition)
      return NULL;
    e->as.while_loop.body = block(p);
    if (!e->as.while_loop.body)
      return NULL;
    e->span = through(token.span, e->as.while_loop.body->span);
    return e;
  }
  if (match(p, TOK_FOR)) {
    Expr *e = new_expr(p, EXPR_FOR, token.span);
    if (!expect(p, TOK_IDENT, "Expected a loop variable."))
      return NULL;
    e->as.for_loop.name = name(p, p->previous);
    if (!expect(p, TOK_IN, "Expected 'in' after loop variable."))
      return NULL;
    e->as.for_loop.iterable = expression(p, 0, 0);
    if (!e->as.for_loop.iterable)
      return NULL;
    e->as.for_loop.body = block(p);
    if (!e->as.for_loop.body)
      return NULL;
    e->span = through(token.span, e->as.for_loop.body->span);
    return e;
  }
  if (token.type == TOK_MINUS || token.type == TOK_BANG ||
      token.type == TOK_TILDE) {
    take(p);
    Expr *value = expression(p, 130, allow_struct);
    if (!value)
      return NULL;
    Expr *e = new_expr(p, EXPR_UNARY, through(token.span, value->span));
    e->as.unary.op = token.type == TOK_MINUS  ? UNARY_NEG
                     : token.type == TOK_BANG ? UNARY_NOT
                                              : UNARY_BIT_NOT;
    e->as.unary.value = value;
    return e;
  }
  error(p, "Expected an expression.");
  return NULL;
}

static int comparison(TokenType t) {
  return t >= TOK_EQUAL_EQUAL && t <= TOK_GREATER_THAN_EQUAL;
}
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
    Expr *value = primary(p, 0);
    Expr *e = new_expr(p, EXPR_UNARY, through(t.span, value->span));
    e->as.unary.op = UNARY_NEG;
    e->as.unary.value = value;
    return e;
  }
  if (t.type == TOK_IDENT) {
    const char *text = path(p);
    if (!text)
      return NULL;
    Expr *e = new_expr(p, EXPR_NAME, through(t.span, p->previous.span));
    e->as.name = text;
    return e;
  }
  if (t.type >= TOK_INT_LIT && t.type <= TOK_BOOL_LIT)
    return primary(p, 0);
  error(p, "Expected a literal or constant in pattern.");
  return NULL;
}
static Pattern *pattern_inner(Parser *p) {
  Token start = peek(p);
  if (match(p, TOK_LPAREN)) {
    if (match(p, TOK_RPAREN)) {
      Span span = through(start.span, p->previous.span);
      Pattern *unit = new_pattern(p, PAT_LITERAL, span);
      unit->as.literal = new_expr(p, EXPR_UNIT, span);
      return unit;
    }
    Pattern *v = pattern(p);
    if (!v || !expect(p, TOK_RPAREN, "Expected ')' after pattern."))
      return NULL;
    v->span = through(start.span, p->previous.span);
    return v;
  }
  if (match(p, TOK_COLON)) {
    if (!expect(p, TOK_IDENT, "Expected a symbol tag in pattern."))
      return NULL;
    Pattern *v = new_pattern(p, PAT_SYMBOL, start.span);
    v->as.symbol.tag = name(p, p->previous);
    if (match(p, TOK_LPAREN)) {
      v->as.symbol.payload = pattern(p);
      if (!v->as.symbol.payload ||
          !expect(p, TOK_RPAREN, "Expected ')' after payload pattern."))
        return NULL;
    }
    v->span = through(start.span, p->previous.span);
    return v;
  }
  if (comparison(start.type)) {
    take(p);
    Pattern *v = new_pattern(p, PAT_COMPARE, start.span);
    v->as.compare.op = compare_op(start.type);
    v->as.compare.value = pattern_value(p);
    if (!v->as.compare.value)
      return NULL;
    v->span = through(start.span, v->as.compare.value->span);
    return v;
  }
  if (start.type == TOK_IDENT) {
    const char *text = path(p);
    if (!text)
      return NULL;
    if (!at(p, TOK_DOT_DOT)) {
      PatternKind kind = strcmp(text, "_") == 0 ? PAT_WILDCARD
                         : strchr(text, '.')    ? PAT_SET
                                                : PAT_BIND;
      Pattern *v = new_pattern(p, kind, through(start.span, p->previous.span));
      v->as.name = text;
      return v;
    }
    Expr *lower = new_expr(p, EXPR_NAME, through(start.span, p->previous.span));
    lower->as.name = text;
    take(p);
    Expr *upper = pattern_value(p);
    if (!upper)
      return NULL;
    Pattern *v = new_pattern(p, PAT_RANGE, through(start.span, upper->span));
    v->as.range.lower = lower;
    v->as.range.upper = upper;
    return v;
  }
  Expr *lower = pattern_value(p);
  if (!lower)
    return NULL;
  if (match(p, TOK_DOT_DOT)) {
    Expr *upper = pattern_value(p);
    if (!upper)
      return NULL;
    Pattern *v = new_pattern(p, PAT_RANGE, through(start.span, upper->span));
    v->as.range.lower = lower;
    v->as.range.upper = upper;
    return v;
  }
  Pattern *v = new_pattern(p, PAT_LITERAL, lower->span);
  v->as.literal = lower;
  return v;
}
static Pattern *pattern(Parser *p) {
  if (!enter(p))
    return NULL;
  Pattern *result = pattern_inner(p);
  p->depth--;
  return result;
}
static Expr *arm_value(Parser *p) {
  if (at(p, TOK_LBRACE)) {
    if (!enter(p))
      return NULL;
    Expr *result = block(p);
    p->depth--;
    return result;
  }
  if (!at(p, TOK_RET) && !at(p, TOK_BREAK) && !at(p, TOK_CONTINUE)) {
    BlockItem item = {0};
    if (!simple_item(p, &item))
      return NULL;
    if (item.kind == BLOCK_ITEM_EXPR)
      return item.as.expr;
    Expr *e = new_expr(p, EXPR_BLOCK, item.span);
    e->as.block.items = arena_alloc(p->arena, sizeof(item));
    e->as.block.items[0] = item;
    e->as.block.count = 1;
    return checked_expr(p, e);
  }
  Token keyword = take(p);
  Stmt *s = arena_calloc(p->arena, 1, sizeof(*s));
  s->kind = keyword.type == TOK_RET     ? STMT_RET
            : keyword.type == TOK_BREAK ? STMT_BREAK
                                        : STMT_CONTINUE;
  s->span = keyword.span;
  if (s->kind == STMT_RET && !at(p, TOK_NEWLINE) && !at(p, TOK_RBRACE) &&
      !at(p, TOK_EOF)) {
    s->as.value = expression(p, 0, 1);
    if (!s->as.value)
      return NULL;
    s->span = through(keyword.span, s->as.value->span);
  }
  // An arm with a control-flow statement has the same shape as an explicit
  // one-statement block; later stages need no special return-in-arm rule.
  BlockItem *item = arena_calloc(p->arena, 1, sizeof(*item));
  *item = (BlockItem){.kind = BLOCK_ITEM_STMT, .span = s->span, .as.stmt = s};
  Expr *e = new_expr(p, EXPR_BLOCK, s->span);
  e->as.block.items = item;
  e->as.block.count = 1;
  return checked_expr(p, e);
}
static Expr *match_block(Parser *p) {
  Token start = take(p); // {
  List arms = {0};
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    size_t checkpoint = p->position;
    MatchArm arm = {.span = peek(p).span};
    arm.pattern = pattern(p);
    if (arm.pattern && expect(p, TOK_FAT_ARROW, "Expected '=>' after pattern."))
      arm.value = arm_value(p);
    if (arm.value) {
      arm.span = through(arm.span, arm.value->span);
      PUSH(p, arms, arm);
    }
    if (!arm.value || !separator(p, TOK_RBRACE))
      recover(p, checkpoint, 1);
    newlines(p);
  }
  if (!expect(p, TOK_RBRACE, "Expected '}' after match arms."))
    return NULL;
  Expr *e = new_expr(p, EXPR_MATCH, through(start.span, p->previous.span));
  e->as.match.arms = arms.data;
  e->as.match.count = arms.count;
  return checked_expr(p, e);
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
  return e->kind == EXPR_NAME;
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
static Expr *expression_inner(Parser *p, int minimum, int allow_struct) {
  Expr *left = primary(p, allow_struct);
  if (!left)
    return NULL;
  int compared = 0, ranged = 0;
  for (;;) {
    if (!checked_expr(p, left))
      return NULL;
    Token token = peek(p);
    if (minimum <= 160) {
      if (token.type == TOK_LPAREN) {
        left = call(p, left, (List){0});
        if (!left)
          return NULL;
        continue;
      }
      if (token.type == TOK_LESS_THAN &&
          (left->kind == EXPR_NAME || left->kind == EXPR_FIELD)) {
        Parser saved = *p;
        p->probing++;
        List types = {0};
        int success = type_arguments(p, &types) && at(p, TOK_LPAREN) &&
                      p->errors == saved.errors;
        if (success) {
          p->probing = saved.probing;
          left = call(p, left, types);
          if (!left)
            return NULL;
          continue;
        }
        *p = saved;
      }
      if (match(p, TOK_PERIOD)) {
        if (!expect(p, TOK_IDENT, "Expected a field or method name after '.'."))
          return NULL;
        Expr *e =
            new_expr(p, EXPR_FIELD, through(left->span, p->previous.span));
        e->as.field.object = left;
        e->as.field.name = name(p, p->previous);
        left = e;
        continue;
      }
      if (token.type == TOK_LBRACE &&
          (left->kind == EXPR_NAME || left->kind == EXPR_FIELD)) {
        if (allow_struct) {
          left = struct_literal(p, left);
          if (!left)
            return NULL;
          continue;
        }
        // A named field cannot start an ordinary block item. Only diagnose
        // that unmistakable literal shape; empty blocks and normal bodies
        // must retain the existing condition/body ambiguity rule.
        size_t field = p->position + 1;
        while (field < p->tokens.count &&
               p->tokens.items[field].type == TOK_NEWLINE)
          field++;
        if (field + 1 < p->tokens.count &&
            p->tokens.items[field].type == TOK_IDENT &&
            p->tokens.items[field + 1].type == TOK_COLON) {
          error(p, "Wrap struct literals in conditions or iterables in "
                   "parentheses.");
          return NULL;
        }
      }
      if (match(p, TOK_LBRACKET)) {
        Expr *index = expression(p, 0, 1);
        if (!index || !expect(p, TOK_RBRACKET, "Expected ']' after index."))
          return NULL;
        Expr *e =
            new_expr(p, EXPR_INDEX, through(left->span, p->previous.span));
        e->as.index.object = left;
        e->as.index.index = index;
        left = e;
        continue;
      }
      if (token.type == TOK_QUESTION || token.type == TOK_BANG) {
        take(p);
        Expr *e =
            new_expr(p, token.type == TOK_QUESTION ? EXPR_TRY : EXPR_PANIC,
                     through(left->span, token.span));
        e->as.postfix = left;
        left = e;
        continue;
      }
    }
    const Operator *op = operator_info(token.type);
    if (!op || op->power < minimum)
      break;
    // The lexer intentionally keeps & and | as single-character bitwise
    // operators. Recognize C-style pairs only when their source bytes touch;
    // whitespace/comments/newline filtering must not manufacture && or ||.
    if ((token.type == TOK_BIT_AND || token.type == TOK_BIT_OR) &&
        p->position + 1 < p->tokens.count) {
      Token next = p->tokens.items[p->position + 1];
      if (next.type == token.type && next.span.file == token.span.file &&
          next.span.start == token.span.end) {
        error_at(p, through(token.span, next.span),
                 token.type == TOK_BIT_AND ? "Use 'and' instead of '&&'."
                                           : "Use 'or' instead of '||'.");
        return NULL;
      }
    }
    if ((comparison(token.type) && compared) ||
        (token.type == TOK_DOT_DOT && ranged)) {
      error(p,
            comparison(token.type)
                ? "Comparison operators do not chain; use 'and' or parentheses."
                : "Range operators do not chain.");
      return NULL;
    }
    take(p);
    if (comparison(token.type))
      compared = 1;
    if (token.type == TOK_DOT_DOT)
      ranged = 1;
    Expr *right =
        token.type == TOK_PIPE && at(p, TOK_LBRACE)
            ? match_block(p)
            : expression(p, op->power + !op->right_associative, allow_struct);
    if (!right)
      return NULL;
    Expr *e;
    if (token.type == TOK_PIPE) {
      const Operator *following = operator_info(peek(p).type);
      if ((right->kind == EXPR_MATCH && following &&
           following->power > op->power) ||
          (right->kind != EXPR_MATCH && !call_stage(right))) {
        error_at(p, right->span,
                 "A pipeline stage must be a call to a name or dotted path, or "
                 "a match block.");
        return NULL;
      }
      e = new_expr(p, EXPR_PIPE, through(left->span, right->span));
      e->as.pipe.value = left;
      e->as.pipe.stage = right;
    } else {
      e = new_expr(p, EXPR_BINARY, through(left->span, right->span));
      e->as.binary.op = op->op;
      e->as.binary.left = left;
      e->as.binary.right = right;
    }
    left = e;
  }
  return left;
}
static Expr *expression(Parser *p, int minimum, int allow_struct) {
  if (!enter(p))
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

static int simple_item(Parser *p, BlockItem *item) {
  // Only this statement-level entry point permits assignment or ++/-- after
  // the target expression. All nested expressions go through expression().
  if (!enter(p))
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
    Stmt *s = arena_calloc(p->arena, 1, sizeof(*s));
    s->kind = op.type == TOK_PLUS_PLUS ? STMT_INC : STMT_DEC;
    s->span = through(left->span, op.span);
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
  Expr *right = expression(p, 0, 1);
  if (!right)
    return 0;
  Stmt *s = arena_calloc(p->arena, 1, sizeof(*s));
  s->kind = STMT_ASSIGN;
  s->span = through(left->span, right->span);
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

Program *parse_program(Arena *arena, InternTable *names, TokenArray tokens,
                       Diagnostics *diagnostics) {
  Program *program = arena_calloc(arena, 1, sizeof(*program));
  if (!tokens.count || tokens.items[tokens.count - 1].type != TOK_EOF) {
    diagnostics_error(diagnostics, span_make(NULL, 0, 0),
                      "Parser input must end in EOF.");
    return program;
  }
  Parser p = {.arena = arena,
              .names = names,
              .tokens = tokens,
              .diagnostics = diagnostics};
  program->span =
      through(tokens.items[0].span, tokens.items[tokens.count - 1].span);
  List declarations = {0};
  newlines(&p);
  while (!at(&p, TOK_EOF)) {
    size_t checkpoint = p.position;
    Decl *d = match(&p, TOK_ERR) ? NULL : declaration(&p, 0);
    if (d)
      PUSH(&p, declarations, d);
    if (!d || !separator(&p, TOK_EOF))
      recover(&p, checkpoint, 0);
    newlines(&p);
  }
  program->items = declarations.data;
  program->count = declarations.count;
  return program;
}
