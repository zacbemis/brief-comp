#include "sema/sema.h"
#include <string.h>

typedef struct Local Local;
struct Local {
  SemaVar *variable;
  unsigned depth;
  Local *next;
};
typedef struct {
  Arena *arena;
  SemaProgram *program;
  SemaFunction *function;
  Diagnostics *errors;
  Local *locals;
  unsigned depth, loops;
} Resolver;
int sema_integer(const SemaType *t) {
  return t && t->kind >= SEMA_SHRT && t->kind <= SEMA_ULNG;
}
int sema_number(const SemaType *t) {
  return sema_integer(t) || (t && (t->kind == SEMA_FLT || t->kind == SEMA_DUB));
}
static const SemaType *type(Resolver *r, const Type *syntax) {
  if (!syntax)
    return &r->program->types[SEMA_UNKNOWN];
  if (syntax->kind == TYPE_UNIT)
    return &r->program->types[SEMA_UNIT];
  if (syntax->kind == TYPE_NAME)
    for (size_t i = SEMA_BOOL; i < SEMA_TYPE_COUNT; i++)
      if (!strcmp(syntax->as.name, r->program->types[i].name))
        return &r->program->types[i];
  diagnostics_error(r->errors, syntax->span,
                    "This type is unknown or not implemented yet.");
  return &r->program->types[SEMA_ERROR];
}
static SemaSymbol *lookup(SemaProgram *p, const char *name) {
  if (!strcmp(name, "print"))
    return &p->print;
  for (size_t i = 0; i < p->count; i++)
    if (!strcmp(name, p->functions[i].symbol.name))
      return &p->functions[i].symbol;
  return NULL;
}
static SemaVar *local(Resolver *r, const char *name) {
  for (Local *l = r->locals; l; l = l->next)
    if (!strcmp(l->variable->name, name))
      return l->variable;
  return NULL;
}
static SemaVar *variable(Resolver *r, const char *name, Span span,
                         const SemaType *annotation, int mutable) {
  for (Local *l = r->locals; l && l->depth == r->depth; l = l->next)
    if (!strcmp(l->variable->name, name)) {
      Diagnostic *d = diagnostics_error(r->errors, span,
                                        "Duplicate binding in this scope.");
      diagnostic_note(r->errors, d, l->variable->span, "First declared here.");
      break;
    }
  SemaFunction *f = r->function;
  SemaVar *v = arena_calloc(r->arena, 1, sizeof(*v));
  *v = (SemaVar){.name = name,
                 .span = span,
                 .id = f->variable_count,
                 .mutable = mutable,
                 .annotation = annotation,
                 .type = annotation};
  if (f->variable_count == f->variable_capacity) {
    size_t capacity = f->variable_capacity ? f->variable_capacity * 2 : 16;
    SemaVar **items = arena_calloc(r->arena, capacity, sizeof(*items));
    if (f->variable_count)
      memcpy(items, f->variables, f->variable_count * sizeof(*items));
    f->variables = items;
    f->variable_capacity = capacity;
  }
  f->variables[f->variable_count++] = v;
  Local *l = arena_alloc(r->arena, sizeof(*l));
  *l = (Local){v, r->depth, r->locals};
  r->locals = l;
  return v;
}
static SemaExpr *node(Resolver *r, SemaExprKind kind, Span span) {
  SemaExpr *e = arena_calloc(r->arena, 1, sizeof(*e));
  e->kind = kind;
  e->span = span;
  return e;
}
static SemaExpr *unsupported(Resolver *r, Span span, const char *message) {
  diagnostics_error(r->errors, span, message);
  return node(r, SEMA_EXPR_ERROR, span);
}
static SemaExpr *resolve_expr(Resolver *r, const Expr *syntax);
static SemaExpr *resolve_stmt(Resolver *r, const Stmt *s) {
  if (s->kind == STMT_EXPR)
    return resolve_expr(r, s->as.value);
  if (s->kind == STMT_INC || s->kind == STMT_DEC) {
    if (s->as.value->kind != EXPR_NAME)
      return unsupported(r, s->span,
                         "Field and index updates are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_UPDATE, s->span);
    e->as.binding.variable = local(r, s->as.value->as.name);
    if (!e->as.binding.variable)
      diagnostics_error(r->errors, s->as.value->span, "Unknown binding.");
    e->as.binding.op = s->kind == STMT_INC ? ASSIGN_ADD : ASSIGN_SUB;
    return e;
  }
  if (s->kind == STMT_ASSIGN) {
    if (s->as.assign.target->kind != EXPR_NAME)
      return unsupported(
          r, s->span, "Field and index assignments are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_ASSIGN, s->span);
    e->as.binding.variable = local(r, s->as.assign.target->as.name);
    if (!e->as.binding.variable)
      diagnostics_error(r->errors, s->as.assign.target->span,
                        "Unknown binding.");
    e->as.binding.value = resolve_expr(r, s->as.assign.value);
    e->as.binding.op = s->as.assign.op;
    return e;
  }
  SemaExprKind kind = s->kind == STMT_RET     ? SEMA_EXPR_RETURN
                      : s->kind == STMT_BREAK ? SEMA_EXPR_BREAK
                                              : SEMA_EXPR_CONTINUE;
  SemaExpr *e = node(r, kind, s->span);
  if (s->kind == STMT_RET && s->as.value)
    e->as.value = resolve_expr(r, s->as.value);
  if (s->kind != STMT_RET && !r->loops)
    diagnostics_error(r->errors, s->span,
                      "break and continue are only allowed inside loops.");
  return e;
}
static SemaExpr *resolve_expr(Resolver *r, const Expr *s) {
  SemaExpr *e;
  switch (s->kind) {
  case EXPR_UNIT:
    return node(r, SEMA_EXPR_UNIT, s->span);
  case EXPR_STRING:
  case EXPR_INT:
  case EXPR_FLOAT:
  case EXPR_BOOL:
  case EXPR_CHAR:
    e = node(r,
             s->kind == EXPR_STRING  ? SEMA_EXPR_STRING
             : s->kind == EXPR_INT   ? SEMA_EXPR_INT
             : s->kind == EXPR_FLOAT ? SEMA_EXPR_FLOAT
             : s->kind == EXPR_BOOL  ? SEMA_EXPR_BOOL
                                     : SEMA_EXPR_CHAR,
             s->span);
    e->as.string = s->as.literal;
    return e;
  case EXPR_NAME:
    e = node(r, SEMA_EXPR_VAR, s->span);
    e->as.variable = local(r, s->as.name);
    if (!e->as.variable)
      diagnostics_error(r->errors, s->span,
                        lookup(r->program, s->as.name)
                            ? "Function values are not implemented yet."
                            : "Unknown binding.");
    return e;
  case EXPR_UNARY:
    // Fold only a directly negated literal, allowing the signed minimum.
    if (s->as.unary.op == UNARY_NEG &&
        (s->as.unary.value->kind == EXPR_INT ||
         s->as.unary.value->kind == EXPR_FLOAT)) {
      e = resolve_expr(r, s->as.unary.value);
      e->negative = 1;
      e->span = s->span;
      return e;
    }
    e = node(r, SEMA_EXPR_UNARY, s->span);
    e->as.unary.op = s->as.unary.op;
    e->as.unary.value = resolve_expr(r, s->as.unary.value);
    return e;
  case EXPR_BINARY:
    if (s->as.binary.op == BINARY_RANGE)
      return unsupported(r, s->span, "Ranges are only supported in for loops.");
    e = node(r, SEMA_EXPR_BINARY, s->span);
    e->as.binary.op = s->as.binary.op;
    e->as.binary.left = resolve_expr(r, s->as.binary.left);
    e->as.binary.right = resolve_expr(r, s->as.binary.right);
    return e;
  case EXPR_CALL:
    e = node(r, SEMA_EXPR_CALL, s->span);
    if (s->as.call.type_count)
      diagnostics_error(r->errors, s->span,
                        "Generic calls are not implemented yet.");
    if (s->as.call.callee->kind != EXPR_NAME)
      diagnostics_error(
          r->errors, s->as.call.callee->span,
          "Only calls to plain function names are currently implemented.");
    else if (local(r, s->as.call.callee->as.name))
      diagnostics_error(r->errors, s->as.call.callee->span,
                        "A local binding is not callable.");
    else {
      e->as.call.target = lookup(r->program, s->as.call.callee->as.name);
      if (!e->as.call.target)
        diagnostics_error(r->errors, s->as.call.callee->span,
                          "Unknown function.");
      else if (!e->as.call.target->builtin) {
        SemaEdge *edge = arena_alloc(r->arena, sizeof(*edge));
        *edge = (SemaEdge){e->as.call.target->id, r->function->edges};
        r->function->edges = edge;
      }
    }
    e->as.call.count = s->as.call.count;
    e->as.call.args =
        arena_calloc(r->arena, e->as.call.count, sizeof(SemaExpr *));
    for (size_t i = 0; i < e->as.call.count; i++)
      e->as.call.args[i] = resolve_expr(r, s->as.call.args[i]);
    return e;
  case EXPR_BLOCK: {
    e = node(r, SEMA_EXPR_BLOCK, s->span);
    e->as.block.count = s->as.block.count;
    e->as.block.items =
        arena_calloc(r->arena, e->as.block.count, sizeof(SemaExpr *));
    Local *saved = r->locals;
    r->depth++;
    for (size_t i = 0; i < e->as.block.count; i++) {
      BlockItem item = s->as.block.items[i];
      SemaExpr *value;
      if (item.kind == BLOCK_ITEM_EXPR)
        value = resolve_expr(r, item.as.expr);
      else if (item.kind == BLOCK_ITEM_STMT)
        value = resolve_stmt(r, item.as.stmt);
      else if (item.as.decl->kind != DECL_BINDING)
        value = unsupported(
            r, item.span,
            "Local declarations other than bindings are not implemented yet.");
      else if (item.as.decl->as.binding.kind == BIND_CONST)
        value = unsupported(r, item.span,
                            "const bindings are not implemented yet.");
      else {
        const Decl *d = item.as.decl;
        value = node(r, SEMA_EXPR_BIND, d->span);
        value->as.binding.value = resolve_expr(r, d->as.binding.value);
        value->as.binding.variable =
            variable(r, d->name, d->span, type(r, d->as.binding.annotation),
                     d->as.binding.kind == BIND_VAR);
      }
      e->as.block.items[i] = value;
    }
    r->locals = saved;
    r->depth--;
    return e;
  }
  case EXPR_IF:
    e = node(r, SEMA_EXPR_IF, s->span);
    e->as.conditional.condition = resolve_expr(r, s->as.conditional.condition);
    e->as.conditional.yes = resolve_expr(r, s->as.conditional.then_branch);
    if (s->as.conditional.else_branch)
      e->as.conditional.no = resolve_expr(r, s->as.conditional.else_branch);
    return e;
  case EXPR_WHILE:
    e = node(r, SEMA_EXPR_WHILE, s->span);
    e->as.while_loop.condition = resolve_expr(r, s->as.while_loop.condition);
    r->loops++;
    e->as.while_loop.body = resolve_expr(r, s->as.while_loop.body);
    r->loops--;
    return e;
  case EXPR_FOR: {
    const Expr *range = s->as.for_loop.iterable;
    if (range->kind != EXPR_BINARY || range->as.binary.op != BINARY_RANGE)
      return unsupported(r, range->span,
                         "Only range for-loops are implemented yet.");
    e = node(r, SEMA_EXPR_FOR, s->span);
    e->as.for_loop.lower = resolve_expr(r, range->as.binary.left);
    e->as.for_loop.upper = resolve_expr(r, range->as.binary.right);
    Local *saved = r->locals;
    r->depth++;
    e->as.for_loop.variable = variable(r, s->as.for_loop.name, s->span,
                                       &r->program->types[SEMA_UNKNOWN], 0);
    r->loops++;
    e->as.for_loop.body = resolve_expr(r, s->as.for_loop.body);
    r->loops--;
    r->locals = saved;
    r->depth--;
    return e;
  }
  default:
    return unsupported(r, s->span, "This expression is not implemented yet.");
  }
}
SemaProgram *sema_resolve(Arena *a, const Program *syntax,
                          Diagnostics *errors) {
  SemaProgram *p = arena_calloc(a, 1, sizeof(*p));
  p->arena = a;
  const char *names[] = {"unknown", "never", "error", "()",  "bool",
                         "chr",     "shrt",  "ushrt", "int", "uint",
                         "lng",     "ulng",  "flt",   "dub", "str"};
  unsigned bits[] = {0, 0, 0, 0, 1, 8, 16, 16, 32, 32, 64, 64, 32, 64, 0};
  for (size_t i = 0; i < SEMA_TYPE_COUNT; i++)
    p->types[i] = (SemaType){(SemaTypeKind)i, names[i], bits[i],
                             i == SEMA_SHRT || i == SEMA_INT || i == SEMA_LNG};
  SemaVar *arg = arena_calloc(a, 1, sizeof(*arg));
  arg->type = &p->types[SEMA_STR];
  p->print = (SemaSymbol){.name = "print",
                          .arity = 1,
                          .builtin = 1,
                          .return_type = &p->types[SEMA_UNIT]};
  p->print.params = arena_alloc(a, sizeof(SemaVar *));
  p->print.params[0] = arg;
  p->functions = arena_calloc(a, syntax->count, sizeof(SemaFunction));
  Resolver r = {.arena = a, .program = p, .errors = errors};
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind != DECL_FN) {
      diagnostics_error(
          errors, d->span,
          "Only function declarations are currently implemented.");
      continue;
    }
    SemaSymbol *previous = lookup(p, d->name);
    if (previous) {
      Diagnostic *error = diagnostics_error(
          errors, d->span,
          previous->builtin ? "The name 'print' is reserved for the builtin."
                            : "Duplicate function name.");
      if (!previous->builtin)
        diagnostic_note(errors, error, previous->span, "First declared here.");
      continue;
    }
    SemaFunction *f = &p->functions[p->count];
    f->syntax = d;
    r.function = f;
    r.locals = NULL;
    r.depth = 0;
    f->written = d->as.function.return_type != NULL;
    f->symbol =
        (SemaSymbol){.name = d->name,
                     .span = d->span,
                     .id = p->count++,
                     .arity = d->as.function.count,
                     .return_type = type(&r, d->as.function.return_type)};
    f->symbol.params = arena_calloc(a, f->symbol.arity, sizeof(SemaVar *));
    for (size_t j = 0; j < f->symbol.arity; j++) {
      Parameter param = d->as.function.params[j];
      if (param.borrow == BORROW_MUT)
        diagnostics_error(errors, param.span,
                          "Mutable borrow parameters are not implemented yet.");
      f->symbol.params[j] =
          variable(&r, param.name, param.span, type(&r, param.type), 0);
    }
    if (!strcmp(d->name, "main")) {
      p->entry = &f->symbol;
      if (f->symbol.arity)
        diagnostics_error(errors, d->span, "main() cannot have parameters.");
    }
  }
  if (!p->entry)
    diagnostics_error(errors, syntax->span, "A program must define main().");
  for (size_t i = 0; i < p->count; i++) {
    r.function = &p->functions[i];
    r.locals = NULL;
    r.depth = 0;
    for (size_t j = 0; j < r.function->symbol.arity; j++) {
      Local *l = arena_alloc(a, sizeof(*l));
      *l = (Local){r.function->symbol.params[j], 0, r.locals};
      r.locals = l;
    }
    r.function->body = resolve_expr(&r, r.function->syntax->as.function.body);
  }
  p->resolved = errors->count == 0;
  return p;
}
