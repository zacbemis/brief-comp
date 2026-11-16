#include "ir/lower/internal.h"

// Literal loads have no effects. Keep their immortal owners in the match
// scope so both taken and failed tests get ordinary exit/jump cleanup.
static size_t *string_patterns(Lower *l, const SemaExpr *e) {
  size_t *strings = arena_calloc(l->arena, e->as.match.count, sizeof(*strings));
  for (size_t i = 0; i < e->as.match.count; i++) {
    const SemaArm *a = &e->as.match.arms[i];
    if (a->useful && !a->any)
      strings[i] = lower_expr(l, a->lower);
  }
  return strings;
}

static void branch_false(Lower *l, size_t test, size_t label, Span span) {
  lower_emit(
      l, (IrInstruction){
             .op = IR_BRANCH_FALSE, .span = span, .a = test, .label = label});
}

// Emits the arm's test, returning the label taken when it fails, or
// IR_NO_VALUE for a catch-all arm.
static size_t arm_test(Lower *l, const SemaExpr *e, const SemaArm *a,
                       size_t input, size_t tag, const size_t *strings,
                       size_t index) {
  if (a->any)
    return IR_NO_VALUE;
  const SemaType *boolean = &l->program->types[SEMA_BOOL];
  const SemaType *type = e->as.match.input->type;
  size_t next = l->function->labels++;
  if (tag != IR_NO_VALUE) {
    size_t test =
        lower_tag_test(l, tag, a->case_variants, a->case_count, a->span);
    branch_false(l, test, next, a->span);
  } else if (strings) {
    size_t test = lower_binary(l, BINARY_EQ, type, boolean, input,
                               strings[index], a->span);
    branch_false(l, test, next, a->span);
  } else {
    size_t first = lower_expr(l, a->first_value);
    size_t test =
        lower_binary(l, BINARY_GE, type, boolean, input, first, a->span);
    branch_false(l, test, next, a->span);
    size_t last = lower_expr(l, a->last_value);
    test = lower_binary(l, BINARY_LE, type, boolean, input, last, a->span);
    branch_false(l, test, next, a->span);
  }
  return next;
}

static void arm_bindings(Lower *l, const SemaArm *a, size_t input, size_t tag) {
  size_t bindings = a->kind == PAT_SYMBOL ? a->payload_count : 1;
  for (size_t position = 0; position < bindings; position++) {
    SemaVar *binding =
        a->kind == PAT_SYMBOL
            ? (a->payload_bindings ? a->payload_bindings[position] : NULL)
            : a->binding;
    if (!binding)
      continue;
    size_t bound = input;
    int normalized_ok = a->kind == PAT_BIND && a->case_count == 1 &&
                        a->case_variants[0].tag == l->program->ok;
    if (tag != IR_NO_VALUE && (a->kind == PAT_SYMBOL || normalized_ok)) {
      bound = lower_temporary(l, binding->type);
      if (bound != IR_NO_VALUE)
        lower_emit(
            l, (IrInstruction){.op = IR_SYMBOL_PAYLOAD,
                               .span = a->span,
                               .result = bound,
                               .a = input,
                               .position = position,
                               .tag = normalized_ok ? l->program->ok : a->tag});
    }
    lower_store(l, binding, IR_NO_VALUE, NULL, bound, a->span);
    if (sema_reference(binding->type))
      lower_own(l, binding->id, 1);
  }
}

size_t lower_match(Lower *l, const SemaExpr *e) {
  size_t result = lower_temporary(l, e->type);
  size_t input = lower_expr(l, e->as.match.input);
  if (l->returned)
    return IR_NO_VALUE;
  const SemaType *type = e->as.match.input->type;
  size_t tag = IR_NO_VALUE;
  if (type->kind == SEMA_SYM) {
    tag = lower_temporary(l, &l->program->types[SEMA_UINT]);
    lower_emit(l, (IrInstruction){.op = IR_SYMBOL_TAG,
                                  .span = e->span,
                                  .result = tag,
                                  .a = input});
  }
  size_t *strings = type->kind == SEMA_STR ? string_patterns(l, e) : NULL;
  size_t end = l->function->labels++;
  int falls = 0;
  Owned *saved = l->owned;
  for (size_t i = 0; i < e->as.match.count; i++) {
    const SemaArm *a = &e->as.match.arms[i];
    if (!a->useful)
      continue;
    size_t next = arm_test(l, e, a, input, tag, strings, i);
    arm_bindings(l, a, input, tag);
    size_t value = lower_expr(l, a->body);
    if (!l->returned) {
      value = lower_convert(l, value, e->type, a->span);
      if (result != IR_NO_VALUE)
        lower_emit(l, (IrInstruction){.op = IR_COPY,
                                      .span = a->span,
                                      .result = result,
                                      .a = value});
      lower_cleanup(l, saved, IR_NO_VALUE, a->span);
      lower_jump(l, end, a->span);
      falls = 1;
    }
    l->owned = saved;
    if (next != IR_NO_VALUE)
      lower_label(l, next, a->span);
  }
  lower_emit(l, (IrInstruction){.op = IR_UNREACHABLE, .span = e->span});
  if (falls)
    lower_label(l, end, e->span);
  l->returned = e->returns;
  return result;
}
