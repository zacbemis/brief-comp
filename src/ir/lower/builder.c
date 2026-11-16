#include "ir/lower/internal.h"

#include <string.h>

void lower_emit(Lower *l, IrInstruction instruction) {
  IrFunction *f = l->function;
  ARENA_GROW(l->arena, f->instructions, f->count, f->capacity, 16);
  f->instructions[f->count++] = instruction;
}

void lower_own(Lower *l, size_t id, int variable) {
  Owned *o = arena_alloc(l->arena, sizeof(*o));
  *o = (Owned){id, variable, l->owned};
  l->owned = o;
}

void lower_cleanup(Lower *l, Owned *until, size_t except, Span span) {
  for (Owned *o = l->owned; o != until; o = o->next)
    if (o->variable || o->id != except)
      lower_emit(l,
                 (IrInstruction){.op = o->variable ? IR_DROP_VAR : IR_DROP_TEMP,
                                 .span = span,
                                 .a = o->id});
}

size_t lower_temporary(Lower *l, const SemaType *type) {
  if (type->kind == SEMA_UNIT || type->kind == SEMA_NEVER)
    return IR_NO_VALUE;
  IrFunction *f = l->function;
  ARENA_GROW(l->arena, f->temporary_types, f->temporaries,
             f->temporary_capacity, 16);
  size_t id = f->temporaries++;
  f->temporary_types[id] = type;
  if (sema_reference(type))
    lower_own(l, id, 0);
  return id;
}

void lower_jump(Lower *l, size_t target, Span span) {
  lower_emit(l, (IrInstruction){.op = IR_JUMP, .span = span, .label = target});
  l->returned = 1;
}

void lower_label(Lower *l, size_t id, Span span) {
  lower_emit(l, (IrInstruction){.op = IR_LABEL, .span = span, .label = id});
  l->returned = 0;
}

size_t lower_constant(Lower *l, const SemaType *type, uint64_t integer,
                      double real, int negative, Span span) {
  size_t result = lower_temporary(l, type);
  IrInstruction in = {
      .op = IR_CONSTANT, .span = span, .type = type, .result = result};
  in.as.constant.integer = integer;
  in.as.constant.real = real;
  in.as.constant.negative = negative;
  lower_emit(l, in);
  return result;
}

size_t lower_binary(Lower *l, BinaryOp op, const SemaType *type,
                    const SemaType *result_type, size_t a, size_t b,
                    Span span) {
  size_t result = lower_temporary(l, result_type);
  IrInstruction in = {.op = IR_BINARY,
                      .span = span,
                      .type = type,
                      .result = result,
                      .a = a,
                      .b = b};
  in.as.binary = op;
  lower_emit(l, in);
  return result;
}

size_t lower_pack(Lower *l, const SemaType *type, SemaTag *tag,
                  const size_t *args, size_t count, Span span) {
  size_t result = lower_temporary(l, type);
  IrInstruction in = {.op = IR_SYMBOL,
                      .span = span,
                      .type = type,
                      .result = result,
                      .tag = tag};
  in.as.symbol.count = count;
  in.as.symbol.args = arena_calloc(l->arena, count, sizeof(*args));
  if (count)
    memcpy(in.as.symbol.args, args, count * sizeof(*args));
  lower_emit(l, in);
  return result;
}

size_t lower_convert(Lower *l, size_t value, const SemaType *target,
                     Span span) {
  if (!target)
    return value;
  const SemaType *source = value == IR_NO_VALUE
                               ? &l->program->types[SEMA_UNIT]
                               : l->function->temporary_types[value];
  if (source == target)
    return value;
  const SemaVariant *ok = sema_ok(target);
  const SemaType *payload = ok && ok->payload_count
                                ? sema_single_payload(ok)
                                : &l->program->types[SEMA_UNIT];
  if (ok && !sema_assignable(source, target) &&
      sema_assignable(source, payload)) {
    if (ok->payload_count)
      value = lower_convert(l, value, payload, span);
    return lower_pack(l, target, l->program->ok, &value, ok->payload_count,
                      span);
  }
  // Ordinary contexts widen; guarded catch-alls and propagation also narrow.
  if (target->kind == SEMA_SYM) {
    size_t result = lower_temporary(l, target);
    lower_emit(l, (IrInstruction){.op = IR_SYMBOL_CAST,
                                  .span = span,
                                  .type = target,
                                  .result = result,
                                  .a = value});
    return result;
  }
  return value;
}

size_t lower_load(Lower *l, SemaVar *v, size_t object, SemaField *field,
                  Span span) {
  size_t result = lower_temporary(l, field ? field->type : v->type);
  if (result != IR_NO_VALUE)
    lower_emit(l, (IrInstruction){.op = field ? IR_FIELD_LOAD : IR_LOAD,
                                  .span = span,
                                  .result = result,
                                  .a = field ? object : v->id,
                                  .field = field});
  return result;
}

void lower_store(Lower *l, SemaVar *v, size_t object, SemaField *field,
                 size_t value, Span span) {
  value = lower_convert(l, value, field ? field->type : v->type, span);
  if (value != IR_NO_VALUE)
    lower_emit(l, (IrInstruction){.op = field ? IR_FIELD_STORE : IR_STORE,
                                  .span = span,
                                  .a = field ? object : v->id,
                                  .b = value,
                                  .field = field});
}

size_t lower_tag_test(Lower *l, size_t tag, const SemaVariant *variants,
                      size_t count, Span span) {
  const SemaType *uint = &l->program->types[SEMA_UINT];
  const SemaType *boolean = &l->program->types[SEMA_BOOL];
  size_t test = IR_NO_VALUE;
  for (size_t i = 0; i < count; i++) {
    size_t id = lower_constant(l, uint, variants[i].tag->id, 0, 0, span);
    size_t one = lower_binary(l, BINARY_EQ, uint, boolean, tag, id, span);
    test = test == IR_NO_VALUE ? one
                               : lower_binary(l, BINARY_BIT_OR, boolean,
                                              boolean, test, one, span);
  }
  return test;
}
