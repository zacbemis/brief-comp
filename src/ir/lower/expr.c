#include "ir/lower/internal.h"

static size_t string(Lower *l, const SemaExpr *e) {
  size_t result = lower_temporary(l, e->type);
  IrInstruction in = {.op = IR_STRING, .span = e->span, .result = result};
  in.as.string.bytes = e->as.text.bytes;
  in.as.string.length = e->as.text.length;
  lower_emit(l, in);
  return result;
}

static size_t field(Lower *l, const SemaExpr *e) {
  size_t object = lower_expr(l, e->as.field.object);
  if (l->returned)
    return IR_NO_VALUE;
  return lower_load(l, NULL, object, e->as.field.field, e->span);
}

static size_t symbol(Lower *l, const SemaExpr *e) {
  size_t count = e->as.symbol.count;
  size_t *args = arena_calloc(l->arena, count, sizeof(*args));
  // A return may prevent the symbol type from ever being constructed.
  const SemaVariant *variant = sema_variant(e->type, e->as.symbol.tag);
  for (size_t i = 0; i < count; i++) {
    args[i] = lower_expr(l, e->as.symbol.payloads[i]);
    if (l->returned)
      return IR_NO_VALUE;
    args[i] = lower_convert(l, args[i], variant ? variant->payloads[i] : NULL,
                            e->span);
  }
  if (e->as.symbol.tag == l->program->ok)
    return count ? lower_convert(l, args[0], e->type, e->span) : IR_NO_VALUE;
  return lower_pack(l, e->type, e->as.symbol.tag, args, count, e->span);
}

static size_t structure(Lower *l, const SemaExpr *e) {
  SemaStruct *s = e->as.structure.type;
  size_t *args = arena_calloc(l->arena, s->count, sizeof(*args));
  for (size_t i = 0; i < e->as.structure.count; i++) {
    size_t value = lower_expr(l, e->as.structure.values[i]);
    if (l->returned)
      return IR_NO_VALUE;
    SemaField *field = e->as.structure.fields[i];
    args[field->id] = lower_convert(l, value, field->type, e->span);
  }
  IrInstruction in = {.op = IR_NEW,
                      .span = e->span,
                      .type = e->type,
                      .result = lower_temporary(l, e->type)};
  in.as.call.count = s->count;
  in.as.call.args = args;
  lower_emit(l, in);
  return in.result;
}

// Bindings, assignments and ++/--, including compound updates of fields.
static size_t write(Lower *l, const SemaExpr *e) {
  SemaVar *v = e->as.binding.variable;
  if (e->kind == SEMA_EXPR_BIND && !e->as.binding.value) {
    // C locals start at zero. Register their lexical cleanup now, even if
    // an early exit occurs before the first store; null/tag-zero is empty.
    if (sema_reference(v->type))
      lower_own(l, v->id, 1);
    return IR_NO_VALUE;
  }
  const SemaExpr *target = e->as.binding.target;
  SemaField *field = target ? target->as.field.field : NULL;
  size_t object = target ? lower_expr(l, target->as.field.object) : IR_NO_VALUE;
  if (l->returned)
    return IR_NO_VALUE;
  const SemaType *type = field ? field->type : v->type;
  size_t old = IR_NO_VALUE;
  if (e->kind != SEMA_EXPR_BIND && e->as.binding.op != ASSIGN_SET)
    old = lower_load(l, v, object, field, e->span);
  size_t value = e->kind == SEMA_EXPR_UPDATE
                     ? lower_constant(l, type, 1, 1.0, 0, e->span)
                     : lower_expr(l, e->as.binding.value);
  if (l->returned)
    return IR_NO_VALUE;
  if (old != IR_NO_VALUE)
    value =
        lower_binary(l, (BinaryOp)(BINARY_ADD + e->as.binding.op - ASSIGN_ADD),
                     type, type, old, value, e->span);
  lower_store(l, v, object, field, value, e->span);
  if (e->kind == SEMA_EXPR_BIND && sema_reference(v->type))
    lower_own(l, v->id, 1);
  return IR_NO_VALUE;
}

static size_t unary(Lower *l, const SemaExpr *e) {
  const SemaExpr *value = e->as.unary.value;
  size_t a = lower_expr(l, value);
  if (l->returned)
    return IR_NO_VALUE;
  IrInstruction in = {.op = IR_UNARY,
                      .span = e->span,
                      .type = value->type,
                      .result = lower_temporary(l, e->type),
                      .a = a};
  in.as.unary = e->as.unary.op;
  lower_emit(l, in);
  return in.result;
}

static size_t logical(Lower *l, const SemaExpr *e, size_t a) {
  BinaryOp op = e->as.binary.op;
  size_t result = lower_temporary(l, e->type), other = l->function->labels++,
         end = l->function->labels++;
  lower_emit(l, (IrInstruction){
                    .op = IR_COPY, .span = e->span, .result = result, .a = a});
  lower_emit(l,
             (IrInstruction){.op = IR_BRANCH_FALSE,
                             .span = e->span,
                             .a = a,
                             .label = op == BINARY_LOGICAL_AND ? end : other});
  if (op == BINARY_LOGICAL_OR) {
    lower_jump(l, end, e->span);
    lower_label(l, other, e->span);
  }
  size_t b = lower_expr(l, e->as.binary.right);
  if (!l->returned)
    lower_emit(l,
               (IrInstruction){
                   .op = IR_COPY, .span = e->span, .result = result, .a = b});
  lower_label(l, end, e->span);
  return result;
}

static size_t binary(Lower *l, const SemaExpr *e) {
  BinaryOp op = e->as.binary.op;
  size_t a = lower_expr(l, e->as.binary.left);
  if (l->returned)
    return IR_NO_VALUE;
  if (op == BINARY_LOGICAL_AND || op == BINARY_LOGICAL_OR)
    return logical(l, e, a);
  size_t b = lower_expr(l, e->as.binary.right);
  if (l->returned)
    return IR_NO_VALUE;
  const SemaType *type =
      e->as.binary.common ? e->as.binary.common : e->as.binary.left->type;
  a = lower_convert(l, a, type, e->span);
  b = lower_convert(l, b, type, e->span);
  return lower_binary(l, op, type, e->type, a, b, e->span);
}

static size_t call(Lower *l, const SemaExpr *e) {
  SemaSymbol *target = e->as.call.target;
  size_t *args = arena_calloc(l->arena, e->as.call.count, sizeof(*args));
  for (size_t i = 0; i < e->as.call.count; i++) {
    args[i] = lower_expr(l, e->as.call.args[i]);
    if (l->returned)
      return IR_NO_VALUE;
    args[i] = lower_convert(l, args[i], target->params[i]->type, e->span);
  }
  if (target->builtin == SEMA_BUILTIN_CONVERT) {
    IrInstruction in = {.op = IR_CONVERT,
                        .span = e->span,
                        .result = lower_temporary(l, e->type),
                        .a = args[0],
                        .type = e->as.call.args[0]->type};
    in.as.conversion = target->return_type;
    if (e->type->kind == SEMA_SYM)
      for (size_t i = 0; i < e->type->symbols->count; i++)
        if (e->type->symbols->variants[i].tag != l->program->ok)
          in.tag = e->type->symbols->variants[i].tag;
    lower_emit(l, in);
    return in.result;
  }
  IrInstruction in = {
      .op = IR_CALL, .span = e->span, .result = lower_temporary(l, e->type)};
  in.as.call.builtin = target->builtin;
  in.as.call.function = target->id;
  in.as.call.count = e->as.call.count;
  in.as.call.args = args;
  if (in.as.call.builtin == SEMA_BUILTIN_STR)
    in.type = e->as.call.args[0]->type;
  lower_emit(l, in);
  return in.result;
}

static size_t block(Lower *l, const SemaExpr *e) {
  // Pin a reference result outside this lexical cleanup region.
  size_t escaped =
      sema_reference(e->type) ? lower_temporary(l, e->type) : IR_NO_VALUE;
  Owned *saved = l->owned;
  size_t value = IR_NO_VALUE;
  for (size_t i = 0; i < e->as.block.count && !l->returned; i++)
    value = lower_expr(l, e->as.block.items[i]);
  if (!l->returned) {
    if (escaped != IR_NO_VALUE)
      lower_emit(l, (IrInstruction){.op = IR_COPY,
                                    .span = e->span,
                                    .result = escaped,
                                    .a = value});
    lower_cleanup(l, saved, IR_NO_VALUE, e->span);
  }
  l->owned = saved;
  return escaped != IR_NO_VALUE ? escaped : value;
}

static size_t ret(Lower *l, const SemaExpr *e) {
  size_t value = e->as.value ? lower_expr(l, e->as.value) : IR_NO_VALUE;
  if (!l->returned) {
    const SemaType *type = l->function->return_type;
    if (e->none_ending)
      value = lower_pack(l, type, sema_none(type)->tag, NULL, 0, e->span);
    value = lower_convert(l, value, type, e->span);
    lower_cleanup(l, NULL, value, e->span);
    lower_emit(l,
               (IrInstruction){.op = IR_RETURN, .span = e->span, .a = value});
  }
  l->returned = 1;
  return IR_NO_VALUE;
}

static size_t loop_exit(Lower *l, const SemaExpr *e) {
  lower_cleanup(l, l->loop->saved, IR_NO_VALUE, e->span);
  if (e->kind == SEMA_EXPR_CONTINUE)
    l->loop->continue_used = 1;
  lower_jump(l, e->kind == SEMA_EXPR_BREAK ? l->loop->exit : l->loop->next,
             e->span);
  return IR_NO_VALUE;
}

static size_t lower_raw(Lower *l, const SemaExpr *e) {
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
  case SEMA_EXPR_ERROR:
    return IR_NO_VALUE;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
  case SEMA_EXPR_BOOL:
  case SEMA_EXPR_CHAR:
    return lower_constant(l, e->type, e->integer, e->real, e->negative,
                          e->span);
  case SEMA_EXPR_STRING:
    return string(l, e);
  case SEMA_EXPR_VAR:
    return lower_load(l, e->as.variable, IR_NO_VALUE, NULL, e->span);
  case SEMA_EXPR_FIELD:
    return field(l, e);
  case SEMA_EXPR_SYMBOL:
    return symbol(l, e);
  case SEMA_EXPR_STRUCT:
    return structure(l, e);
  case SEMA_EXPR_BIND:
  case SEMA_EXPR_ASSIGN:
  case SEMA_EXPR_UPDATE:
    return write(l, e);
  case SEMA_EXPR_UNARY:
    return unary(l, e);
  case SEMA_EXPR_BINARY:
    return binary(l, e);
  case SEMA_EXPR_CALL:
    return call(l, e);
  case SEMA_EXPR_BLOCK:
    return block(l, e);
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC:
    return lower_try(l, e);
  case SEMA_EXPR_RETURN:
    return ret(l, e);
  case SEMA_EXPR_MATCH:
    return lower_match(l, e);
  case SEMA_EXPR_IF:
    return lower_if(l, e);
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    return loop_exit(l, e);
  case SEMA_EXPR_WHILE:
    return lower_while(l, e);
  case SEMA_EXPR_FOR:
    return lower_for(l, e);
  }
  return IR_NO_VALUE;
}

// Lowers with the expression's raw type, then applies the checker's
// contextual :none ending or implicit :ok adaptation.
size_t lower_expr(Lower *l, const SemaExpr *e) {
  SemaExpr raw = *e;
  if (e->raw_type)
    raw.type = e->raw_type;
  size_t value = lower_raw(l, &raw);
  if (l->returned)
    return value;
  if (e->none_ending)
    return lower_pack(l, e->type, sema_none(e->type)->tag, NULL, 0, e->span);
  if (!e->adapt)
    return value;
  const SemaVariant *ok = sema_ok(e->type);
  if (ok->payload_count)
    value = lower_convert(l, value, sema_single_payload(ok), e->span);
  return lower_pack(l, e->type, l->program->ok, &value, ok->payload_count,
                    e->span);
}
