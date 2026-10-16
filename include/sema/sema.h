#ifndef BRIEF_SEMA_H
#define BRIEF_SEMA_H

#include "ast/ast.h"
#include "common/diagnostic.h"
#include <stdint.h>

typedef enum {
  SEMA_UNKNOWN,
  SEMA_NEVER,
  SEMA_ERROR,
  SEMA_UNIT,
  SEMA_BOOL,
  SEMA_CHR,
  SEMA_SHRT,
  SEMA_USHRT,
  SEMA_INT,
  SEMA_UINT,
  SEMA_LNG,
  SEMA_ULNG,
  SEMA_FLT,
  SEMA_DUB,
  SEMA_STR,
  SEMA_TYPE_COUNT
} SemaTypeKind;
typedef struct {
  SemaTypeKind kind;
  const char *name;
  unsigned bits;
  int is_signed;
} SemaType;
typedef struct SemaVar SemaVar;
struct SemaVar {
  const char *name;
  Span span;
  size_t id;
  int mutable;
  const SemaType *annotation, *type;
};
typedef struct {
  const char *name;
  Span span;
  size_t id, arity;
  int builtin;
  SemaVar **params;
  const SemaType *return_type;
} SemaSymbol;
typedef enum {
  SEMA_EXPR_UNIT,
  SEMA_EXPR_STRING,
  SEMA_EXPR_CALL,
  SEMA_EXPR_BLOCK,
  SEMA_EXPR_RETURN,
  SEMA_EXPR_ERROR,
  SEMA_EXPR_INT,
  SEMA_EXPR_FLOAT,
  SEMA_EXPR_BOOL,
  SEMA_EXPR_CHAR,
  SEMA_EXPR_VAR,
  SEMA_EXPR_BIND,
  SEMA_EXPR_ASSIGN,
  SEMA_EXPR_UPDATE, // Valueless ++/--; binding.op is ASSIGN_ADD/ASSIGN_SUB.
  SEMA_EXPR_UNARY,
  SEMA_EXPR_BINARY,
  SEMA_EXPR_IF,
  SEMA_EXPR_WHILE,
  SEMA_EXPR_FOR,
  SEMA_EXPR_BREAK,
  SEMA_EXPR_CONTINUE
} SemaExprKind;
enum { SEMA_NEXT = 1, SEMA_RETURN = 2, SEMA_BREAK = 4, SEMA_CONTINUE = 8 };
typedef struct SemaExpr SemaExpr;
struct SemaExpr {
  SemaExprKind kind;
  Span span;
  const SemaType *type;
  unsigned flow;
  int reachable;
  int returns;      // No fallthrough (including break/continue).
  uint64_t integer; // Literal magnitude/bool/character; sign is separate.
  double real;
  int negative;
  union {
    Token string; // Also the spelling of number/character/bool literals.
    SemaVar *variable;
    struct {
      SemaSymbol *target;
      SemaExpr **args;
      size_t count;
    } call;
    struct {
      SemaExpr **items;
      size_t count;
    } block;
    struct {
      SemaVar *variable;
      SemaExpr *value;
      AssignOp op;
    } binding;
    struct {
      UnaryOp op;
      SemaExpr *value;
    } unary;
    struct {
      BinaryOp op;
      SemaExpr *left, *right;
    } binary;
    struct {
      SemaExpr *condition, *yes, *no;
    } conditional;
    struct {
      SemaExpr *condition, *body;
    } while_loop;
    struct {
      SemaVar *variable;
      SemaExpr *lower, *upper, *body;
    } for_loop;
    SemaExpr *value;
  } as;
};
typedef struct SemaEdge SemaEdge;
struct SemaEdge {
  size_t callee;
  SemaEdge *next;
};
typedef struct {
  SemaSymbol symbol;
  const Decl *syntax;
  SemaExpr *body;
  SemaVar **variables;
  size_t variable_count, variable_capacity;
  int written;
  int inference_conflict; // An intrinsic conflict, not a propagated error type.
  int inference_reported; // A specific/fallback inference diagnostic was
                          // emitted.
  SemaEdge *edges;
  size_t component;
} SemaFunction;
typedef struct {
  Arena *arena;
  SemaType types[SEMA_TYPE_COUNT];
  SemaSymbol print;
  SemaFunction *functions;
  size_t count;
  SemaSymbol *entry;
  int resolved, checked;
} SemaProgram;

int sema_integer(const SemaType *type); // Excludes chr.
int sema_number(const SemaType *type);
SemaProgram *sema_resolve(Arena *arena, const Program *syntax,
                          Diagnostics *errors);
void sema_check(SemaProgram *program, Diagnostics *errors);
// Iterative Tarjan; components are numbered in callee-first order.
size_t sema_components(SemaProgram *program);

#endif
