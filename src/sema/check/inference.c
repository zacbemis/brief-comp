#include "sema/check/internal.h"

#include <stdint.h>

static const SemaType *check_function(SemaProgram *p, SemaFunction *f,
                                      Diagnostics *errors, int probe,
                                      int defaults) {
  Checker c = {.p = p,
               .function = f,
               .errors = errors,
               .probe = probe,
               .defaults = defaults,
               .reachable = 1,
               .candidate = &p->types[SEMA_UNKNOWN],
               .expected =
                   f->written ? f->written_type : f->symbol.outcome_type};
  if (unknown(c.expected)) {
    c.expected = check_return_hint(f->body);
    if (special(c.expected))
      c.expected = check_hint(f->body);
  }
  int discard = f->written && !f->partial && f->written_type->kind == SEMA_UNIT;
  const SemaType *body =
      check_expr(&c, f->body, c.expected, discard ? DISCARD : TAIL);
  if (f->body->flow & SEMA_NEXT) {
    if (discard)
      body = &p->types[SEMA_UNIT];
    body =
        check_returned(&c, body, f->body->span,
                       "Function body does not match the written return type.");
    if (!f->written || f->partial)
      c.candidate = check_return_join(&c, c.candidate, body, f->body->span,
                                      "Incompatible return types.");
  } else if (!c.saw_return)
    c.candidate = &p->types[SEMA_UNIT];
  const SemaType *result = c.candidate;
  if (c.failures)
    result = check_return_join(&c, result, c.failures, f->body->span,
                               "Symbol tag has conflicting payload types.");
  if (c.empty_return) {
    const SemaType *context = check_none_context(&c, result);
    const SemaType *empty =
        context ? check_none_value(&c, context) : &p->types[SEMA_UNIT];
    // Another function in the SCC can reveal :none on a later probe.
    const SemaVariant *ok = sema_ok(result);
    int wait = !f->written && !context &&
               ((!special(result) && result->kind != SEMA_SYM &&
                 result->kind != SEMA_UNIT) ||
                (ok && ok->payload_count));
    if (!wait) {
      empty = check_returned(
          &c, empty, c.empty_span,
          "Return value does not match the written return type.");
      result = check_return_join(&c, result, empty, c.empty_span,
                                 "Incompatible return types.");
    }
  }
  if (f->written && !f->partial)
    return f->written_type;
  if (f->partial)
    result = check_return_join(&c, result, f->written_type, f->body->span,
                               "Symbol tag has conflicting payload types.");
  return result;
}
void sema_check(SemaProgram *p, Diagnostics *errors) {
  size_t count = sema_components(p);
  for (size_t i = 0; i < p->count; i++) {
    p->functions[i].inference_conflict = 0;
    p->functions[i].inference_reported = 0;
    SemaFunction *f = &p->functions[i];
    if (!f->written) {
      f->symbol.outcome_type = &p->types[SEMA_UNKNOWN];
      f->symbol.return_type = &p->types[SEMA_UNKNOWN];
    } else {
      f->symbol.return_type = f->symbol.outcome_type = f->written_type;
    }
  }
  // Bucket members once: long acyclic call chains must not require quadratic
  // scans of the entire program, nor use the C stack.
  size_t *head = arena_alloc(p->arena, count * sizeof(*head));
  size_t *next = arena_alloc(p->arena, p->count * sizeof(*next));
  for (size_t i = 0; i < count; i++)
    head[i] = SIZE_MAX;
  for (size_t i = 0; i < p->count; i++) {
    next[i] = head[p->functions[i].component];
    head[p->functions[i].component] = i;
  }
  for (size_t component = 0; component < count; component++) {
    for (size_t i = head[component]; i != SIZE_MAX; i = next[i])
      (void)check_function(p, &p->functions[i], errors, 1, 0);
    // Resolve non-literal anchors first, then decimal defaults, then whole
    // defaults. A whole literal must not force int before a decimal anchor
    // elsewhere in the same recursive component has had a chance to propagate.
    for (int defaults = 0; defaults <= 2; defaults++) {
      int changed;
      do {
        changed = 0;
        for (size_t i = head[component]; i != SIZE_MAX; i = next[i]) {
          SemaFunction *f = &p->functions[i];
          const SemaType *inferred = check_function(p, f, errors, 1, defaults);
          // Concrete payload anchors grow monotonically. Valueless success
          // edges stay provisional until the component's absence is known.
          const SemaType *old = f->symbol.outcome_type;
          if ((!f->written || f->partial) && old->kind != SEMA_ERROR &&
              !unknown(inferred) && inferred != old) {
            const SemaType *previous = old;
            if (!f->written) {
              const SemaVariant *ok = sema_ok(old),
                                *next_ok = sema_ok(inferred);
              // Valueless edges are provisional, not fixed payload anchors.
              // They can become :none after another SCC member resolves.
              if (old->kind == SEMA_UNIT)
                previous = &p->types[SEMA_UNKNOWN];
              else if (ok && !ok->payload_count &&
                       (!next_ok || next_ok->payload_count))
                previous = sema_symbol_set(p, old->symbols->variants + 1,
                                           old->symbols->count - 1);
            }
            const SemaType *merged = inferred;
            if (!unknown(previous)) {
              Checker merge = {.p = p, .function = f, .probe = 1};
              merged =
                  check_return_join(&merge, previous, inferred, f->symbol.span,
                                    "Incompatible return types.");
            }
            f->symbol.outcome_type = merged;
            f->symbol.return_type = merged;
            changed |= merged != old;
          }
        }
      } while (changed);
    }
    for (size_t i = head[component]; i != SIZE_MAX; i = next[i]) {
      SemaFunction *f = &p->functions[i];
      int unresolved = unknown(f->symbol.return_type);
      const SemaType *final = check_function(p, f, errors, 0, 2);
      if (unresolved) {
        // A malformed handler may have stayed unknown while SCC outcomes
        // grew. Its final, specific error is not an unanchored recursion.
        if (final->kind != SEMA_ERROR) {
          diagnostics_error(
              errors, f->symbol.span,
              "Cannot infer recursive return type; write a return type.");
          f->inference_reported = 1;
        }
        f->symbol.return_type = f->symbol.outcome_type = &p->types[SEMA_ERROR];
      }
      // Unrelated errors, even in this function, must not hide a conflict.
      // Track its provenance rather than the total diagnostic count: avoid
      // cascades from frontend/callee error types and duplicate join errors.
      if (f->symbol.return_type->kind == SEMA_ERROR && f->inference_conflict &&
          !f->inference_reported) {
        diagnostics_error(errors, f->symbol.span,
                          "Cannot infer a consistent return type.");
        f->inference_reported = 1;
      }
    }
  }
  sema_check_assignment(p, errors);
  sema_check_outcomes(p, errors);
  sema_check_permissions(p, errors);
  p->checked = p->resolved && errors->count == 0;
}
