#ifndef BRIEF_SEMA_INTERNAL_H
#define BRIEF_SEMA_INTERNAL_H

#include "sema/sema.h"

// Passes run by sema_check after types stabilize, plus the shared analyses
// they use. Not part of the public sema API.

// Intraprocedural read-only alias propagation, after types/flow stabilize.
// Does not alter canonical types or ownership.
void sema_check_permissions(SemaProgram *program, Diagnostics *errors);
// Local outcome obligations, using the same checked-tree flow framework.
void sema_check_outcomes(SemaProgram *program, Diagnostics *errors);
// Definite assignment for typed declarations without an initializer.
void sema_check_assignment(SemaProgram *program, Diagnostics *errors);
// Scalar normalization / symbol coverage after endpoint and input typing.
// NULL diagnostics probes coverage without emitting errors.
void sema_match_coverage(SemaProgram *program, SemaExpr *match,
                         Diagnostics *errors);
// Iterative Tarjan; components are numbered in callee-first order.
size_t sema_components(SemaProgram *program);

#endif
