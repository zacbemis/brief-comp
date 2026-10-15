#ifndef BRIEF_SEMA_H
#define BRIEF_SEMA_H

#include "ast/ast.h"
#include "common/diagnostic.h"

typedef enum { SEMA_UNIT, SEMA_STR, SEMA_ERROR } SemaTypeKind;
typedef struct {
  SemaTypeKind kind;
} SemaType;

typedef struct {
  const char *name;
  Span span;
  size_t id;
  size_t arity;
  int builtin;
  const SemaType *return_type;
} SemaSymbol;

typedef enum {
  SEMA_EXPR_UNIT,
  SEMA_EXPR_STRING,
  SEMA_EXPR_CALL,
  SEMA_EXPR_BLOCK,
  SEMA_EXPR_RETURN,
  SEMA_EXPR_ERROR
} SemaExprKind;
typedef struct SemaExpr SemaExpr;
struct SemaExpr {
  SemaExprKind kind;
  Span span;
  const SemaType *type; // NULL until checking; canonical within this program.
  int returns; // This expression unconditionally returns from its function.
  union {
    Token string;
    struct {
      SemaSymbol *target;
      SemaExpr **args;
      size_t count;
    } call;
    struct {
      SemaExpr **items;
      size_t count;
    } block;
    SemaExpr *value;
  } as;
};
typedef struct {
  SemaSymbol symbol;
  const Decl *syntax;
  SemaExpr *body;
  unsigned visit; // Temporary call-graph traversal state.
} SemaFunction;
typedef struct {
  SemaType unit_type, str_type, error_type;
  SemaSymbol print;
  SemaFunction *functions;
  size_t count;
  SemaSymbol *entry;
  int resolved;
  int checked;
} SemaProgram;

// Separate stages, both arena-owned. Resolution collects the function table
// before walking bodies, allowing forward calls. M2 supports only zero-argument
// unit-returning functions and the print(str) builtin; other syntax is
// diagnosed.
SemaProgram *sema_resolve(Arena *arena, const Program *syntax,
                          Diagnostics *errors);
void sema_check(SemaProgram *program, Diagnostics *errors);

#endif
