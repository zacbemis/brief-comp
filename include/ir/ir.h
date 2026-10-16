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
  IR_UNREACHABLE
} IrOp;
typedef struct {
  IrOp op;
  Span span;
  SemaTypeKind type; // Operand type for operators; literal type for constants.
  // Dense temporary IDs. LOAD reads variable a; STORE writes variable a from
  // temporary b. COPY writes result from a; RETURN uses a (or IR_NO_VALUE).
  // Branch/jump destinations are function-local label IDs.
  size_t result, a, b, label;
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
      size_t
          *args; // Unit arguments are IR_NO_VALUE; effects already sequenced.
    } call;
    UnaryOp unary;
    BinaryOp binary;
  } as;
} IrInstruction;
typedef struct {
  Span span;
  const char *name;
  SemaTypeKind return_type;
  SemaTypeKind *variables, *temporary_types;
  size_t variable_count, parameter_count;
  IrInstruction *instructions;
  size_t count, capacity, temporaries, temporary_capacity, labels;
} IrFunction;
typedef struct {
  IrFunction *functions;
  size_t count, entry;
} IrProgram;
// Requires successful resolution and checking. NULL for unchecked input.
IrProgram *ir_lower(Arena *arena, const SemaProgram *program);
#endif
