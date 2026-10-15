#ifndef AST_H
#define AST_H

#include "ast/decl.h"
#include "ast/expr.h"
#include "ast/pattern.h"
#include "ast/stmt.h"
#include "ast/type.h"
#include "lexer/token.h"
#include <stdio.h>

// All nodes, lists, and interned names live in the compilation arena. Literal
// tokens keep their source spelling: decoding and numeric conversion belong
// to later stages. Types here are syntax, not canonical semantic types.
typedef struct {
  Span span;
  const char *tag; // NULL includes a named set (or TYPE_INFER).
  Type *type;      // NULL for a tag with no payload.
} SymEntry;

struct Type {
  TypeKind kind;
  Span span;
  unsigned depth; // Parser-computed tree depth, for the syntax nesting limit.
  union {
    const char *name;
    struct {
      Type *base;
      Type **args;
      size_t count;
    } apply;
    struct {
      Type *element;
      Token size;
    } array;
    struct {
      SymEntry *items;
      size_t count;
    } sym;
  } as;
};

typedef struct {
  Span span;
  const char *name;
  Type *type;
  BorrowKind borrow;
  Visibility visibility;
} Parameter;

typedef struct {
  Span span;
  const char *name;
  Expr *value;
} FieldValue;

typedef struct {
  Span span;
  Pattern *pattern;
  Expr *value;
} MatchArm;

typedef struct BlockItem BlockItem;
typedef enum {
  BLOCK_ITEM_DECL,
  BLOCK_ITEM_STMT,
  BLOCK_ITEM_EXPR,
} BlockItemKind;

struct BlockItem {
  BlockItemKind kind;
  Span span;
  union {
    Decl *decl;
    Stmt *stmt;
    Expr *expr;
  } as;
};

struct Expr {
  ExprKind kind;
  Span span;
  unsigned depth; // Includes left-associated chains, not just parser recursion.
  union {
    const char *name;
    Token literal;
    struct {
      const char *tag;
      Expr *payload;
    } symbol;
    struct {
      Expr **items;
      size_t count;
    } array;
    struct {
      Expr *type;
      FieldValue *fields;
      size_t count;
    } structure;
    struct {
      UnaryOp op;
      Expr *value;
    } unary;
    struct {
      BinaryOp op;
      Expr *left;
      Expr *right;
    } binary;
    struct {
      Expr *callee;
      Type **types;
      size_t type_count;
      Expr **args;
      size_t count;
    } call;
    struct {
      Expr *object;
      const char *name;
    } field;
    struct {
      Expr *object;
      Expr *index;
    } index;
    struct {
      BlockItem *items;
      size_t count;
    } block;
    struct {
      Expr *condition;
      Expr *then_branch;
      Expr *else_branch;
    } conditional;
    struct {
      const char *name;
      Expr *iterable;
      Expr *body;
    } for_loop;
    struct {
      Expr *condition;
      Expr *body;
    } while_loop;
    struct {
      MatchArm *arms;
      size_t count;
    } match;
    Expr *postfix; // TRY / PANIC
    struct {
      Expr *value;
      Expr *stage;
    } pipe;
  } as;
};

struct Stmt {
  StmtKind kind;
  Span span;
  union {
    Expr *value; // RET (optional) / EXPR
    struct {
      AssignOp op;
      Expr *target;
      Expr *value;
    } assign;
  } as;
};

struct Pattern {
  PatternKind kind;
  Span span;
  union {
    // An unqualified name is syntactically ambiguous: resolution may turn a
    // PAT_BIND into a reference to a named symbol set. Do not guess by case.
    const char *name;
    Expr *literal;
    struct {
      const char *tag;
      Pattern *payload;
    } symbol;
    struct {
      Expr *lower;
      Expr *upper;
    } range;
    struct {
      BinaryOp op;
      Expr *value;
    } compare;
  } as;
};

struct Decl {
  DeclKind kind;
  Span span;
  Visibility visibility;
  const char *name;
  union {
    struct {
      BindingKind kind;
      Type *annotation;
      Expr *value;
    } binding;
    struct {
      Parameter *params;
      size_t count;
      Type *return_type; // NULL means inferred.
      Expr *body;
    } function;
    struct {
      Parameter *fields;
      size_t count;
    } structure;
    Type *alias;
    struct {
      Type *target;
      Decl **methods;
      size_t count;
    } impl;
  } as;
};

typedef struct {
  Span span;
  Decl **items;
  size_t count;
} Program;

// Stable, single-line S-expressions followed by one newline. No addresses or
// arena-dependent details appear in the dump.
void ast_print(FILE *output, const Program *program);

#endif
