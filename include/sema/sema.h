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
  SEMA_STRUCT,
  SEMA_TYPE_COUNT
} SemaTypeKind;
typedef struct SemaStruct SemaStruct;
typedef struct {
  SemaTypeKind kind;
  const char *name;
  unsigned bits;
  int is_signed;
  SemaStruct *structure;
} SemaType;
typedef struct {
  const char *name;
  Span span;
  size_t id;
  const SemaType *type;
} SemaField;
struct SemaStruct {
  SemaType type;
  const Decl *syntax;
  SemaField *fields;
  size_t count, id;
  int equatable;
};
enum { SEMA_BUILTIN_PRINT = 1, SEMA_BUILTIN_STR = 2 };
typedef struct SemaVar SemaVar;
struct SemaVar {
  const char *name;
  Span span;
  size_t id;
  int mutable;
  int data_mutable; // Parameter access permission; local permissions are
                    // flow-checked.
  const SemaType *annotation, *type;
};
typedef struct {
  const char *name;
  Span span;
  size_t id, arity;
  int builtin;
  SemaVar **params;
  const SemaType *return_type;
  SemaStruct *owner; // NULL for a top-level function/builtin.
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
  SEMA_EXPR_CONTINUE,
  SEMA_EXPR_FIELD,
  SEMA_EXPR_STRUCT
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
      const char *method; // Instance calls include the receiver as args[0].
    } call;
    struct {
      SemaExpr **items;
      size_t count;
    } block;
    struct {
      SemaVar *variable;
      SemaExpr *value;
      AssignOp op;
      SemaExpr *target; // Field lvalue; NULL for binding/local assignment.
    } binding;
    struct {
      SemaExpr *object;
      const char *name;
      SemaField *field;
    } field;
    struct {
      SemaStruct *type;
      SemaExpr **values;
      SemaField **fields;
      size_t count;
    } structure;
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
  SemaType types[SEMA_TYPE_COUNT]; // Primitive singletons; structs use their
                                   // own type.
  SemaSymbol print, stringify;
  SemaStruct *structs;
  size_t struct_count;
  SemaFunction *functions;
  size_t count;
  SemaSymbol *entry;
  int resolved, checked;
} SemaProgram;

int sema_integer(const SemaType *type); // Excludes chr.
int sema_number(const SemaType *type);
int sema_reference(const SemaType *type);
SemaField *sema_field(const SemaType *type, const char *name);
SemaSymbol *sema_method(SemaProgram *program, const SemaType *type,
                        const char *name);
SemaProgram *sema_resolve(Arena *arena, const Program *syntax,
                          Diagnostics *errors);
void sema_check(SemaProgram *program, Diagnostics *errors);
// Intraprocedural read-only alias propagation, after types/flow stabilize.
// Called by sema_check; does not alter canonical types or ownership.
void sema_check_permissions(SemaProgram *program, Diagnostics *errors);
// Iterative Tarjan; components are numbered in callee-first order.
size_t sema_components(SemaProgram *program);

#endif
