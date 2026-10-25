#include "sema/sema.h"
#include <string.h>

typedef struct Local Local;
struct Local {
  SemaVar *variable;
  unsigned depth;
  Local *next;
  Local *shadowed; // The visible binding with this name it hides, if any.
};
typedef struct {
  SemaSymbol *first, *last;
} MethodChain;
typedef struct {
  Arena *arena;
  SemaProgram *program;
  SemaFunction *function;
  SemaStruct *owner;
  Diagnostics *errors;
  Local *locals;
  NameMap local_names; // Each visible name's innermost binding.
  unsigned depth, loops;
  int pipeline_call;
} Resolver;
int sema_integer(const SemaType *t) {
  return t && t->kind >= SEMA_SHRT && t->kind <= SEMA_ULNG;
}
int sema_number(const SemaType *t) {
  return sema_integer(t) || (t && (t->kind == SEMA_FLT || t->kind == SEMA_DUB));
}
int sema_reference(const SemaType *t) {
  return t &&
         (t->kind == SEMA_STR || t->kind == SEMA_STRUCT || t->kind == SEMA_SYM);
}
static SemaStruct *structure(SemaProgram *p, const char *name) {
  return name_map_get(&p->struct_names, NULL, name);
}
static SemaStruct *named_structure(Resolver *r, const char *name) {
  return !strcmp(name, "Self") ? r->owner : structure(r->program, name);
}
SemaField *sema_field(const SemaType *t, const char *name) {
  if (!t || t->kind != SEMA_STRUCT || !t->structure)
    return NULL;
  for (size_t i = 0; i < t->structure->count; i++)
    if (!strcmp(name, t->structure->fields[i].name))
      return &t->structure->fields[i];
  return NULL;
}
SemaSymbol *sema_method(SemaProgram *p, const SemaType *t, const char *name) {
  if (!t || t->kind != SEMA_STRUCT || !t->structure)
    return NULL;
  return name_map_get(&p->function_names, t->structure, name);
}
static SemaAlias *alias(SemaProgram *p, const char *name) {
  return name_map_get(&p->alias_names, NULL, name);
}
static SemaTag *tag(Resolver *r, const char *name, Span span) {
  SemaTag *result = sema_tag(r->program, name);
  if (!result)
    diagnostics_error(r->errors, span, "Too many symbol tags.");
  return result;
}
static const SemaType *type(Resolver *r, const Type *syntax) {
  if (!syntax)
    return &r->program->types[SEMA_UNKNOWN];
  if (syntax->kind == TYPE_UNIT)
    return &r->program->types[SEMA_UNIT];
  if (syntax->kind == TYPE_SELF && r->owner)
    return &r->owner->type;
  if (syntax->kind == TYPE_NAME) {
    for (size_t i = SEMA_BOOL; i < SEMA_STRUCT; i++)
      if (!strcmp(syntax->as.name, r->program->types[i].name))
        return &r->program->types[i];
    SemaStruct *s = structure(r->program, syntax->as.name);
    if (s)
      return &s->type;
    SemaAlias *a = alias(r->program, syntax->as.name);
    if (a)
      return a->type ? a->type : &r->program->types[SEMA_ERROR];
  }
  if (syntax->kind == TYPE_SYM) {
    const SemaType *result = NULL;
    int valid = 1;
    for (size_t i = 0; i < syntax->as.sym.count; i++) {
      SymEntry entry = syntax->as.sym.items[i];
      const SemaType *part;
      if (entry.tag) {
        SemaTag *name = tag(r, entry.tag, entry.span);
        const SemaType *payload = entry.type ? type(r, entry.type) : NULL;
        if (!name || (payload && payload->kind == SEMA_ERROR)) {
          valid = 0;
          continue;
        }
        SemaVariant variant = {name, payload};
        part = sema_symbol_set(r->program, &variant, 1);
      } else if (entry.type->kind == TYPE_INFER) {
        diagnostics_error(
            r->errors, entry.span,
            "Partial symbol sets are only allowed as function return types.");
        valid = 0;
        continue;
      } else {
        part = type(r, entry.type);
        if (part->kind != SEMA_SYM) {
          if (part->kind != SEMA_ERROR)
            diagnostics_error(r->errors, entry.span,
                              "A symbol-set inclusion requires a symbol set.");
          valid = 0;
          continue;
        }
      }
      result = result ? sema_symbol_union(r->program, result, part) : part;
      if (!result) {
        diagnostics_error(r->errors, entry.span,
                          "Symbol tag has conflicting payload types.");
        return &r->program->types[SEMA_ERROR];
      }
    }
    return valid && result ? result : &r->program->types[SEMA_ERROR];
  }
  diagnostics_error(r->errors, syntax->span,
                    "This type is unknown or not implemented yet.");
  return &r->program->types[SEMA_ERROR];
}
typedef struct AliasEdge AliasEdge;
struct AliasEdge {
  SemaAlias *target;
  Span span;
  AliasEdge *next;
};
static void alias_dependencies(Resolver *r, const Type *syntax,
                               AliasEdge **head) {
  // Syntax depth is parser-limited. Alias chains themselves use no C recursion.
  if (syntax->kind == TYPE_NAME) {
    SemaAlias *a = alias(r->program, syntax->as.name);
    if (a) {
      AliasEdge *edge = arena_alloc(r->arena, sizeof(*edge));
      *edge = (AliasEdge){a, syntax->span, *head};
      *head = edge;
    }
  } else if (syntax->kind == TYPE_SYM) {
    for (size_t i = 0; i < syntax->as.sym.count; i++)
      if (syntax->as.sym.items[i].type)
        alias_dependencies(r, syntax->as.sym.items[i].type, head);
  }
}
static void resolve_aliases(Resolver *r) {
  SemaProgram *p = r->program;
  AliasEdge **edges = arena_calloc(r->arena, p->alias_count, sizeof(*edges));
  unsigned char *state = arena_calloc(r->arena, p->alias_count, 1);
  typedef struct {
    SemaAlias *alias;
    AliasEdge *edge;
  } Frame;
  Frame *stack = arena_calloc(r->arena, p->alias_count, sizeof(*stack));
  for (size_t i = 0; i < p->alias_count; i++)
    alias_dependencies(r, p->aliases[i].syntax->as.alias, &edges[i]);
  for (size_t i = 0; i < p->alias_count; i++) {
    if (state[i])
      continue;
    size_t top = 0;
    state[i] = 1;
    stack[top++] = (Frame){&p->aliases[i], edges[i]};
    while (top) {
      Frame *frame = &stack[top - 1];
      if (frame->edge) {
        AliasEdge *edge = frame->edge;
        frame->edge = edge->next;
        size_t id = (size_t)(edge->target - p->aliases);
        if (!state[id]) {
          state[id] = 1;
          stack[top++] = (Frame){edge->target, edges[id]};
        } else if (state[id] == 1) {
          diagnostics_error(
              r->errors, edge->span,
              "Recursive named types require a struct indirection.");
          edge->target->type = &p->types[SEMA_ERROR];
        }
      } else {
        SemaAlias *a = frame->alias;
        if (!a->type)
          a->type = type(r, a->syntax->as.alias);
        if (a->type->kind != SEMA_SYM && a->type->kind != SEMA_ERROR) {
          diagnostics_error(r->errors, a->syntax->span,
                            "Named types currently require a symbol set.");
          a->type = &p->types[SEMA_ERROR];
        }
        state[a - p->aliases] = 2;
        top--;
      }
    }
  }
}
static const SemaType *return_type(Resolver *r, const Type *syntax,
                                   SemaFunction *f) {
  if (!syntax || syntax->kind != TYPE_SYM)
    return type(r, syntax);
  Type copy = *syntax;
  copy.as.sym.items =
      arena_calloc(r->arena, syntax->as.sym.count, sizeof(SymEntry));
  copy.as.sym.count = 0;
  for (size_t i = 0; i < syntax->as.sym.count; i++) {
    SymEntry e = syntax->as.sym.items[i];
    if (!e.tag && e.type->kind == TYPE_INFER) {
      if (f->partial)
        diagnostics_error(r->errors, e.span,
                          "Duplicate partial return marker.");
      f->partial = 1;
    } else
      copy.as.sym.items[copy.as.sym.count++] = e;
  }
  const SemaType *result = type(r, &copy);
  if (f->partial && result->kind != SEMA_ERROR && !sema_ok(result))
    diagnostics_error(r->errors, syntax->span,
                      "A partial return type must declare :ok.");
  return result;
}
static void handled(SemaExpr *e) {
  if (e->kind == SEMA_EXPR_CALL)
    e->as.call.handled = 1;
  else if (e->kind == SEMA_EXPR_BLOCK && e->as.block.pipeline)
    handled(e->as.block.items[e->as.block.count - 1]);
}
static SemaSymbol *lookup(SemaProgram *p, const char *name) {
  if (!strcmp(name, "print"))
    return &p->print;
  if (!strcmp(name, "str"))
    return &p->stringify;
  for (size_t i = 0; i < sizeof(p->converters) / sizeof(*p->converters); i++)
    if (!strcmp(name, p->converters[i].name))
      return &p->converters[i];
  return name_map_get(&p->function_names, NULL, name);
}
static SemaVar *local(Resolver *r, const char *name) {
  Local *l = name_map_get(&r->local_names, NULL, name);
  return l ? l->variable : NULL;
}
static void push_local(Resolver *r, SemaVar *v) {
  Local *l = arena_alloc(r->arena, sizeof(*l));
  *l = (Local){v, r->depth, r->locals, NULL};
  l->shadowed = name_map_put(&r->local_names, NULL, v->name, l);
  r->locals = l;
}
// Leaves the scopes entered since saved, making hidden bindings visible again.
static void restore(Resolver *r, Local *saved) {
  for (Local *l = r->locals; l != saved; l = l->next)
    name_map_put(&r->local_names, NULL, l->variable->name, l->shadowed);
  r->locals = saved;
}
static SemaVar *variable(Resolver *r, const char *name, Span span,
                         const SemaType *annotation, int mutable) {
  // A visible binding at this depth can only belong to the current scope.
  Local *existing = name_map_get(&r->local_names, NULL, name);
  if (existing && existing->depth == r->depth) {
    Diagnostic *d =
        diagnostics_error(r->errors, span, "Duplicate binding in this scope.");
    diagnostic_note(r->errors, d, existing->variable->span,
                    "First declared here.");
  }
  SemaFunction *f = r->function;
  SemaVar *v = arena_calloc(r->arena, 1, sizeof(*v));
  *v = (SemaVar){.name = name,
                 .span = span,
                 .id = f->variable_count,
                 .mutable = mutable,
                 .data_mutable = 1,
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
  push_local(r, v);
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
    if (s->as.value->kind == EXPR_INDEX)
      return unsupported(r, s->span, "Index updates are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_UPDATE, s->span);
    if (s->as.value->kind == EXPR_FIELD)
      e->as.binding.target = resolve_expr(r, s->as.value);
    else {
      e->as.binding.variable = local(r, s->as.value->as.name);
      if (!e->as.binding.variable)
        diagnostics_error(r->errors, s->as.value->span, "Unknown binding.");
    }
    e->as.binding.op = s->kind == STMT_INC ? ASSIGN_ADD : ASSIGN_SUB;
    return e;
  }
  if (s->kind == STMT_ASSIGN) {
    if (s->as.assign.target->kind == EXPR_INDEX)
      return unsupported(r, s->span,
                         "Index assignments are not implemented yet.");
    SemaExpr *e = node(r, SEMA_EXPR_ASSIGN, s->span);
    if (s->as.assign.target->kind == EXPR_FIELD)
      e->as.binding.target = resolve_expr(r, s->as.assign.target);
    else {
      e->as.binding.variable = local(r, s->as.assign.target->as.name);
      if (!e->as.binding.variable)
        diagnostics_error(r->errors, s->as.assign.target->span,
                          "Unknown binding.");
    }
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
    if (s->kind == EXPR_STRING) {
      Token token = s->as.literal;
      unsigned char *bytes = arena_alloc(r->arena, token.length);
      size_t length = 0;
      for (size_t i = 1; i + 1 < token.length; i++) {
        unsigned char c = (unsigned char)token.start[i];
        if (c == '\\') {
          c = (unsigned char)token.start[++i];
          c = c == 'n' ? '\n' : c == 't' ? '\t' : c == '0' ? 0 : c;
        }
        bytes[length++] = c;
      }
      bytes[length] = 0;
      e->as.text.bytes = bytes;
      e->as.text.length = length;
    } else
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
    int pipeline = r->pipeline_call;
    r->pipeline_call = 0;
    int generic = s->as.call.type_count != 0;
    if (generic)
      diagnostics_error(r->errors, s->span,
                        "Generic calls are not implemented yet.");
    const Expr *callee = s->as.call.callee;
    SemaExpr *receiver = NULL;
    if (callee->kind == EXPR_FIELD) {
      SemaStruct *owner =
          callee->as.field.object->kind == EXPR_NAME &&
                  !local(r, callee->as.field.object->as.name)
              ? named_structure(r, callee->as.field.object->as.name)
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
          if (!local(r, e->as.call.method))
            e->as.call.fallback = lookup(r->program, e->as.call.method);
        }
      }
    } else if (!generic) {
      if (callee->kind != EXPR_NAME)
        diagnostics_error(r->errors, callee->span,
                          "Function values are not implemented yet.");
      else if (!pipeline && local(r, callee->as.name))
        diagnostics_error(r->errors, callee->span,
                          "A local binding is not callable.");
      else {
        e->as.call.target = lookup(r->program, callee->as.name);
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
      e->as.call.shadowed = local(r, callee->as.name) != NULL;
    }
    // Instance receiver types may be inferred later. Conservative edges to
    // matching methods keep every possible callee in the SCC schedule. Edges
    // are added in declaration order, merging the target into the chain.
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
    e->as.call.count = s->as.call.count + (receiver != NULL);
    e->as.call.args =
        arena_calloc(r->arena, e->as.call.count, sizeof(SemaExpr *));
    if (receiver)
      e->as.call.args[0] = receiver;
    for (size_t i = 0; i < s->as.call.count; i++)
      e->as.call.args[i + (receiver != NULL)] =
          resolve_expr(r, s->as.call.args[i]);
    return e;
  case EXPR_TRY:
  case EXPR_PANIC:
    e = node(r, s->kind == EXPR_TRY ? SEMA_EXPR_TRY : SEMA_EXPR_PANIC, s->span);
    e->as.value = resolve_expr(r, s->as.postfix);
    handled(e->as.value);
    return e;
  case EXPR_PIPE: {
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
    SemaVar *v = variable(r, "$pipe", s->as.pipe.value->span,
                          &r->program->types[SEMA_UNKNOWN], 0);
    SemaExpr *bind = node(r, SEMA_EXPR_BIND, input->span);
    bind->as.binding.variable = v;
    bind->as.binding.value = input;
    SemaExpr *result;
    if (stage->kind == EXPR_CALL) {
      Expr arg = {.kind = EXPR_NAME, .span = input->span};
      arg.as.name = "$pipe";
      Expr call = *stage;
      call.as.call.count++;
      call.as.call.args =
          arena_calloc(r->arena, call.as.call.count, sizeof(Expr *));
      call.as.call.args[0] = &arg;
      for (size_t i = 0; i < stage->as.call.count; i++)
        call.as.call.args[i + 1] = stage->as.call.args[i];
      r->pipeline_call = stage->as.call.callee->kind == EXPR_NAME;
      result = resolve_expr(r, &call);
      bind->as.binding.stage = result;
    } else {
      handled(input);
      result = node(r, SEMA_EXPR_MATCH, s->span);
      SemaExpr *read = node(r, SEMA_EXPR_VAR, input->span);
      read->as.variable = v;
      result->as.match.input = read;
      result->as.match.count = stage->as.match.count;
      result->as.match.arms =
          arena_calloc(r->arena, stage->as.match.count, sizeof(SemaArm));
      for (size_t i = 0; i < stage->as.match.count; i++) {
        MatchArm source = stage->as.match.arms[i];
        const Pattern *p = source.pattern;
        SemaArm *a = &result->as.match.arms[i];
        if (p->kind == PAT_SYMBOL || p->kind == PAT_SET ||
            (p->kind == PAT_BIND && alias(r->program, p->as.name)))
          bind->as.binding.outcome = 1;
        a->kind = p->kind;
        a->span = p->span;
        Local *arm_scope = r->locals;
        r->depth++;
        SemaAlias *named = (p->kind == PAT_BIND || p->kind == PAT_SET)
                               ? alias(r->program, p->as.name)
                               : NULL;
        if (named) {
          a->kind = PAT_SET;
          a->set = named->type;
        } else if (p->kind == PAT_BIND)
          a->binding = variable(r, p->as.name, p->span,
                                &r->program->types[SEMA_UNKNOWN], 0);
        else if (p->kind == PAT_SYMBOL) {
          a->tag = tag(r, p->as.symbol.tag, p->span);
          const Pattern *payload = p->as.symbol.payload;
          a->payload = payload != NULL;
          if (!a->tag)
            a->invalid = 1;
          if (payload && payload->kind == PAT_BIND &&
              !alias(r->program, payload->as.name))
            a->binding = variable(r, payload->as.name, payload->span,
                                  &r->program->types[SEMA_UNKNOWN], 0);
          else if (payload && payload->kind != PAT_WILDCARD) {
            a->invalid = 1;
            diagnostics_error(
                r->errors, payload->span,
                "Refutable symbol payload patterns are not implemented yet.");
          }
        } else if (p->kind == PAT_LITERAL)
          a->lower = resolve_expr(r, p->as.literal);
        else if (p->kind == PAT_RANGE) {
          a->lower = resolve_expr(r, p->as.range.lower);
          a->upper = resolve_expr(r, p->as.range.upper);
        } else if (p->kind == PAT_COMPARE) {
          a->comparison = p->as.compare.op;
          a->lower = resolve_expr(r, p->as.compare.value);
        } else if (p->kind != PAT_WILDCARD) {
          a->invalid = 1;
          diagnostics_error(r->errors, p->span, "Unknown named symbol set.");
        }
        a->body = resolve_expr(r, source.value);
        restore(r, arm_scope);
        r->depth--;
      }
    }
    if (postfix) {
      handled(result);
      SemaExpr *unwrap =
          node(r, postfix->kind == EXPR_TRY ? SEMA_EXPR_TRY : SEMA_EXPR_PANIC,
               postfix->span);
      unwrap->as.value = result;
      result = unwrap;
    }
    e = node(r, SEMA_EXPR_BLOCK, s->span);
    e->as.block.pipeline = 1;
    e->as.block.count = 2;
    e->as.block.items = arena_calloc(r->arena, 2, sizeof(SemaExpr *));
    e->as.block.items[0] = bind;
    e->as.block.items[1] = result;
    restore(r, saved);
    r->depth--;
    return e;
  }
  case EXPR_SYMBOL:
    e = node(r, SEMA_EXPR_SYMBOL, s->span);
    e->as.symbol.tag = tag(r, s->as.symbol.tag, s->span);
    if (s->as.symbol.payload)
      e->as.symbol.payload = resolve_expr(r, s->as.symbol.payload);
    return e;
  case EXPR_FIELD:
    e = node(r, SEMA_EXPR_FIELD, s->span);
    e->as.field.object = resolve_expr(r, s->as.field.object);
    e->as.field.name = s->as.field.name;
    return e;
  case EXPR_STRUCT_LIT: {
    SemaStruct *owner = s->as.structure.type->kind == EXPR_NAME
                            ? named_structure(r, s->as.structure.type->as.name)
                            : NULL;
    if (!owner)
      return unsupported(r, s->span, "Unknown struct type.");
    e = node(r, SEMA_EXPR_STRUCT, s->span);
    e->as.structure.type = owner;
    e->as.structure.count = s->as.structure.count;
    e->as.structure.values =
        arena_calloc(r->arena, s->as.structure.count, sizeof(SemaExpr *));
    e->as.structure.fields =
        arena_calloc(r->arena, s->as.structure.count, sizeof(SemaField *));
    unsigned char *seen = arena_calloc(r->arena, owner->count, 1);
    for (size_t i = 0; i < s->as.structure.count; i++) {
      FieldValue f = s->as.structure.fields[i];
      SemaField *field = sema_field(&owner->type, f.name);
      if (!field)
        diagnostics_error(r->errors, f.span, "Unknown struct field.");
      else if (seen[field->id])
        diagnostics_error(r->errors, f.span,
                          "Duplicate struct field initializer.");
      else
        seen[field->id] = 1;
      e->as.structure.fields[i] = field;
      e->as.structure.values[i] = resolve_expr(r, f.value);
    }
    for (size_t i = 0; i < owner->count; i++)
      if (!seen[i])
        diagnostics_error(r->errors, s->span,
                          "Missing struct field initializer.");
    return e;
  }
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
    restore(r, saved);
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
    restore(r, saved);
    r->depth--;
    return e;
  }
  default:
    return unsupported(r, s->span, "This expression is not implemented yet.");
  }
}
static void collect_function(Resolver *r, const Decl *d, SemaStruct *owner) {
  SemaProgram *p = r->program;
  if (d->kind != DECL_FN) {
    diagnostics_error(r->errors, d->span,
                      "Only functions are supported inside impl.");
    return;
  }
  SemaSymbol *previous =
      owner ? sema_method(p, &owner->type, d->name) : lookup(p, d->name);
  if (previous) {
    Diagnostic *error = diagnostics_error(
        r->errors, d->span,
        previous->builtin == SEMA_BUILTIN_PRINT
            ? "The name 'print' is reserved for the builtin."
        : previous->builtin == SEMA_BUILTIN_STR
            ? "The name 'str' is reserved for the builtin."
        : previous->builtin == SEMA_BUILTIN_CONVERT
            ? "Numeric type names are reserved for builtin conversions."
            : "Duplicate function name.");
    if (!previous->builtin)
      diagnostic_note(r->errors, error, previous->span, "First declared here.");
    return;
  }
  if (!owner && alias(p, d->name)) {
    diagnostics_error(r->errors, d->span,
                      "A function and a named type cannot share a name.");
    return;
  }
  if (!owner && structure(p, d->name)) {
    diagnostics_error(r->errors, d->span,
                      "A function and a struct cannot share a name.");
    return;
  }
  SemaFunction *f = &p->functions[p->count];
  f->syntax = d;
  r->function = f;
  r->owner = owner;
  restore(r, NULL);
  r->depth = 0;
  f->written = d->as.function.return_type != NULL;
  f->symbol = (SemaSymbol){.name = d->name,
                           .span = d->span,
                           .id = p->count++,
                           .arity = d->as.function.count,
                           .owner = owner};
  name_map_put(&p->function_names, owner, d->name, &f->symbol);
  if (owner) {
    MethodChain *chain = name_map_get(&p->method_names, NULL, d->name);
    if (!chain) {
      chain = arena_calloc(r->arena, 1, sizeof(*chain));
      name_map_put(&p->method_names, NULL, d->name, chain);
    }
    if (chain->last)
      chain->last->next_method = &f->symbol;
    else
      chain->first = &f->symbol;
    chain->last = &f->symbol;
  }
  f->written_type = return_type(r, d->as.function.return_type, f);
  f->symbol.outcome_type =
      f->written_type->kind == SEMA_SYM || f->written_type->kind <= SEMA_ERROR
          ? f->written_type
          : sema_success(p, f->written_type);
  f->symbol.return_type = sema_result(p, f->symbol.outcome_type);
  f->symbol.params = arena_calloc(r->arena, f->symbol.arity, sizeof(SemaVar *));
  for (size_t j = 0; j < f->symbol.arity; j++) {
    Parameter param = d->as.function.params[j];
    SemaVar *v = variable(r, param.name, param.span, type(r, param.type), 0);
    v->data_mutable = param.borrow == BORROW_MUT;
    if (v->data_mutable && v->type->kind != SEMA_STRUCT &&
        v->type->kind != SEMA_SYM && v->type->kind != SEMA_ERROR)
      diagnostics_error(
          r->errors, param.span,
          "Mutable borrowing currently requires a struct or symbol type.");
    f->symbol.params[j] = v;
  }
  if (!owner && !strcmp(d->name, "main")) {
    p->entry = &f->symbol;
    if (f->symbol.arity)
      diagnostics_error(r->errors, d->span, "main() cannot have parameters.");
  }
}
SemaProgram *sema_resolve(Arena *a, const Program *syntax,
                          Diagnostics *errors) {
  SemaProgram *p = arena_calloc(a, 1, sizeof(*p));
  p->arena = a;
  name_map_init(&p->struct_names, a);
  name_map_init(&p->alias_names, a);
  name_map_init(&p->tag_names, a);
  name_map_init(&p->function_names, a);
  name_map_init(&p->method_names, a);
  const char *names[] = {"unknown", "never", "error", "()",     "bool", "chr",
                         "shrt",    "ushrt", "int",   "uint",   "lng",  "ulng",
                         "flt",     "dub",   "str",   "struct", "Sym"};
  unsigned bits[] = {0, 0, 0, 0, 1, 8, 16, 16, 32, 32, 64, 64, 32, 64, 0, 0, 0};
  for (size_t i = 0; i < SEMA_TYPE_COUNT; i++)
    p->types[i] = (SemaType){.kind = (SemaTypeKind)i,
                             .name = names[i],
                             .bits = bits[i],
                             .is_signed = i == SEMA_SHRT || i == SEMA_INT ||
                                          i == SEMA_LNG};
  p->ok = sema_tag(p, "ok");
  p->print = (SemaSymbol){.name = "print",
                          .arity = 1,
                          .builtin = SEMA_BUILTIN_PRINT,
                          .return_type = &p->types[SEMA_UNIT]};
  p->stringify = (SemaSymbol){.name = "str",
                              .arity = 1,
                              .builtin = SEMA_BUILTIN_STR,
                              .return_type = &p->types[SEMA_STR]};
  p->print.outcome_type = sema_success(p, p->print.return_type);
  p->stringify.outcome_type = sema_success(p, p->stringify.return_type);
  p->print.params = arena_calloc(a, 1, sizeof(SemaVar *));
  p->print.params[0] = arena_calloc(a, 1, sizeof(SemaVar));
  p->print.params[0]->type = &p->types[SEMA_STR];
  p->stringify.params = arena_calloc(a, 1, sizeof(SemaVar *));
  p->stringify.params[0] = arena_calloc(a, 1, sizeof(SemaVar));
  p->stringify.params[0]->type = &p->types[SEMA_UNKNOWN];
  for (size_t i = SEMA_SHRT; i <= SEMA_DUB; i++) {
    SemaSymbol *s = &p->converters[i - SEMA_SHRT];
    *s = (SemaSymbol){.name = p->types[i].name,
                      .arity = 1,
                      .builtin = SEMA_BUILTIN_CONVERT,
                      .return_type = &p->types[i]};
    s->params = arena_calloc(a, 1, sizeof(*s->params));
    s->params[0] = arena_calloc(a, 1, sizeof(**s->params));
    s->params[0]->type = &p->types[SEMA_UNKNOWN];
  }
  p->structs = arena_calloc(a, syntax->count, sizeof(SemaStruct));
  size_t capacity = syntax->count;
  Resolver r = {.arena = a, .program = p, .errors = errors};
  name_map_init(&r.local_names, a);
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind == DECL_IMPL)
      capacity += d->as.impl.count;
    if (d->kind != DECL_STRUCT)
      continue;
    if (structure(p, d->name) || lookup(p, d->name)) {
      diagnostics_error(errors, d->span, "Duplicate or reserved struct name.");
      continue;
    }
    SemaStruct *s = &p->structs[p->struct_count];
    name_map_put(&p->struct_names, NULL, d->name, s);
    s->id = p->struct_count++;
    s->syntax = d;
    s->type = (SemaType){.kind = SEMA_STRUCT, .name = d->name, .structure = s};
    s->count = d->as.structure.count;
    s->fields = arena_calloc(a, s->count, sizeof(SemaField));
  }
  p->aliases = arena_calloc(a, syntax->count, sizeof(*p->aliases));
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind != DECL_TYPE)
      continue;
    int reserved = structure(p, d->name) || lookup(p, d->name) ||
                   alias(p, d->name) || !strcmp(d->name, "Self");
    for (size_t j = SEMA_BOOL; j < SEMA_STRUCT; j++)
      reserved |= !strcmp(d->name, p->types[j].name);
    if (reserved) {
      diagnostics_error(errors, d->span, "Duplicate or reserved named type.");
      continue;
    }
    name_map_put(&p->alias_names, NULL, d->name, &p->aliases[p->alias_count]);
    p->aliases[p->alias_count++].syntax = d;
  }
  resolve_aliases(&r);
  // Named types exist before resolving fields, including forward references.
  for (size_t i = 0; i < p->struct_count; i++) {
    SemaStruct *s = &p->structs[i];
    r.owner = s;
    for (size_t j = 0; j < s->count; j++) {
      Parameter f = s->syntax->as.structure.fields[j];
      for (size_t k = 0; k < j; k++)
        if (!strcmp(s->fields[k].name, f.name))
          diagnostics_error(errors, f.span, "Duplicate struct field.");
      if (f.borrow == BORROW_MUT)
        diagnostics_error(errors, f.span,
                          "Struct fields cannot be borrowed parameters.");
      s->fields[j] = (SemaField){
          .name = f.name, .span = f.span, .id = j, .type = type(&r, f.type)};
    }
  }
  sema_update_equatable(p);
  p->functions = arena_calloc(a, capacity, sizeof(SemaFunction));
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind == DECL_FN)
      collect_function(&r, d, NULL);
    else if (d->kind == DECL_IMPL) {
      r.owner = NULL;
      const SemaType *target = type(&r, d->as.impl.target);
      if (target->kind != SEMA_STRUCT) {
        if (target->kind != SEMA_ERROR)
          diagnostics_error(errors, d->span,
                            "impl currently requires a struct type.");
        continue;
      }
      for (size_t j = 0; j < d->as.impl.count; j++)
        collect_function(&r, d->as.impl.methods[j], target->structure);
    } else if (d->kind != DECL_STRUCT && d->kind != DECL_TYPE)
      diagnostics_error(errors, d->span,
                        "This top-level declaration is not implemented yet.");
  }
  if (!p->entry)
    diagnostics_error(errors, syntax->span, "A program must define main().");
  for (size_t i = 0; i < p->count; i++) {
    r.function = &p->functions[i];
    r.owner = r.function->symbol.owner;
    restore(&r, NULL);
    r.depth = 0;
    for (size_t j = 0; j < r.function->symbol.arity; j++)
      push_local(&r, r.function->symbol.params[j]);
    r.function->body = resolve_expr(&r, r.function->syntax->as.function.body);
  }
  p->resolved = errors->count == 0;
  return p;
}
