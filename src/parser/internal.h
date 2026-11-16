#ifndef BRIEF_PARSER_INTERNAL_H
#define BRIEF_PARSER_INTERNAL_H

#include "parser/parser.h"

typedef struct {
  Arena *arena;
  InternTable *names;
  TokenArray tokens;
  Diagnostics *diagnostics;
  size_t position;
  Token previous;
  unsigned depth;
} Parser;

typedef struct {
  void *data;
  size_t count, capacity;
} List;

// Token cursor.

static inline Token peek(Parser *p) { return p->tokens.items[p->position]; }
static inline Token take(Parser *p) {
  Token token = peek(p);
  if (token.type != TOK_EOF)
    p->position++;
  p->previous = token;
  return token;
}
static inline int at(Parser *p, TokenType type) { return peek(p).type == type; }
static inline int match(Parser *p, TokenType type) {
  if (!at(p, type))
    return 0;
  take(p);
  return 1;
}
static inline void newlines(Parser *p) {
  while (match(p, TOK_NEWLINE)) {
  }
}
static inline Span through(Span first, Span last) {
  return span_make(first.file, first.start, last.end);
}
static inline const char *name(Parser *p, Token token) {
  return intern_n(p->names, token.start, token.length);
}
static inline int is_comparison(TokenType t) {
  return t >= TOK_EQUAL_EQUAL && t <= TOK_GREATER_THAN_EQUAL;
}

// Diagnostics.

static inline Diagnostic *error_at(Parser *p, Span span, const char *message) {
  return diagnostics_error(p->diagnostics, span, message);
}
static inline void error(Parser *p, const char *message) {
  // Do not repeat a lexer error as an additional syntax diagnostic.
  if (at(p, TOK_ERR))
    return;
  error_at(p, peek(p).span, message);
}
static inline int expect(Parser *p, TokenType type, const char *message) {
  if (match(p, type))
    return 1;
  error(p, message);
  return 0;
}

// Node construction.

static inline Expr *new_expr(Parser *p, ExprKind kind, Span span) {
  Expr *e = arena_calloc(p->arena, 1, sizeof(*e));
  e->kind = kind;
  e->span = span;
  e->depth = 1;
  return e;
}
static inline Type *new_type(Parser *p, TypeKind kind, Span span) {
  Type *t = arena_calloc(p->arena, 1, sizeof(*t));
  t->kind = kind;
  t->span = span;
  t->depth = 1;
  return t;
}
static inline Decl *new_decl(Parser *p, DeclKind kind, Span span,
                             Visibility visibility) {
  Decl *d = arena_calloc(p->arena, 1, sizeof(*d));
  d->kind = kind;
  d->span = span;
  d->visibility = visibility;
  return d;
}
static inline Pattern *new_pattern(Parser *p, PatternKind kind, Span span) {
  Pattern *v = arena_calloc(p->arena, 1, sizeof(*v));
  v->kind = kind;
  v->span = span;
  return v;
}
static inline Stmt *new_stmt(Parser *p, StmtKind kind, Span span) {
  Stmt *s = arena_calloc(p->arena, 1, sizeof(*s));
  s->kind = kind;
  s->span = span;
  return s;
}

// cursor.c: lists and error recovery.

void parser_push(Parser *p, List *list, const void *item, size_t size);
#define PUSH(p, list, item) parser_push(p, &(list), &(item), sizeof(item))
// Resume at the starting item's nesting level. An enclosing } is also a
// recovery boundary when an inner ( or [ was never closed.
void parser_recover(Parser *p, size_t start, int enclosed);
int parser_separator(Parser *p, TokenType end);

// nesting.c: the syntax nesting limit. parser_enter increments p->depth;
// the caller decrements it after the nested production.
int parser_enter(Parser *p);
Expr *parser_checked_expr(Parser *p, Expr *e);
Type *parser_checked_type(Parser *p, Type *t);

// names.c: names, paths and symbol heads.

int parser_expect_identifier(Parser *p, const char *message);
int parser_primitive_member_name(TokenType type);
int parser_expect_member_name(Parser *p, const char *message);
int parser_type_name_token(TokenType t);
const char *parser_check_name_case(Parser *p, Token token, size_t mark,
                                   TokenType wanted, const char *message);
void parser_check_function_name(Parser *p, Token token, size_t mark,
                                int in_member);
const char *parser_path(Parser *p, int qualifier);
const char *parser_join_path(Parser *p, const char *left, const char *right);
int parser_typed_binding_elsewhere(Parser *p, int name);

// `:tag`, `:!tag` or `Mod.:tag`, shared by types, expressions and patterns.
typedef struct {
  const char *module;
  Span module_span;
  SymbolKind kind;
  const char *tag;
  Span tag_span;
  Token leaf, tag_token;
  size_t module_mark, tag_mark;
} SymbolHead;
int parser_at_symbol(Parser *p);
int parser_symbol_head(Parser *p, SymbolHead *head, const char *message);
// Called once the payload list is complete, so syntax errors stay primary.
void parser_check_symbol_head(Parser *p, const SymbolHead *head);

// Grammar productions.

Type *parse_type(Parser *p);
int parse_type_arguments(Parser *p, List *args);
Expr *parse_primary(Parser *p, int allow_struct);
Expr *parse_expression(Parser *p, int minimum, int allow_struct);
int parse_simple_item(Parser *p, BlockItem *item);
Expr *parse_match_block(Parser *p);
Expr *parse_block(Parser *p);
Decl *parse_binding(Parser *p, Visibility visibility);
Stmt *parse_control_statement(Parser *p);

#endif
