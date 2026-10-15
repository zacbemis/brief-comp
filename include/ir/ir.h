#ifndef BRIEF_IR_H
#define BRIEF_IR_H

#include "sema/sema.h"
#include <stdint.h>

#define IR_NO_VALUE SIZE_MAX
typedef enum { IR_STRING, IR_CALL, IR_RETURN } IrOp;
typedef struct {
  IrOp op;
  Span span;
  union {
    struct {
      size_t result;
      const unsigned char *bytes;
      size_t length;
    } string;
    struct {
      int builtin;
      size_t function;
      size_t argument;
    } call;
  } as;
} IrInstruction;
typedef struct {
  Span span;
  const char *name;
  IrInstruction *instructions;
  size_t count;
  size_t capacity;
  size_t temporaries;
} IrFunction;
typedef struct {
  IrFunction *functions;
  size_t count;
  size_t entry;
} IrProgram;

// Requires successful resolution and checking. NULL for unchecked input.
IrProgram *ir_lower(Arena *arena, const SemaProgram *program);

#endif
