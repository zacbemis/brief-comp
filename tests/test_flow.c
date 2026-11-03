#include "sema/flow.h"
#include "test_utils.h"
#include <stdarg.h>

// An independent toy analysis: may facts join by union, must facts by
// intersection. In particular, neither a zero identity nor bytewise OR works
// for the whole state. Integers set/clear facts; variables observe them.
typedef struct {
  unsigned may, must;
} Facts;
typedef struct {
  SemaExpr *root;
  Facts exit, seen[8];
  int active[8];
  size_t visits[8], probes, finals;
  SemaFlowValue args[4], inputs[4];
  size_t arg_count, arm_count;
} Analysis;
static int merge(SemaFlow *flow, void *into, const void *from, SemaExpr *at) {
  Facts *a = into;
  const Facts *b = from;
  Facts old = *a;
  (void)flow;
  ASSERT_TRUE(at != NULL);
  a->may |= b->may;
  a->must &= b->must;
  return a->may != old.may || a->must != old.must;
}
static SemaFlowValue enter(SemaFlow *flow, SemaExpr *e, void *state) {
  Analysis *a = flow->context;
  Facts *facts = state;
  if (e == a->root) {
    if (flow->probe)
      a->probes++;
    else
      a->finals++;
  }
  if (e->kind == SEMA_EXPR_INT) {
    unsigned bits = (unsigned)e->integer;
    if (e->negative) {
      facts->may &= ~bits;
      facts->must &= ~bits;
    } else {
      facts->may |= bits;
      facts->must |= bits;
    }
  }
  if (!flow->probe && e->kind == SEMA_EXPR_VAR) {
    size_t id = (size_t)e->integer;
    ASSERT_TRUE(id < 8);
    if (id < 8) {
      a->seen[id] = *facts;
      a->active[id] = flow->active;
      a->visits[id]++;
    }
  }
  return facts->may;
}
static SemaFlowValue child(SemaFlow *flow, SemaExpr *e, size_t index,
                           void *state, SemaFlowValue result,
                           SemaFlowValue value) {
  Analysis *a = flow->context;
  (void)state;
  if (!flow->probe && e->kind == SEMA_EXPR_CALL) {
    ASSERT_EQ_INT(a->arg_count, index);
    ASSERT_TRUE(index < 4);
    if (index < 4)
      a->args[a->arg_count++] = value;
    if (index == 1)
      ASSERT_EQ_INT(a->args[0], result); // A snapshot, not a later state read.
  }
  return index ? result : value;
}
static SemaFlowValue leave(SemaFlow *flow, SemaExpr *e, void *state,
                           SemaFlowValue result) {
  Analysis *a = flow->context;
  if (!flow->probe && e == a->root)
    a->exit = *(Facts *)state;
  return result;
}
static void arm(SemaFlow *flow, SemaArm *arm, void *state,
                SemaFlowValue input) {
  Analysis *a = flow->context;
  (void)state;
  (void)arm;
  if (!flow->probe && a->arm_count < 4)
    a->inputs[a->arm_count++] = input;
}
static Analysis run(Arena *arena, SemaExpr *root, Facts entry) {
  SemaFlowAnalysis ops = {.state_size = sizeof(Facts),
                          .join = merge,
                          .enter = enter,
                          .child = child,
                          .leave = leave,
                          .arm = arm};
  Analysis a = {.root = root};
  Facts saved = entry;
  sema_flow_run(arena, &ops, &a, root, &entry);
  ASSERT_EQ_INT(saved.may, entry.may);
  ASSERT_EQ_INT(saved.must, entry.must);
  ASSERT_EQ_INT(1, a.finals);
  return a;
}
static SemaExpr *node(Arena *a, SemaExprKind kind) {
  SemaExpr *e = arena_calloc(a, 1, sizeof(*e));
  e->kind = kind;
  e->flow = kind == SEMA_EXPR_RETURN     ? SEMA_RETURN
            : kind == SEMA_EXPR_BREAK    ? SEMA_BREAK
            : kind == SEMA_EXPR_CONTINUE ? SEMA_CONTINUE
                                         : SEMA_NEXT;
  return e;
}
static SemaExpr *write_fact(Arena *a, unsigned bits, int clear) {
  SemaExpr *e = node(a, SEMA_EXPR_INT);
  e->integer = bits;
  e->negative = clear;
  return e;
}
static SemaExpr *observe(Arena *a, size_t id) {
  SemaExpr *e = node(a, SEMA_EXPR_VAR);
  e->integer = id;
  return e;
}
static SemaExpr *block(Arena *a, size_t count, ...) {
  SemaExpr *e = node(a, SEMA_EXPR_BLOCK);
  e->as.block.count = count;
  e->as.block.items = arena_calloc(a, count, sizeof(*e->as.block.items));
  va_list args;
  va_start(args, count);
  for (size_t i = 0; i < count; i++) {
    SemaExpr *item = va_arg(args, SemaExpr *);
    e->as.block.items[i] = item;
    if (e->flow & SEMA_NEXT)
      e->flow = (e->flow & ~SEMA_NEXT) | item->flow;
  }
  va_end(args);
  return e;
}
static SemaExpr *branch(Arena *a, SemaExpr *yes, SemaExpr *no) {
  SemaExpr *e = node(a, SEMA_EXPR_IF);
  e->as.conditional.condition = node(a, SEMA_EXPR_BOOL);
  e->as.conditional.yes = yes;
  e->as.conditional.no = no;
  e->flow = yes->flow | (no ? no->flow : SEMA_NEXT);
  return e;
}
static SemaExpr *loop(Arena *a, SemaExpr *body, int forever) {
  SemaExpr *e = node(a, SEMA_EXPR_WHILE);
  e->as.while_loop.condition = node(a, SEMA_EXPR_BOOL);
  e->as.while_loop.condition->integer = (uint64_t)forever;
  e->as.while_loop.body = body;
  e->flow = forever && !(body->flow & SEMA_BREAK) ? 0 : SEMA_NEXT;
  return e;
}
static int empty_join(SemaFlow *flow, void *into, const void *from,
                      SemaExpr *at) {
  (void)flow;
  (void)into;
  (void)from;
  (void)at;
  return 0;
}
int main(void) {
  Arena arena;
  arena_init(&arena);
  Arena *a = &arena;
  TEST("branches join may and must facts, without a hard-coded identity") {
    Analysis r =
        run(a, branch(a, write_fact(a, 1, 0), write_fact(a, 1, 0)), (Facts){0});
    ASSERT_EQ_INT(1, r.exit.may);
    ASSERT_EQ_INT(1, r.exit.must);
    ASSERT_EQ_INT(1, r.probes); // Temporary branch joins do not drive repeats.
    r = run(a, branch(a, write_fact(a, 1, 0), NULL), (Facts){0});
    ASSERT_EQ_INT(1, r.exit.may);
    ASSERT_EQ_INT(0, r.exit.must);
    r = run(a,
            branch(a,
                   block(a, 2, write_fact(a, 2, 0), node(a, SEMA_EXPR_RETURN)),
                   branch(a, write_fact(a, 1, 0), write_fact(a, 1, 0))),
            (Facts){0});
    ASSERT_EQ_INT(1, r.exit.may); // Returning arm contributes no state.
    ASSERT_EQ_INT(1, r.exit.must);
  }
  TEST("short-circuit joins include skipped and exclude returning RHS paths") {
    for (int op = BINARY_LOGICAL_AND; op <= BINARY_LOGICAL_OR; op++) {
      SemaExpr *e = node(a, SEMA_EXPR_BINARY);
      e->as.binary.op = (BinaryOp)op;
      e->as.binary.left = write_fact(a, 1, 0);
      e->as.binary.right = write_fact(a, 2, 0);
      Analysis r = run(a, e, (Facts){0});
      ASSERT_EQ_INT(3, r.exit.may);
      ASSERT_EQ_INT(1, r.exit.must);
      e->as.binary.right =
          block(a, 2, write_fact(a, 2, 0), node(a, SEMA_EXPR_RETURN));
      r = run(a, e, (Facts){0});
      ASSERT_EQ_INT(1, r.exit.may);
      ASSERT_EQ_INT(1, r.exit.must);
    }
  }
  TEST("match arms fork input state and capture the input before arm effects") {
    SemaExpr *e = node(a, SEMA_EXPR_MATCH);
    e->as.match.input = write_fact(a, 1, 0);
    e->as.match.count = 4;
    SemaArm *arms = arena_calloc(a, 4, sizeof(*arms));
    e->as.match.arms = arms;
    arms[0] = (SemaArm){.useful = 1,
                        .body = block(a, 3, observe(a, 0), write_fact(a, 1, 1),
                                      write_fact(a, 2, 0))};
    arms[1] = (SemaArm){
        .useful = 1, .body = block(a, 2, observe(a, 1), write_fact(a, 2, 0))};
    arms[2] = (SemaArm){
        .useful = 1,
        .body = block(a, 2, write_fact(a, 4, 0), node(a, SEMA_EXPR_RETURN))};
    arms[3] = (SemaArm){
        .useful = 0, .body = block(a, 2, observe(a, 2), write_fact(a, 8, 0))};
    Analysis r = run(a, e, (Facts){0});
    ASSERT_EQ_INT(3, r.exit.may);
    ASSERT_EQ_INT(2, r.exit.must);
    ASSERT_EQ_INT(1, r.seen[0].may);
    ASSERT_EQ_INT(1, r.seen[1].may);
    ASSERT_EQ_INT(0, r.active[2]); // Useless syntax still gets visited.
    ASSERT_EQ_INT(4, r.arm_count);
    for (size_t i = 0; i < r.arm_count; i++)
      ASSERT_EQ_INT(1, r.inputs[i]);
  }
  TEST("loops retain zero-iteration paths but while true exits only by break") {
    Analysis r = run(a, loop(a, write_fact(a, 2, 1), 0), (Facts){3, 3});
    ASSERT_EQ_INT(3, r.exit.may);
    ASSERT_EQ_INT(1, r.exit.must);
    r = run(
        a,
        loop(a, block(a, 2, write_fact(a, 2, 1), node(a, SEMA_EXPR_BREAK)), 1),
        (Facts){3, 3});
    ASSERT_EQ_INT(1, r.exit.may);
    ASSERT_EQ_INT(1, r.exit.must);
    // A non-terminating loop preserves state when checking following syntax.
    r = run(a, block(a, 2, loop(a, node(a, SEMA_EXPR_UNIT), 1), observe(a, 0)),
            (Facts){1, 1});
    ASSERT_EQ_INT(1, r.seen[0].may);
    ASSERT_EQ_INT(0, r.active[0]);
  }
  TEST("nested break/continue edges and dead tails target the right loop") {
    SemaExpr *inner =
        loop(a, block(a, 2, write_fact(a, 1, 0), node(a, SEMA_EXPR_BREAK)), 1);
    SemaExpr *body =
        block(a, 5, inner, write_fact(a, 2, 0), node(a, SEMA_EXPR_CONTINUE),
              observe(a, 0), write_fact(a, 4, 0));
    Analysis r = run(a, loop(a, body, 0), (Facts){0});
    ASSERT_EQ_INT(3, r.exit.may);
    ASSERT_EQ_INT(0, r.exit.must);
    ASSERT_EQ_INT(0, r.active[0]);
    ASSERT_EQ_INT(1, r.visits[0]); // No repeated final-pass diagnostics.
    ASSERT_TRUE(r.probes >= 2 && r.probes <= 4);
  }
  TEST("for bounds precede the loop and call children carry snapshots") {
    SemaExpr *e = node(a, SEMA_EXPR_FOR);
    e->as.for_loop.lower = write_fact(a, 1, 0);
    e->as.for_loop.upper = write_fact(a, 2, 0);
    e->as.for_loop.body =
        block(a, 2, observe(a, 0), node(a, SEMA_EXPR_CONTINUE));
    Analysis r = run(a, e, (Facts){0});
    ASSERT_EQ_INT(3, r.exit.must);
    ASSERT_EQ_INT(3, r.seen[0].may);
    SemaExpr *call = node(a, SEMA_EXPR_CALL);
    SemaExpr *args[] = {observe(a, 0), write_fact(a, 2, 0), observe(a, 1)};
    call->as.call.args = args;
    call->as.call.count = 3;
    r = run(a, call, (Facts){1, 1});
    ASSERT_EQ_INT(3, r.arg_count);
    ASSERT_EQ_INT(1, r.args[0]);
    ASSERT_EQ_INT(3, r.args[1]);
    ASSERT_EQ_INT(3, r.args[2]);
    ASSERT_EQ_INT(1, r.seen[0].may);
    ASSERT_EQ_INT(3, r.seen[1].may);
  }
  TEST("deep loops use whole-tree passes and a fresh run resets headers") {
    SemaExpr *e = write_fact(a, 1, 0);
    for (size_t i = 0; i < 25; i++)
      e = loop(a, e, 0);
    Analysis r = run(a, e, (Facts){0});
    ASSERT_EQ_INT(1, r.exit.may);
    ASSERT_EQ_INT(0, r.exit.must);
    ASSERT_TRUE(r.probes <= 27);
    r = run(a, e, (Facts){2, 2});
    ASSERT_EQ_INT(3, r.exit.may);
    ASSERT_EQ_INT(2, r.exit.must);
    ASSERT_TRUE(r.probes <= 27);
    SemaFlowAnalysis empty = {.state_size = 0, .join = empty_join};
    sema_flow_run(a, &empty, NULL, branch(a, node(a, SEMA_EXPR_UNIT), NULL),
                  NULL);
  }
  arena_destroy(a);
  RUN_TESTS();
}
