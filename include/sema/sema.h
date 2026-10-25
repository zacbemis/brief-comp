#ifndef BRIEF_SEMA_H
#define BRIEF_SEMA_H

#include "ast/ast.h"
#include "common/diagnostic.h"
#include "common/name_map.h"
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
  SEMA_SYM,
  SEMA_TYPE_COUNT
} SemaTypeKind;
typedef struct SemaStruct SemaStruct;
typedef struct SemaSymSet SemaSymSet;
typedef struct {
  SemaTypeKind kind;
  const char *name;
  unsigned bits;
  int is_signed;
  SemaStruct *structure;
  SemaSymSet *symbols;
} SemaType;
typedef struct SemaTag SemaTag;
struct SemaTag {
  const char *name;
  size_t id; // Program-global; zero is reserved for empty ownership slots.
  SemaTag *next;
};
typedef struct {
  SemaTag *tag;
  const SemaType *payload; // NULL means no payload; () is distinct.
} SemaVariant;
struct SemaSymSet {
  SemaType type;
  SemaVariant *variants; // Canonical, sorted by global tag ID.
  size_t count, id;
  int equatable;
  SemaSymSet *next; // Creation order: nested value types precede their users.
};
typedef struct {
  const Decl *syntax;
  const SemaType *type;
} SemaAlias;
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
enum { SEMA_BUILTIN_PRINT = 1, SEMA_BUILTIN_STR = 2, SEMA_BUILTIN_CONVERT = 3 };
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
typedef struct SemaSymbol SemaSymbol;
struct SemaSymbol {
  const char *name;
  Span span;
  size_t id, arity;
  int builtin;
  SemaVar **params;
  const SemaType *return_type, *outcome_type; // ABI type and full outcome set.
  SemaStruct *owner;       // NULL for a top-level function/builtin.
  SemaSymbol *next_method; // Next method of any type with this name.
};
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
  SEMA_EXPR_STRUCT,
  SEMA_EXPR_MATCH,
  SEMA_EXPR_SYMBOL,
  SEMA_EXPR_TRY,
  SEMA_EXPR_PANIC
} SemaExprKind;
enum { SEMA_NEXT = 1, SEMA_RETURN = 2, SEMA_BREAK = 4, SEMA_CONTINUE = 8 };
typedef struct SemaExpr SemaExpr;
typedef struct {
  PatternKind kind;
  Span span;
  SemaVar *binding;
  SemaExpr *lower, *upper, *body;
  BinaryOp comparison;
  // Inclusive, order-preserving scalar ranks, shared by coverage and lowering.
  uint64_t first, last;
  SemaExpr *first_value, *last_value;
  int any, useful, invalid;
  SemaTag *tag;
  int payload;
  const SemaType *set; // Named-set pattern, or narrowed catch-all binding type.
  const SemaType *cases; // Useful symbol cases; shared with lowering.
} SemaArm;
struct SemaExpr {
  SemaExprKind kind;
  Span span;
  const SemaType *type, *raw_type, *failures;
  int outcome; // A direct call (or its pipeline result) has implicit :ok.
  int adapt;   // 1 packs implicit success, 2 extracts an only-:ok payload.
  unsigned flow;
  int reachable;
  int returns;      // No fallthrough (including break/continue).
  uint64_t integer; // Literal magnitude/bool/character; sign is separate.
  double real;
  int negative;
  union {
    Token string; // Spelling of number/character/bool literals.
    struct {
      // Decoded bytes; embedded NULs count toward length.
      const unsigned char *bytes;
      size_t length;
    } text;
    SemaVar *variable;
    struct {
      SemaSymbol *target;
      SemaExpr **args;
      size_t count;
      const char *method; // Instance calls include the receiver as args[0].
      int pipeline;       // Unqualified pipeline lookup: method before module
                          // function.
      int shadowed, handled;
      SemaSymbol *fallback; // Pipeline fallback; dotted calls use it only as
                            // a diagnostic candidate, never a call target.
    } call;
    struct {
      SemaExpr **items;
      size_t count;
      int pipeline;
    } block;
    struct {
      SemaVar *variable;
      SemaExpr *value;
      AssignOp op;
      SemaExpr *target; // Field lvalue; NULL for binding/local assignment.
      SemaExpr *stage;  // Hidden pipeline binding: supplies argument context.
      int outcome;      // Preserve symbol input for a match/postfix handler.
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
      const SemaType *common; // Symbol equality may widen both operands.
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
    struct {
      SemaExpr *input;
      SemaArm *arms;
      size_t count;
    } match;
    struct {
      SemaTag *tag;
      SemaExpr *payload;
    } symbol;
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
  int written, partial;
  const SemaType *written_type;
  int inference_conflict; // An intrinsic conflict, not a propagated error type.
  int inference_reported; // A specific/fallback inference diagnostic was
                          // emitted.
  SemaEdge *edges;
  size_t component;
} SemaFunction;
typedef struct {
  Arena *arena;
  SemaType types[SEMA_TYPE_COUNT]; // Primitive singletons; structs and symbol
                                   // sets use their own canonical types.
  SemaSymbol print, stringify, converters[SEMA_DUB - SEMA_SHRT + 1];
  SemaStruct *structs;
  size_t struct_count;
  SemaAlias *aliases;
  size_t alias_count;
  SemaTag *tags, *ok;
  size_t tag_count;
  SemaSymSet *symbol_sets, *last_symbol_set;
  size_t symbol_set_count;
  // Canonical sets by content (open addressing), and reusable working
  // storage for building candidate variant lists before interning them.
  SemaSymSet **set_table;
  size_t set_table_capacity;
  SemaVariant *scratch;
  size_t scratch_capacity;
  SemaFunction *functions;
  size_t count;
  SemaSymbol *entry;
  // Declarations by name. function_names holds top-level functions under a
  // NULL owner and methods under their struct. method_names maps a method
  // name to its chain of methods on any type, linked in declaration order
  // through next_method.
  NameMap struct_names, alias_names, tag_names, function_names, method_names;
  int resolved, checked;
} SemaProgram;

int sema_integer(const SemaType *type); // Excludes chr.
int sema_number(const SemaType *type);
// Outcome depends on source/target domains, never on a particular value.
const SemaType *sema_conversion(SemaProgram *program, const SemaType *source,
                                const SemaType *target);
// Includes value-represented symbols: their active payload needs ARC handling.
int sema_reference(const SemaType *type);
const SemaVariant *sema_ok(const SemaType *type);
const SemaType *sema_success(SemaProgram *program, const SemaType *payload);
const SemaType *sema_result(SemaProgram *program, const SemaType *type);
const SemaType *sema_failures(SemaProgram *program, const SemaType *type);
int sema_equatable(const SemaType *type);
void sema_update_equatable(SemaProgram *program);
int sema_assignable(const SemaType *actual, const SemaType *wanted);
SemaTag *sema_tag(SemaProgram *program, const char *name);
const SemaVariant *sema_variant(const SemaType *type, const SemaTag *tag);
// Interns a set; NULL indicates conflicting payloads. No empty sets are made.
const SemaType *sema_symbol_set(SemaProgram *program,
                                const SemaVariant *variants, size_t count);
const SemaType *sema_symbol_union(SemaProgram *program, const SemaType *a,
                                  const SemaType *b);
void sema_symbol_coverage(SemaProgram *program, SemaExpr *match,
                          Diagnostics *errors);
SemaField *sema_field(const SemaType *type, const char *name);
SemaSymbol *sema_method(SemaProgram *program, const SemaType *type,
                        const char *name);
SemaProgram *sema_resolve(Arena *arena, const Program *syntax,
                          Diagnostics *errors);
void sema_check(SemaProgram *program, Diagnostics *errors);
// Intraprocedural read-only alias propagation, after types/flow stabilize.
// Called by sema_check; does not alter canonical types or ownership.
void sema_check_permissions(SemaProgram *program, Diagnostics *errors);
// Scalar normalization / symbol coverage after endpoint and input typing.
// NULL diagnostics probes coverage without emitting errors.
void sema_match_coverage(SemaProgram *program, SemaExpr *match,
                         Diagnostics *errors);
// Iterative Tarjan; components are numbered in callee-first order.
size_t sema_components(SemaProgram *program);

#endif
