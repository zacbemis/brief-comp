#ifndef BRIEF_IR_H
#define BRIEF_IR_H
#include "sema/sema.h"
#define IR_NO_VALUE SIZE_MAX
typedef enum {
  IR_STRING,
  IR_CONSTANT,
  IR_CALL,
  IR_RETURN,
  IR_LOAD,
  IR_STORE,
  IR_COPY,
  IR_UNARY,
  IR_BINARY,
  IR_LABEL,
  IR_JUMP,
  IR_BRANCH_FALSE,
  IR_UNREACHABLE,
  IR_NEW,
  IR_FIELD_LOAD,
  IR_FIELD_STORE,
  IR_RETAIN,
  IR_DROP_TEMP,
  IR_DROP_VAR,
  IR_DROP_FIELD,
  IR_SYMBOL,      // Pack tag/payload a, then pin through the ownership pass.
  IR_SYMBOL_CAST, // Repack a symbol set without changing its global tag.
  IR_SYMBOL_TAG,
  IR_SYMBOL_PAYLOAD,
  IR_PANIC_SYMBOL
} IrOp;
typedef struct {
  IrOp op;
  Span span;
  const SemaType
      *type; // Operand type for operators; result type for constants.
  // Dense temporary IDs. LOAD reads variable a; STORE writes variable a from b.
  // COPY writes result from a; RETURN uses a (or IR_NO_VALUE). Field operations
  // use object temporary a and field metadata; FIELD_STORE stores b.
  size_t result, a, b, label;
  SemaField *field;
  SemaTag *tag;
  union {
    struct {
      const unsigned char *bytes;
      size_t length;
    } string;
    struct {
      uint64_t integer;
      double real;
      int negative;
    } constant;
    struct {
      int builtin;
      size_t function, count;
      size_t *args; // Unit arguments have no representation; effects remain.
    } call; // NEW uses type->structure and declaration-ordered field args.
    UnaryOp unary;
    BinaryOp binary;
  } as;
} IrInstruction;
typedef struct {
  Span span;
  const char *name;
  int method;
  const SemaType *return_type;
  const SemaType **variables, **temporary_types;
  size_t variable_count, parameter_count;
  IrInstruction *instructions;
  size_t count, capacity, temporaries, temporary_capacity, labels;
} IrFunction;
typedef struct {
  IrFunction *functions;
  size_t count, entry;
  const SemaStruct *structs;
  size_t struct_count;
  const SemaSymSet *symbol_sets;
  SemaTag *ok;
  const SemaType *entry_outcome;
  int ownership_inserted;
} IrProgram;
// Requires successful resolution and checking. NULL for unchecked input.
// Scope/exit drops are explicit; reference reads/stores still need the RC pass.
IrProgram *ir_lower(Arena *arena, const SemaProgram *program);
// Independent, idempotent ownership pass. Parameters borrow, produced
// references own, stores retain before replacing, and returns transfer one
// owned reference.
void ir_insert_rc(Arena *arena, IrProgram *program);
#endif
