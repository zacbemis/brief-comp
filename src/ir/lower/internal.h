#ifndef BRIEF_IR_LOWER_INTERNAL_H
#define BRIEF_IR_LOWER_INTERNAL_H

#include "ir/ir.h"

// Values owned by the current lexical region, dropped on every exit from it.
typedef struct Owned Owned;
struct Owned {
  size_t id;
  int variable;
  Owned *next;
};
typedef struct Loop Loop;
struct Loop {
  size_t exit, next;
  Loop *outer;
  int continue_used;
  Owned *saved;
};
typedef struct {
  Arena *arena;
  const SemaProgram *program;
  IrFunction *function;
  int returned; // The current block has no fallthrough.
  Loop *loop;
  Owned *owned;
} Lower;

// builder.c: instruction emission for the current function.

void lower_emit(Lower *l, IrInstruction instruction);
void lower_own(Lower *l, size_t id, int variable);
// Drops owned values back to `until`, keeping `except` (a returned temporary).
void lower_cleanup(Lower *l, Owned *until, size_t except, Span span);
// IR_NO_VALUE for unit and never; references are owned by the current region.
size_t lower_temporary(Lower *l, const SemaType *type);
void lower_jump(Lower *l, size_t target, Span span);
void lower_label(Lower *l, size_t id, Span span);
size_t lower_constant(Lower *l, const SemaType *type, uint64_t integer,
                      double real, int negative, Span span);
size_t lower_binary(Lower *l, BinaryOp op, const SemaType *type,
                    const SemaType *result_type, size_t a, size_t b, Span span);
size_t lower_pack(Lower *l, const SemaType *type, SemaTag *tag,
                  const size_t *args, size_t count, Span span);
// Wraps implicit :ok and widens or narrows symbol sets to the target type.
size_t lower_convert(Lower *l, size_t value, const SemaType *target, Span span);
size_t lower_load(Lower *l, SemaVar *v, size_t object, SemaField *field,
                  Span span);
void lower_store(Lower *l, SemaVar *v, size_t object, SemaField *field,
                 size_t value, Span span);
// Tests a symbol tag temporary against several tags, ORing the results.
size_t lower_tag_test(Lower *l, size_t tag, const SemaVariant *variants,
                      size_t count, Span span);

// Expression lowering. Each returns the result temporary or IR_NO_VALUE.

size_t lower_expr(Lower *l, const SemaExpr *e);
size_t lower_try(Lower *l, const SemaExpr *e);
size_t lower_match(Lower *l, const SemaExpr *e);
size_t lower_if(Lower *l, const SemaExpr *e);
size_t lower_while(Lower *l, const SemaExpr *e);
size_t lower_for(Lower *l, const SemaExpr *e);

#endif
