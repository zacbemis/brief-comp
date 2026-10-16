#include "ir/ir.h"
#include <string.h>

typedef struct Loop Loop;
struct Loop {
  size_t exit, next;
  Loop *outer;
  int continue_used;
};
typedef struct {
  Arena *arena;
  IrFunction *function;
  int returned;
  Loop *loop;
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
static size_t temporary(Lower *l, SemaTypeKind type) {
  if (type == SEMA_UNIT || type == SEMA_NEVER)
    return IR_NO_VALUE;
  IrFunction *f = l->function;
  if (f->temporaries == f->temporary_capacity) {
    size_t capacity = f->temporary_capacity ? f->temporary_capacity * 2 : 16;
    SemaTypeKind *items = arena_calloc(l->arena, capacity, sizeof(*items));
    if (f->temporaries)
      memcpy(items, f->temporary_types, f->temporaries * sizeof(*items));
    f->temporary_types = items;
    f->temporary_capacity = capacity;
  }
  f->temporary_types[f->temporaries] = type;
  return f->temporaries++;
}
static void jump(Lower *l, size_t label, Span span) {
  emit(l, (IrInstruction){.op = IR_JUMP, .span = span, .label = label});
  l->returned = 1;
}
static void label(Lower *l, size_t id, Span span) {
  emit(l, (IrInstruction){.op = IR_LABEL, .span = span, .label = id});
  l->returned = 0;
}
static size_t constant(Lower *l, SemaTypeKind type, uint64_t integer,
                       double real, int negative, Span span) {
  size_t result = temporary(l, type);
  IrInstruction in = {
      .op = IR_CONSTANT, .span = span, .type = type, .result = result};
  in.as.constant.integer = integer;
  in.as.constant.real = real;
  in.as.constant.negative = negative;
  emit(l, in);
  return result;
}
static size_t binary(Lower *l, BinaryOp op, SemaTypeKind type,
                     SemaTypeKind result_type, size_t a, size_t b, Span span) {
  size_t result = temporary(l, result_type);
  IrInstruction in = {.op = IR_BINARY,
                      .span = span,
                      .type = type,
                      .result = result,
                      .a = a,
                      .b = b};
  in.as.binary = op;
  emit(l, in);
  return result;
}
static size_t lower(Lower *l, const SemaExpr *e) {
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    return IR_NO_VALUE;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
  case SEMA_EXPR_BOOL:
  case SEMA_EXPR_CHAR:
    return constant(l, e->type->kind, e->integer, e->real, e->negative,
                    e->span);
  case SEMA_EXPR_STRING: {
    Token t = e->as.string;
    unsigned char *bytes = arena_alloc(l->arena, t.length);
    size_t length = 0;
    for (size_t i = 1; i + 1 < t.length; i++) {
      unsigned char c = (unsigned char)t.start[i];
      if (c == '\\') {
        c = (unsigned char)t.start[++i];
        if (c == 'n')
          c = '\n';
        else if (c == 't')
          c = '\t';
        else if (c == '0')
          c = 0;
      }
      bytes[length++] = c;
    }
    bytes[length] = 0;
    size_t result = temporary(l, SEMA_STR);
    IrInstruction in = {.op = IR_STRING, .span = e->span, .result = result};
    in.as.string.bytes = bytes;
    in.as.string.length = length;
    emit(l, in);
    return result;
  }
  case SEMA_EXPR_VAR: {
    size_t result = temporary(l, e->type->kind);
    if (result != IR_NO_VALUE)
      emit(l, (IrInstruction){.op = IR_LOAD,
                              .span = e->span,
                              .result = result,
                              .a = e->as.variable->id});
    return result;
  }
  case SEMA_EXPR_BIND:
  case SEMA_EXPR_ASSIGN: {
    SemaVar *v = e->as.binding.variable;
    size_t old = IR_NO_VALUE;
    if (e->kind == SEMA_EXPR_ASSIGN && e->as.binding.op != ASSIGN_SET) {
      old = temporary(l, v->type->kind);
      emit(l, (IrInstruction){
                  .op = IR_LOAD, .span = e->span, .result = old, .a = v->id});
    }
    size_t value = lower(l, e->as.binding.value);
    if (l->returned)
      return IR_NO_VALUE;
    if (old != IR_NO_VALUE)
      value = binary(l, (BinaryOp)(BINARY_ADD + e->as.binding.op - ASSIGN_ADD),
                     v->type->kind, v->type->kind, old, value, e->span);
    if (value != IR_NO_VALUE)
      emit(l, (IrInstruction){
                  .op = IR_STORE, .span = e->span, .a = v->id, .b = value});
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_UPDATE: {
    SemaVar *v = e->as.binding.variable;
    size_t old = temporary(l, v->type->kind);
    emit(l, (IrInstruction){
                .op = IR_LOAD, .span = e->span, .result = old, .a = v->id});
    size_t one = constant(l, v->type->kind, 1, 1.0, 0, e->span);
    size_t result =
        binary(l, e->as.binding.op == ASSIGN_ADD ? BINARY_ADD : BINARY_SUB,
               v->type->kind, v->type->kind, old, one, e->span);
    emit(l, (IrInstruction){
                .op = IR_STORE, .span = e->span, .a = v->id, .b = result});
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_UNARY: {
    const SemaExpr *value = e->as.unary.value;
    UnaryOp op = e->as.unary.op;
    size_t a = lower(l, value);
    if (l->returned)
      return IR_NO_VALUE;
    size_t result = temporary(l, e->type->kind);
    IrInstruction in = {.op = IR_UNARY,
                        .span = e->span,
                        .type = value->type->kind,
                        .result = result,
                        .a = a};
    in.as.unary = op;
    emit(l, in);
    return result;
  }
  case SEMA_EXPR_BINARY: {
    BinaryOp op = e->as.binary.op;
    size_t a = lower(l, e->as.binary.left);
    if (l->returned)
      return IR_NO_VALUE;
    if (op == BINARY_LOGICAL_AND || op == BINARY_LOGICAL_OR) {
      size_t result = temporary(l, SEMA_BOOL), other = l->function->labels++,
             end = l->function->labels++;
      emit(l, (IrInstruction){
                  .op = IR_COPY, .span = e->span, .result = result, .a = a});
      emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                              .span = e->span,
                              .a = a,
                              .label = op == BINARY_LOGICAL_AND ? end : other});
      if (op == BINARY_LOGICAL_OR) {
        jump(l, end, e->span);
        label(l, other, e->span);
      }
      size_t b = lower(l, e->as.binary.right);
      if (!l->returned)
        emit(l, (IrInstruction){
                    .op = IR_COPY, .span = e->span, .result = result, .a = b});
      label(l, end, e->span);
      return result;
    }
    size_t b = lower(l, e->as.binary.right);
    if (l->returned)
      return IR_NO_VALUE;
    return binary(l, op, e->as.binary.left->type->kind, e->type->kind, a, b,
                  e->span);
  }
  case SEMA_EXPR_CALL: {
    size_t *args = arena_calloc(l->arena, e->as.call.count, sizeof(*args));
    for (size_t i = 0; i < e->as.call.count; i++) {
      args[i] = lower(l, e->as.call.args[i]);
      if (l->returned)
        return IR_NO_VALUE;
    }
    IrInstruction in = {
        .op = IR_CALL, .span = e->span, .result = temporary(l, e->type->kind)};
    in.as.call.builtin = e->as.call.target->builtin;
    in.as.call.function = e->as.call.target->id;
    in.as.call.count = e->as.call.count;
    in.as.call.args = args;
    emit(l, in);
    return in.result;
  }
  case SEMA_EXPR_BLOCK: {
    size_t value = IR_NO_VALUE;
    for (size_t i = 0; i < e->as.block.count && !l->returned; i++)
      value = lower(l, e->as.block.items[i]);
    return value;
  }
  case SEMA_EXPR_RETURN: {
    size_t value = e->as.value ? lower(l, e->as.value) : IR_NO_VALUE;
    if (!l->returned)
      emit(l, (IrInstruction){.op = IR_RETURN, .span = e->span, .a = value});
    l->returned = 1;
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_IF: {
    size_t condition = lower(l, e->as.conditional.condition);
    if (l->returned)
      return IR_NO_VALUE;
    size_t other = l->function->labels++, end = l->function->labels++,
           result = temporary(l, e->type->kind);
    int has_else = e->as.conditional.no != NULL;
    emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                            .span = e->span,
                            .a = condition,
                            .label = other});
    size_t yes = lower(l, e->as.conditional.yes);
    int yes_falls = !l->returned;
    if (yes_falls && result != IR_NO_VALUE)
      emit(l, (IrInstruction){
                  .op = IR_COPY, .span = e->span, .result = result, .a = yes});
    if (yes_falls && has_else)
      jump(l, end, e->span);
    label(l, other, e->span);
    if (has_else) {
      size_t no = lower(l, e->as.conditional.no);
      if (!l->returned && result != IR_NO_VALUE)
        emit(l, (IrInstruction){
                    .op = IR_COPY, .span = e->span, .result = result, .a = no});
      if (yes_falls)
        label(l, end, e->span);
    }
    l->returned = e->returns;
    return result;
  }
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    if (e->kind == SEMA_EXPR_CONTINUE)
      l->loop->continue_used = 1;
    jump(l, e->kind == SEMA_EXPR_BREAK ? l->loop->exit : l->loop->next,
         e->span);
    return IR_NO_VALUE;
  case SEMA_EXPR_WHILE: {
    size_t head = l->function->labels++, end = l->function->labels++;
    jump(l, head, e->span);
    label(l, head, e->span);
    size_t condition = lower(l, e->as.while_loop.condition);
    if (l->returned)
      return IR_NO_VALUE;
    emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                            .span = e->span,
                            .a = condition,
                            .label = end});
    Loop loop = {.exit = end, .next = head, .outer = l->loop};
    l->loop = &loop;
    lower(l, e->as.while_loop.body);
    l->loop = loop.outer;
    if (!l->returned)
      jump(l, head, e->span);
    label(l, end, e->span);
    if (e->returns) {
      emit(l, (IrInstruction){.op = IR_UNREACHABLE, .span = e->span});
      l->returned = 1;
    }
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_FOR: {
    SemaVar *v = e->as.for_loop.variable;
    size_t start = lower(l, e->as.for_loop.lower);
    if (l->returned)
      return IR_NO_VALUE;
    size_t stop = lower(l, e->as.for_loop.upper);
    if (l->returned)
      return IR_NO_VALUE;
    emit(l, (IrInstruction){
                .op = IR_STORE, .span = e->span, .a = v->id, .b = start});
    size_t head = l->function->labels++, step = l->function->labels++,
           end = l->function->labels++;
    jump(l, head, e->span);
    label(l, head, e->span);
    size_t index = temporary(l, v->type->kind);
    emit(l, (IrInstruction){
                .op = IR_LOAD, .span = e->span, .result = index, .a = v->id});
    size_t condition =
        binary(l, BINARY_LT, v->type->kind, SEMA_BOOL, index, stop, e->span);
    emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                            .span = e->span,
                            .a = condition,
                            .label = end});
    Loop loop = {.exit = end, .next = step, .outer = l->loop};
    l->loop = &loop;
    lower(l, e->as.for_loop.body);
    l->loop = loop.outer;
    if (!l->returned || loop.continue_used) {
      if (!l->returned)
        jump(l, step, e->span);
      label(l, step, e->span);
      // A continue may have followed arbitrary body effects; load again here.
      size_t old = temporary(l, v->type->kind);
      emit(l, (IrInstruction){
                  .op = IR_LOAD, .span = e->span, .result = old, .a = v->id});
      size_t one = constant(l, v->type->kind, 1, 1.0, 0, e->span);
      size_t next = binary(l, BINARY_ADD, v->type->kind, v->type->kind, old,
                           one, e->span);
      emit(l, (IrInstruction){
                  .op = IR_STORE, .span = e->span, .a = v->id, .b = next});
      jump(l, head, e->span);
    }
    label(l, end, e->span);
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_ERROR:
    return IR_NO_VALUE;
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
    const SemaFunction *source = &program->functions[i];
    IrFunction *f = &p->functions[i];
    f->name = source->symbol.name;
    f->span = source->symbol.span;
    f->return_type = source->symbol.return_type->kind;
    f->variable_count = source->variable_count;
    f->parameter_count = source->symbol.arity;
    f->variables =
        arena_calloc(arena, f->variable_count, sizeof(*f->variables));
    for (size_t j = 0; j < f->variable_count; j++)
      f->variables[j] = source->variables[j]->type->kind;
    Lower l = {.arena = arena, .function = f};
    size_t value = lower(&l, source->body);
    if (!l.returned)
      emit(&l, (IrInstruction){.op = IR_RETURN,
                               .span = f->span,
                               .a = f->return_type == SEMA_UNIT ? IR_NO_VALUE
                                                                : value});
  }
  return p;
}
