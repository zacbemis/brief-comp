#include "ir/ir.h"
#include <stdio.h>
#include <string.h>

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
  int returned;
  Loop *loop;
  Owned *owned;
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
static void own(Lower *l, size_t id, int variable) {
  Owned *o = arena_alloc(l->arena, sizeof(*o));
  *o = (Owned){id, variable, l->owned};
  l->owned = o;
}
static void cleanup(Lower *l, Owned *until, size_t except, Span span) {
  for (Owned *o = l->owned; o != until; o = o->next)
    if (o->variable || o->id != except)
      emit(l, (IrInstruction){.op = o->variable ? IR_DROP_VAR : IR_DROP_TEMP,
                              .span = span,
                              .a = o->id});
}
static size_t temporary(Lower *l, const SemaType *type) {
  if (type->kind == SEMA_UNIT || type->kind == SEMA_NEVER)
    return IR_NO_VALUE;
  IrFunction *f = l->function;
  if (f->temporaries == f->temporary_capacity) {
    size_t capacity = f->temporary_capacity ? f->temporary_capacity * 2 : 16;
    const SemaType **items = arena_calloc(l->arena, capacity, sizeof(*items));
    if (f->temporaries)
      memcpy(items, f->temporary_types, f->temporaries * sizeof(*items));
    f->temporary_types = items;
    f->temporary_capacity = capacity;
  }
  size_t id = f->temporaries++;
  f->temporary_types[id] = type;
  if (sema_reference(type))
    own(l, id, 0);
  return id;
}
static void jump(Lower *l, size_t target, Span span) {
  emit(l, (IrInstruction){.op = IR_JUMP, .span = span, .label = target});
  l->returned = 1;
}
static void label(Lower *l, size_t id, Span span) {
  emit(l, (IrInstruction){.op = IR_LABEL, .span = span, .label = id});
  l->returned = 0;
}
static size_t constant(Lower *l, const SemaType *type, uint64_t integer,
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
static size_t binary(Lower *l, BinaryOp op, const SemaType *type,
                     const SemaType *result_type, size_t a, size_t b,
                     Span span) {
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
static size_t load(Lower *l, SemaVar *v, size_t object, SemaField *field,
                   Span span) {
  size_t result = temporary(l, field ? field->type : v->type);
  if (result != IR_NO_VALUE)
    emit(l, (IrInstruction){.op = field ? IR_FIELD_LOAD : IR_LOAD,
                            .span = span,
                            .result = result,
                            .a = field ? object : v->id,
                            .field = field});
  return result;
}
static void store(Lower *l, SemaVar *v, size_t object, SemaField *field,
                  size_t value, Span span) {
  if (value != IR_NO_VALUE)
    emit(l, (IrInstruction){.op = field ? IR_FIELD_STORE : IR_STORE,
                            .span = span,
                            .a = field ? object : v->id,
                            .b = value,
                            .field = field});
}
static size_t lower(Lower *l, const SemaExpr *e) {
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    return IR_NO_VALUE;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
  case SEMA_EXPR_BOOL:
  case SEMA_EXPR_CHAR:
    return constant(l, e->type, e->integer, e->real, e->negative, e->span);
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
    size_t result = temporary(l, e->type);
    IrInstruction in = {.op = IR_STRING, .span = e->span, .result = result};
    in.as.string.bytes = bytes;
    in.as.string.length = length;
    emit(l, in);
    return result;
  }
  case SEMA_EXPR_VAR:
    return load(l, e->as.variable, IR_NO_VALUE, NULL, e->span);
  case SEMA_EXPR_FIELD: {
    size_t object = lower(l, e->as.field.object);
    if (l->returned)
      return IR_NO_VALUE;
    return load(l, NULL, object, e->as.field.field, e->span);
  }
  case SEMA_EXPR_STRUCT: {
    SemaStruct *s = e->as.structure.type;
    size_t *args = arena_calloc(l->arena, s->count, sizeof(*args));
    for (size_t i = 0; i < e->as.structure.count; i++) {
      size_t value = lower(l, e->as.structure.values[i]);
      if (l->returned)
        return IR_NO_VALUE;
      args[e->as.structure.fields[i]->id] = value;
    }
    IrInstruction in = {.op = IR_NEW,
                        .span = e->span,
                        .type = e->type,
                        .result = temporary(l, e->type)};
    in.as.call.count = s->count;
    in.as.call.args = args;
    emit(l, in);
    return in.result;
  }
  case SEMA_EXPR_BIND:
  case SEMA_EXPR_ASSIGN:
  case SEMA_EXPR_UPDATE: {
    SemaVar *v = e->as.binding.variable;
    const SemaExpr *target = e->as.binding.target;
    SemaField *field = target ? target->as.field.field : NULL;
    size_t object = target ? lower(l, target->as.field.object) : IR_NO_VALUE;
    if (l->returned)
      return IR_NO_VALUE;
    const SemaType *type = field ? field->type : v->type;
    size_t old = IR_NO_VALUE;
    if (e->kind != SEMA_EXPR_BIND && e->as.binding.op != ASSIGN_SET)
      old = load(l, v, object, field, e->span);
    size_t value = e->kind == SEMA_EXPR_UPDATE
                       ? constant(l, type, 1, 1.0, 0, e->span)
                       : lower(l, e->as.binding.value);
    if (l->returned)
      return IR_NO_VALUE;
    if (old != IR_NO_VALUE)
      value = binary(l, (BinaryOp)(BINARY_ADD + e->as.binding.op - ASSIGN_ADD),
                     type, type, old, value, e->span);
    store(l, v, object, field, value, e->span);
    if (e->kind == SEMA_EXPR_BIND && sema_reference(v->type))
      own(l, v->id, 1);
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_UNARY: {
    const SemaExpr *value = e->as.unary.value;
    size_t a = lower(l, value);
    if (l->returned)
      return IR_NO_VALUE;
    IrInstruction in = {.op = IR_UNARY,
                        .span = e->span,
                        .type = value->type,
                        .result = temporary(l, e->type),
                        .a = a};
    in.as.unary = e->as.unary.op;
    emit(l, in);
    return in.result;
  }
  case SEMA_EXPR_BINARY: {
    BinaryOp op = e->as.binary.op;
    size_t a = lower(l, e->as.binary.left);
    if (l->returned)
      return IR_NO_VALUE;
    if (op == BINARY_LOGICAL_AND || op == BINARY_LOGICAL_OR) {
      size_t result = temporary(l, e->type), other = l->function->labels++,
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
    return binary(l, op, e->as.binary.left->type, e->type, a, b, e->span);
  }
  case SEMA_EXPR_CALL: {
    size_t *args = arena_calloc(l->arena, e->as.call.count, sizeof(*args));
    for (size_t i = 0; i < e->as.call.count; i++) {
      args[i] = lower(l, e->as.call.args[i]);
      if (l->returned)
        return IR_NO_VALUE;
    }
    IrInstruction in = {
        .op = IR_CALL, .span = e->span, .result = temporary(l, e->type)};
    in.as.call.builtin = e->as.call.target->builtin;
    in.as.call.function = e->as.call.target->id;
    in.as.call.count = e->as.call.count;
    in.as.call.args = args;
    if (in.as.call.builtin == SEMA_BUILTIN_STR)
      in.type = e->as.call.args[0]->type;
    emit(l, in);
    return in.result;
  }
  case SEMA_EXPR_BLOCK: {
    // Pin a reference result outside this lexical cleanup region.
    size_t escaped =
        sema_reference(e->type) ? temporary(l, e->type) : IR_NO_VALUE;
    Owned *saved = l->owned;
    size_t value = IR_NO_VALUE;
    for (size_t i = 0; i < e->as.block.count && !l->returned; i++)
      value = lower(l, e->as.block.items[i]);
    if (!l->returned) {
      if (escaped != IR_NO_VALUE)
        emit(l, (IrInstruction){.op = IR_COPY,
                                .span = e->span,
                                .result = escaped,
                                .a = value});
      cleanup(l, saved, IR_NO_VALUE, e->span);
    }
    l->owned = saved;
    return escaped != IR_NO_VALUE ? escaped : value;
  }
  case SEMA_EXPR_RETURN: {
    size_t value = e->as.value ? lower(l, e->as.value) : IR_NO_VALUE;
    if (!l->returned) {
      cleanup(l, NULL, value, e->span);
      emit(l, (IrInstruction){.op = IR_RETURN, .span = e->span, .a = value});
    }
    l->returned = 1;
    return IR_NO_VALUE;
  }
  case SEMA_EXPR_MATCH: {
    size_t result = temporary(l, e->type);
    size_t input = lower(l, e->as.match.input);
    if (l->returned)
      return IR_NO_VALUE;
    size_t end = l->function->labels++;
    int falls = 0;
    Owned *saved = l->owned;
    for (size_t i = 0; i < e->as.match.count; i++) {
      const SemaArm *a = &e->as.match.arms[i];
      if (!a->useful)
        continue;
      size_t next = IR_NO_VALUE;
      if (!a->any) {
        next = l->function->labels++;
        const SemaType *type = e->as.match.input->type;
        size_t first = lower(l, a->first_value);
        size_t test = binary(l, BINARY_GE, type, &l->program->types[SEMA_BOOL],
                             input, first, a->span);
        emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = a->span,
                                .a = test,
                                .label = next});
        size_t last = lower(l, a->last_value);
        test = binary(l, BINARY_LE, type, &l->program->types[SEMA_BOOL], input,
                      last, a->span);
        emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = a->span,
                                .a = test,
                                .label = next});
      }
      if (a->binding) {
        store(l, a->binding, IR_NO_VALUE, NULL, input, a->span);
        if (sema_reference(a->binding->type))
          own(l, a->binding->id, 1);
      }
      size_t value = lower(l, a->body);
      if (!l->returned) {
        if (result != IR_NO_VALUE)
          emit(l, (IrInstruction){.op = IR_COPY,
                                  .span = a->span,
                                  .result = result,
                                  .a = value});
        cleanup(l, saved, IR_NO_VALUE, a->span);
        jump(l, end, a->span);
        falls = 1;
      }
      l->owned = saved;
      if (next != IR_NO_VALUE)
        label(l, next, a->span);
    }
    emit(l, (IrInstruction){.op = IR_UNREACHABLE, .span = e->span});
    if (falls)
      label(l, end, e->span);
    l->returned = e->returns;
    return result;
  }
  case SEMA_EXPR_IF: {
    size_t condition = lower(l, e->as.conditional.condition);
    if (l->returned)
      return IR_NO_VALUE;
    size_t other = l->function->labels++, end = l->function->labels++,
           result = temporary(l, e->type);
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
    cleanup(l, l->loop->saved, IR_NO_VALUE, e->span);
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
    Loop loop = {
        .exit = end, .next = head, .outer = l->loop, .saved = l->owned};
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
    store(l, v, IR_NO_VALUE, NULL, start, e->span);
    size_t head = l->function->labels++, step = l->function->labels++,
           end = l->function->labels++;
    jump(l, head, e->span);
    label(l, head, e->span);
    size_t index = load(l, v, IR_NO_VALUE, NULL, e->span);
    size_t condition =
        binary(l, BINARY_LT, v->type, &l->program->types[SEMA_BOOL], index,
               stop, e->span);
    emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                            .span = e->span,
                            .a = condition,
                            .label = end});
    Loop loop = {
        .exit = end, .next = step, .outer = l->loop, .saved = l->owned};
    l->loop = &loop;
    lower(l, e->as.for_loop.body);
    l->loop = loop.outer;
    if (!l->returned || loop.continue_used) {
      if (!l->returned)
        jump(l, step, e->span);
      label(l, step, e->span);
      size_t old = load(l, v, IR_NO_VALUE, NULL, e->span);
      size_t one = constant(l, v->type, 1, 1.0, 0, e->span);
      size_t next = binary(l, BINARY_ADD, v->type, v->type, old, one, e->span);
      store(l, v, IR_NO_VALUE, NULL, next, e->span);
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
  p->structs = program->structs;
  p->struct_count = program->struct_count;
  p->functions = arena_calloc(arena, p->count, sizeof(IrFunction));
  for (size_t i = 0; i < p->count; i++) {
    const SemaFunction *source = &program->functions[i];
    IrFunction *f = &p->functions[i];
    f->name = source->symbol.name;
    if (source->symbol.owner) {
      const char *owner = source->symbol.owner->type.name;
      size_t length = strlen(owner) + strlen(f->name) + 64;
      char *name = arena_alloc(arena, length);
      snprintf(name, length, "%zu_%s_%s", strlen(owner), owner, f->name);
      f->method = 1;
      // Top-level names receive a different namespace in emission.
      f->name = name;
    }
    f->span = source->symbol.span;
    f->return_type = source->symbol.return_type;
    f->variable_count = source->variable_count;
    f->parameter_count = source->symbol.arity;
    f->variables =
        arena_calloc(arena, f->variable_count, sizeof(*f->variables));
    for (size_t j = 0; j < f->variable_count; j++)
      f->variables[j] = source->variables[j]->type;
    Lower l = {.arena = arena, .program = program, .function = f};
    size_t value = lower(&l, source->body);
    if (!l.returned) {
      size_t result = f->return_type->kind == SEMA_UNIT ? IR_NO_VALUE : value;
      cleanup(&l, NULL, result, f->span);
      emit(&l, (IrInstruction){.op = IR_RETURN, .span = f->span, .a = result});
    }
  }
  return p;
}
