#include "ir/ir.h"

#include <string.h>

typedef struct {
  Arena *arena;
  IrFunction *function;
  int returned;
} Lower;
static void emit(Lower *l, IrInstruction instruction) {
  IrFunction *f = l->function;
  if (f->count == f->capacity) {
    size_t capacity = f->capacity ? f->capacity * 2 : 16;
    IrInstruction *items = arena_calloc(l->arena, capacity, sizeof(*items));
    if (f->count)
      memcpy(items, f->instructions, f->count * sizeof(*items));
    f->instructions = items;
    f->capacity = capacity;
  }
  f->instructions[f->count++] = instruction;
}
static size_t lower(Lower *l, const SemaExpr *e) {
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    return IR_NO_VALUE;
  case SEMA_EXPR_STRING: {
    Token t = e->as.string;
    unsigned char *bytes = arena_alloc(l->arena, t.length);
    size_t length = 0;
    for (size_t i = 1; i + 1 < t.length; i++) {
      unsigned char c = (unsigned char)t.start[i];
      if (c == '\\') {
        c = (unsigned char)t.start[++i];
        switch (c) {
        case 'n':
          c = '\n';
          break;
        case 't':
          c = '\t';
          break;
        case '0':
          c = 0;
          break;
        default:
          break; // Lexer already validated \\, \" and \'.
        }
      }
      bytes[length++] = c;
    }
    bytes[length] = 0;
    size_t result = l->function->temporaries++;
    IrInstruction instruction = {.op = IR_STRING, .span = e->span};
    instruction.as.string.result = result;
    instruction.as.string.bytes = bytes;
    instruction.as.string.length = length;
    emit(l, instruction);
    return result;
  }
  case SEMA_EXPR_CALL: {
    size_t argument = IR_NO_VALUE;
    for (size_t i = 0; i < e->as.call.count; i++)
      argument = lower(l, e->as.call.args[i]);
    if (l->returned)
      return IR_NO_VALUE;
    IrInstruction instruction = {.op = IR_CALL, .span = e->span};
    instruction.as.call.builtin = e->as.call.target->builtin;
    instruction.as.call.function = e->as.call.target->id;
    instruction.as.call.argument = argument;
    emit(l, instruction);
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_BLOCK: {
    size_t value = IR_NO_VALUE;
    for (size_t i = 0; i < e->as.block.count && !l->returned; i++)
      value = lower(l, e->as.block.items[i]);
    return value;
  }
  case SEMA_EXPR_RETURN:
    if (e->as.value)
      lower(l, e->as.value);
    if (!l->returned)
      emit(l, (IrInstruction){.op = IR_RETURN, .span = e->span});
    l->returned = 1;
    return IR_NO_VALUE;
  case SEMA_EXPR_ERROR:
    return IR_NO_VALUE; // Not reachable in checked input.
  }
  return IR_NO_VALUE;
}
IrProgram *ir_lower(Arena *arena, const SemaProgram *program) {
  if (!program->checked)
    return NULL;
  IrProgram *p = arena_calloc(arena, 1, sizeof(*p));
  p->count = program->count;
  p->entry = program->entry->id;
  p->functions = arena_calloc(arena, p->count, sizeof(IrFunction));
  for (size_t i = 0; i < p->count; i++) {
    IrFunction *f = &p->functions[i];
    f->name = program->functions[i].symbol.name;
    f->span = program->functions[i].symbol.span;
    Lower l = {arena, f, 0};
    lower(&l, program->functions[i].body);
    if (!l.returned)
      emit(&l, (IrInstruction){.op = IR_RETURN, .span = f->span});
  }
  return p;
}
