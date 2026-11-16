#include "sema/resolve/internal.h"

// Resolves the callee to a target, a method name, or an associated function.
// Returns the receiver argument for an instance call.
static SemaExpr *callee(Resolver *r, SemaExpr *e, const Expr *callee,
                        int pipeline, int generic) {
  SemaExpr *receiver = NULL;
  if (callee->kind == EXPR_FIELD) {
    SemaStruct *owner =
        callee->as.field.object->kind == EXPR_NAME &&
                !resolve_local(r, callee->as.field.object->as.name)
            ? resolve_named_structure(r, callee->as.field.object)
            : NULL;
    if (owner) {
      if (!generic) {
        e->as.call.target =
            sema_method(r->program, &owner->type, callee->as.field.name);
        if (!e->as.call.target)
          diagnostics_error(r->errors, callee->span,
                            "Unknown associated function.");
      }
    } else {
      receiver = resolve_expr(r, callee->as.field.object);
      if (!generic) {
        e->as.call.method = callee->as.field.name;
        // Dotted calls never fall back to top-level functions. Remember only
        // a visible candidate for advice after receiver inference.
        if (!resolve_local(r, e->as.call.method))
          e->as.call.fallback = resolve_lookup(r->program, e->as.call.method);
      }
    }
  } else if (!generic) {
    if (callee->kind != EXPR_NAME)
      diagnostics_error(r->errors, callee->span,
                        "Function values are not implemented yet.");
    else if (!pipeline && resolve_local(r, callee->as.name))
      diagnostics_error(r->errors, callee->span,
                        "A local binding is not callable.");
    else {
      e->as.call.target = resolve_lookup(r->program, callee->as.name);
      if (!e->as.call.target && !pipeline)
        diagnostics_error(r->errors, callee->span, "Unknown function.");
    }
  }
  // An unsupported generic header has no ordinary callee or signature.
  // Keep a targetless call so its receiver/arguments still get VALUE checks,
  // flow and dependency edges, without callee-specific error cascades.
  if (!generic && pipeline && callee->kind == EXPR_NAME) {
    e->as.call.pipeline = 1;
    e->as.call.method = callee->as.name;
    e->as.call.fallback = e->as.call.target;
    e->as.call.shadowed = resolve_local(r, callee->as.name) != NULL;
  }
  return receiver;
}

// Instance receiver types may be inferred later. Conservative edges to
// matching methods keep every possible callee in the SCC schedule. Edges
// are added in declaration order, merging the target into the chain.
static void call_edges(Resolver *r, SemaExpr *e) {
  SemaSymbol *target = e->as.call.target;
  if (target && target->builtin)
    target = NULL;
  const MethodChain *chain =
      e->as.call.method
          ? name_map_get(&r->program->method_names, NULL, e->as.call.method)
          : NULL;
  for (SemaSymbol *m = chain ? chain->first : NULL; m || target;) {
    SemaSymbol *symbol = m;
    if (target && (!m || target->id <= m->id)) {
      symbol = target;
      if (m == target)
        m = m->next_method;
      target = NULL;
    } else
      m = m->next_method;
    SemaEdge *edge = arena_alloc(r->arena, sizeof(*edge));
    *edge = (SemaEdge){symbol->id, r->function->edges};
    r->function->edges = edge;
  }
}

SemaExpr *resolve_call(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_CALL, s->span);
  int pipeline = r->pipeline_call;
  e->as.call.source_pipeline = r->call_source != NULL;
  e->as.call.syntax = r->call_source ? r->call_source : s;
  r->pipeline_call = 0;
  r->call_source = NULL;
  int generic = s->as.call.type_count != 0;
  if (generic)
    diagnostics_error(r->errors, s->span,
                      "Generic calls are not implemented yet.");
  SemaExpr *receiver = callee(r, e, s->as.call.callee, pipeline, generic);
  call_edges(r, e);
  e->as.call.count = s->as.call.count + (receiver != NULL);
  e->as.call.args =
      arena_calloc(r->arena, e->as.call.count, sizeof(SemaExpr *));
  if (receiver)
    e->as.call.args[0] = receiver;
  for (size_t i = 0; i < s->as.call.count; i++)
    e->as.call.args[i + (receiver != NULL)] =
        resolve_expr(r, s->as.call.args[i]);
  return e;
}

// `value |> f(args)` resolves as f($pipe, args) for the original call syntax.
static SemaExpr *pipe_call(Resolver *r, const Expr *stage, SemaExpr *bind) {
  Expr arg = {.kind = EXPR_NAME, .span = bind->span};
  arg.as.name = "$pipe";
  Expr call = *stage;
  call.as.call.count++;
  call.as.call.args =
      arena_calloc(r->arena, call.as.call.count, sizeof(Expr *));
  call.as.call.args[0] = &arg;
  for (size_t i = 0; i < stage->as.call.count; i++)
    call.as.call.args[i + 1] = stage->as.call.args[i];
  r->pipeline_call = stage->as.call.callee->kind == EXPR_NAME;
  r->call_source = stage;
  SemaExpr *result = resolve_expr(r, &call);
  bind->as.binding.stage = result;
  return result;
}

static SemaExpr *pipe_match(Resolver *r, const Expr *s, const Expr *stage,
                            SemaExpr *bind) {
  SemaExpr *input = bind->as.binding.value;
  resolve_handled(input);
  bind->as.binding.outcome = 1;
  SemaExpr *result = node(r, SEMA_EXPR_MATCH, s->span);
  SemaExpr *read = node(r, SEMA_EXPR_VAR, input->span);
  read->as.variable = bind->as.binding.variable;
  result->as.match.input = read;
  result->as.match.count = stage->as.match.count;
  result->as.match.arms =
      arena_calloc(r->arena, stage->as.match.count, sizeof(SemaArm));
  for (size_t i = 0; i < stage->as.match.count; i++)
    resolve_arm(r, &result->as.match.arms[i], stage->as.match.arms[i]);
  return result;
}

SemaExpr *resolve_pipe(Resolver *r, const Expr *s) {
  const Expr *stage = s->as.pipe.stage;
  const Expr *postfix = NULL;
  if (stage->kind == EXPR_TRY || stage->kind == EXPR_PANIC) {
    postfix = stage;
    stage = stage->as.postfix;
  }
  if (stage->kind != EXPR_CALL && stage->kind != EXPR_MATCH)
    return unsupported(r, stage->span, "Unsupported pipeline stage.");
  // A hidden lexical owner sequences the input before the stage's callee
  // and other arguments, and snapshots its access permission at that point.
  Local *saved = r->locals;
  r->depth++;
  SemaExpr *input = resolve_expr(r, s->as.pipe.value);
  SemaVar *v = resolve_variable(r, "$pipe", s->as.pipe.value->span,
                                &r->program->types[SEMA_UNKNOWN], 0);
  SemaExpr *bind = node(r, SEMA_EXPR_BIND, input->span);
  bind->as.binding.variable = v;
  bind->as.binding.value = input;
  SemaExpr *result = stage->kind == EXPR_CALL ? pipe_call(r, stage, bind)
                                              : pipe_match(r, s, stage, bind);
  if (postfix) {
    resolve_handled(result);
    SemaExpr *unwrap =
        node(r, postfix->kind == EXPR_TRY ? SEMA_EXPR_TRY : SEMA_EXPR_PANIC,
             postfix->span);
    unwrap->as.value = result;
    result = unwrap;
  }
  SemaExpr *e = node(r, SEMA_EXPR_BLOCK, s->span);
  e->as.block.pipeline = 1;
  e->as.block.count = 2;
  e->as.block.items = arena_calloc(r->arena, 2, sizeof(SemaExpr *));
  e->as.block.items[0] = bind;
  e->as.block.items[1] = result;
  resolve_restore(r, saved);
  r->depth--;
  return e;
}
