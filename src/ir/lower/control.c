#include "ir/lower/internal.h"

// Propagates (?) or panics (!) on the failure value `input`.
static void fail(Lower *l, const SemaExpr *e, size_t input,
                 const SemaType *failures) {
  if (e->kind == SEMA_EXPR_PANIC) {
    lower_emit(l, (IrInstruction){.op = IR_PANIC_SYMBOL,
                                  .span = e->span,
                                  .type = e->as.value->type,
                                  .a = input});
    return;
  }
  size_t value = failures ? lower_convert(l, input, failures, e->span) : input;
  value = lower_convert(l, value, l->function->return_type, e->span);
  lower_cleanup(l, NULL, value, e->span);
  lower_emit(l, (IrInstruction){.op = IR_RETURN, .span = e->span, .a = value});
}

// Branches to a success label unless the input's tag is a failure.
static void failure_test(Lower *l, const SemaExpr *e, size_t input) {
  const SemaType *uint = &l->program->types[SEMA_UINT];
  size_t tag = lower_temporary(l, uint);
  lower_emit(
      l, (IrInstruction){
             .op = IR_SYMBOL_TAG, .span = e->span, .result = tag, .a = input});
  size_t failed = lower_tag_test(l, tag, e->failures->symbols->variants,
                                 e->failures->symbols->count, e->span);
  size_t success = l->function->labels++;
  lower_emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = e->span,
                                .a = failed,
                                .label = success});
  Owned *saved = l->owned;
  fail(l, e, input, e->failures);
  l->owned = saved;
  lower_label(l, success, e->span);
}

size_t lower_try(Lower *l, const SemaExpr *e) {
  size_t input = lower_expr(l, e->as.value);
  if (l->returned)
    return IR_NO_VALUE;
  const SemaType *type = e->as.value->type;
  if (sema_only_errors(type)) {
    // There is no success edge, tag test or payload projection. Keep the
    // existing propagation cleanup and panic's non-unwinding behavior.
    fail(l, e, input, NULL);
    l->returned = 1;
    return IR_NO_VALUE;
  }
  const SemaVariant *variant =
      sema_has_errors(type) ? sema_single_success(type) : sema_ok(type);
  if (e->failures)
    failure_test(l, e, input);
  if (!variant)
    return lower_convert(l, input, e->type, e->span);
  size_t result = lower_temporary(l, e->type);
  if (result != IR_NO_VALUE)
    lower_emit(l, (IrInstruction){.op = IR_SYMBOL_PAYLOAD,
                                  .span = e->span,
                                  .result = result,
                                  .a = input,
                                  .tag = variant->tag});
  return result;
}

size_t lower_if(Lower *l, const SemaExpr *e) {
  size_t condition = lower_expr(l, e->as.conditional.condition);
  if (l->returned)
    return IR_NO_VALUE;
  size_t other = l->function->labels++, end = l->function->labels++,
         result = lower_temporary(l, e->type);
  int has_else = e->as.conditional.no != NULL;
  lower_emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = e->span,
                                .a = condition,
                                .label = other});
  size_t yes = lower_expr(l, e->as.conditional.yes);
  int yes_falls = !l->returned;
  if (yes_falls && result != IR_NO_VALUE) {
    yes = lower_convert(l, yes, e->type, e->span);
    lower_emit(l,
               (IrInstruction){
                   .op = IR_COPY, .span = e->span, .result = result, .a = yes});
  }
  if (yes_falls && has_else)
    lower_jump(l, end, e->span);
  lower_label(l, other, e->span);
  if (has_else) {
    size_t no = lower_expr(l, e->as.conditional.no);
    if (!l->returned && result != IR_NO_VALUE) {
      no = lower_convert(l, no, e->type, e->span);
      lower_emit(
          l, (IrInstruction){
                 .op = IR_COPY, .span = e->span, .result = result, .a = no});
    }
    if (yes_falls)
      lower_label(l, end, e->span);
  }
  l->returned = e->returns;
  return result;
}

size_t lower_while(Lower *l, const SemaExpr *e) {
  size_t head = l->function->labels++, end = l->function->labels++;
  lower_jump(l, head, e->span);
  lower_label(l, head, e->span);
  size_t condition = lower_expr(l, e->as.while_loop.condition);
  if (l->returned)
    return IR_NO_VALUE;
  lower_emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = e->span,
                                .a = condition,
                                .label = end});
  Loop loop = {.exit = end, .next = head, .outer = l->loop, .saved = l->owned};
  l->loop = &loop;
  lower_expr(l, e->as.while_loop.body);
  l->loop = loop.outer;
  if (!l->returned)
    lower_jump(l, head, e->span);
  lower_label(l, end, e->span);
  if (e->returns) {
    lower_emit(l, (IrInstruction){.op = IR_UNREACHABLE, .span = e->span});
    l->returned = 1;
  }
  return IR_NO_VALUE;
}

// The step block: exhaustion check for inclusive ranges, then v += 1.
static void step(Lower *l, const SemaExpr *e, size_t stop, size_t head,
                 size_t end) {
  SemaVar *v = e->as.for_loop.variable;
  const SemaType *boolean = &l->program->types[SEMA_BOOL];
  size_t old = lower_load(l, v, IR_NO_VALUE, NULL, e->span);
  if (e->as.for_loop.inclusive) {
    // Continue reaches this exhaustion check too. Never form stop + 1
    // or increment a maximum value, even on a singleton final iteration.
    size_t more =
        lower_binary(l, BINARY_NE, v->type, boolean, old, stop, e->span);
    lower_emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                  .span = e->span,
                                  .a = more,
                                  .label = end});
  }
  size_t one = lower_constant(l, v->type, 1, 1.0, 0, e->span);
  size_t next =
      lower_binary(l, BINARY_ADD, v->type, v->type, old, one, e->span);
  lower_store(l, v, IR_NO_VALUE, NULL, next, e->span);
  lower_jump(l, head, e->span);
}

size_t lower_for(Lower *l, const SemaExpr *e) {
  SemaVar *v = e->as.for_loop.variable;
  size_t start = lower_expr(l, e->as.for_loop.lower);
  if (l->returned)
    return IR_NO_VALUE;
  size_t stop = lower_expr(l, e->as.for_loop.upper);
  if (l->returned)
    return IR_NO_VALUE;
  lower_store(l, v, IR_NO_VALUE, NULL, start, e->span);
  size_t head = l->function->labels++, next = l->function->labels++,
         end = l->function->labels++;
  if (!e->as.for_loop.inclusive) {
    lower_jump(l, head, e->span);
    lower_label(l, head, e->span);
  }
  size_t index = lower_load(l, v, IR_NO_VALUE, NULL, e->span);
  size_t condition =
      lower_binary(l, e->as.for_loop.inclusive ? BINARY_LE : BINARY_LT, v->type,
                   &l->program->types[SEMA_BOOL], index, stop, e->span);
  lower_emit(l, (IrInstruction){.op = IR_BRANCH_FALSE,
                                .span = e->span,
                                .a = condition,
                                .label = end});
  if (e->as.for_loop.inclusive) {
    // Entry is tested only once. Keep head an explicit target even when
    // the body always returns/breaks and no back edge is emitted.
    lower_jump(l, head, e->span);
    lower_label(l, head, e->span);
  }
  Loop loop = {.exit = end, .next = next, .outer = l->loop, .saved = l->owned};
  l->loop = &loop;
  lower_expr(l, e->as.for_loop.body);
  l->loop = loop.outer;
  if (!l->returned || loop.continue_used) {
    if (!l->returned)
      lower_jump(l, next, e->span);
    lower_label(l, next, e->span);
    step(l, e, stop, head, end);
  }
  lower_label(l, end, e->span);
  return IR_NO_VALUE;
}
