#include "sema/resolve/internal.h"

#include <string.h>

// A syntax-only body walk, independent of return inference and its probes.
// Patterns cannot contain a self expression in the accepted parser grammar.
static const Expr *first_self(const Expr *e) {
  if (!e)
    return NULL;
#define SELF(child)                                                            \
  do {                                                                         \
    const Expr *found = first_self(child);                                     \
    if (found)                                                                 \
      return found;                                                            \
  } while (0)
  switch (e->kind) {
  case EXPR_SELF:
    return e;
  case EXPR_SYMBOL:
    for (size_t i = 0; i < e->as.symbol.count; i++)
      SELF(e->as.symbol.payloads[i]);
    break;
  case EXPR_COLLECTION:
    SELF(e->as.collection.constructor);
    for (size_t i = 0; i < e->as.collection.count; i++)
      SELF(e->as.collection.items[i]);
    break;
  case EXPR_STRUCT_LIT:
    SELF(e->as.structure.type);
    for (size_t i = 0; i < e->as.structure.count; i++)
      SELF(e->as.structure.fields[i].value);
    break;
  case EXPR_UNARY:
    SELF(e->as.unary.value);
    break;
  case EXPR_BINARY:
    SELF(e->as.binary.left);
    SELF(e->as.binary.right);
    break;
  case EXPR_CALL:
    SELF(e->as.call.callee);
    for (size_t i = 0; i < e->as.call.count; i++)
      SELF(e->as.call.args[i]);
    break;
  case EXPR_FIELD:
    SELF(e->as.field.object);
    break;
  case EXPR_INDEX:
    SELF(e->as.index.object);
    SELF(e->as.index.index);
    break;
  case EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++) {
      BlockItem item = e->as.block.items[i];
      if (item.kind == BLOCK_ITEM_EXPR) {
        SELF(item.as.expr);
      } else if (item.kind == BLOCK_ITEM_DECL) {
        SELF(item.as.decl->as.binding.value);
        SELF(item.as.decl->as.binding.else_body);
      } else if (item.as.stmt->kind == STMT_ASSIGN) {
        SELF(item.as.stmt->as.assign.target);
        SELF(item.as.stmt->as.assign.value);
      } else if (item.as.stmt->kind != STMT_BREAK &&
                 item.as.stmt->kind != STMT_CONTINUE) {
        SELF(item.as.stmt->as.value);
      }
    }
    break;
  case EXPR_IF:
    SELF(e->as.conditional.condition);
    SELF(e->as.conditional.then_branch);
    SELF(e->as.conditional.else_branch);
    break;
  case EXPR_FOR:
    SELF(e->as.for_loop.iterable);
    SELF(e->as.for_loop.body);
    break;
  case EXPR_WHILE:
    SELF(e->as.while_loop.condition);
    SELF(e->as.while_loop.body);
    break;
  case EXPR_MATCH:
    for (size_t i = 0; i < e->as.match.count; i++)
      SELF(e->as.match.arms[i].value);
    break;
  case EXPR_TRY:
  case EXPR_PANIC:
    SELF(e->as.postfix);
    break;
  case EXPR_PIPE:
    SELF(e->as.pipe.value);
    SELF(e->as.pipe.stage);
    break;
  default:
    break;
  }
#undef SELF
  return NULL;
}
static int outside_self(Diagnostics *errors, const Decl *d) {
  const Expr *use = first_self(d->as.function.body);
  if (!use && d->as.function.receiver == RECEIVER_NONE)
    return 0;
  Span span = d->as.function.receiver != RECEIVER_NONE
                  ? d->as.function.receiver_span
                  : use->name_span;
  diagnostics_error(errors, span,
                    "'self' is only available in struct or trait functions.");
  return 1;
}
static void collect_function(Resolver *r, const Decl *d, SemaStruct *owner) {
  SemaProgram *p = r->program;
  if (d->kind != DECL_FN) {
    diagnostics_error(r->errors, d->span,
                      "Only function members can be resolved as functions.");
    return;
  }
  if (!d->as.function.body) {
    diagnostics_error(r->errors, d->span, "Struct functions require a body.");
    return;
  }
  if (!owner && alias(p, d->name)) {
    diagnostics_error(r->errors, d->span,
                      "A function and a named type cannot share a name.");
    return;
  }
  SemaSymbol *previous = owner ? sema_method(p, &owner->type, d->name)
                               : resolve_lookup(p, d->name);
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
  if (!owner && structure(p, d->name)) {
    diagnostics_error(r->errors, d->span,
                      "A function and a struct cannot share a name.");
    return;
  }
  SemaFunction *f = &p->functions[p->count];
  f->syntax = d;
  r->function = f;
  r->owner = owner;
  resolve_restore(r, NULL);
  r->depth = 0;
  f->written = d->as.function.return_type != NULL;
  const Expr *use = owner ? first_self(d->as.function.body) : NULL;
  ReceiverKind receiver = owner ? d->as.function.receiver : RECEIVER_NONE;
  if (use && receiver == RECEIVER_NONE)
    receiver = RECEIVER_SELF;
  size_t offset = receiver != RECEIVER_NONE;
  f->symbol = (SemaSymbol){.name = d->name,
                           .span = d->span,
                           .id = p->count++,
                           .arity = d->as.function.count + offset,
                           .visibility = d->visibility,
                           .owner = owner,
                           .receiver = receiver};
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
  f->written_type = resolve_return_type(r, d->as.function.return_type, f);
  f->symbol.return_type = f->symbol.outcome_type = f->written_type;
  f->symbol.params = arena_calloc(r->arena, f->symbol.arity, sizeof(SemaVar *));
  if (offset) {
    Span span = d->as.function.receiver != RECEIVER_NONE
                    ? d->as.function.receiver_span
                    : use->name_span;
    SemaVar *self = resolve_variable(r, "self", span, &owner->type, 0);
    self->data_mutable = receiver == RECEIVER_MUT_SELF;
    f->symbol.params[0] = self;
  }
  for (size_t j = 0; j < d->as.function.count; j++) {
    Parameter param = d->as.function.params[j];
    SemaVar *v = resolve_variable(r, param.name, param.span,
                                  resolve_type(r, param.type), 0);
    v->data_mutable = param.borrow == BORROW_MUT;
    if (v->data_mutable && v->type->kind != SEMA_STRUCT &&
        v->type->kind != SEMA_SYM && v->type->kind != SEMA_ERROR)
      diagnostics_error(
          r->errors, param.span,
          "Mutable borrowing currently requires a struct or symbol type.");
    f->symbol.params[j + offset] = v;
  }
  if (!owner && !strcmp(d->name, "main")) {
    p->entry = &f->symbol;
    if (f->symbol.arity)
      diagnostics_error(r->errors, d->span, "main() cannot have parameters.");
  }
}
static void declare_builtins(SemaProgram *p) {
  Arena *a = p->arena;
  const char *names[] = {"unknown", "never", "error", "()",     "bool", "chr",
                         "shrt",    "ushrt", "int",   "uint",   "lng",  "ulng",
                         "flt",     "dbl",   "str",   "struct", "Sym"};
  unsigned bits[] = {0, 0, 0, 0, 1, 8, 16, 16, 32, 32, 64, 64, 32, 64, 0, 0, 0};
  for (size_t i = 0; i < SEMA_TYPE_COUNT; i++)
    p->types[i] = (SemaType){.kind = (SemaTypeKind)i,
                             .name = names[i],
                             .bits = bits[i],
                             .is_signed = i == SEMA_SHRT || i == SEMA_INT ||
                                          i == SEMA_LNG};
  p->ok = sema_tag(p, NULL, "ok");
  p->print = (SemaSymbol){.name = "print",
                          .arity = 1,
                          .builtin = SEMA_BUILTIN_PRINT,
                          .return_type = &p->types[SEMA_UNIT]};
  p->stringify = (SemaSymbol){.name = "str",
                              .arity = 1,
                              .builtin = SEMA_BUILTIN_STR,
                              .return_type = &p->types[SEMA_STR]};
  p->print.outcome_type = p->print.return_type;
  p->stringify.outcome_type = p->stringify.return_type;
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
}

// Imports, traits and conformance headers have no available signatures.
// Report each unsupported declaration once, before dependent names or main.
// Then reject permanently invalid outside-owner uses before dependent
// type/body errors. Available struct functions classify self once during
// collection; imports, traits and conformance still take precedence.
static int unavailable_declarations(const Program *syntax,
                                    Diagnostics *errors) {
  int unavailable = 0;
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind == DECL_IMPORT) {
      diagnostics_error(errors, d->span, "Imports are not implemented yet.");
      unavailable = 1;
    } else if ((d->kind == DECL_STRUCT || d->kind == DECL_TRAIT) &&
               d->as.structure.conformance_count) {
      diagnostics_error(errors, d->as.structure.conformance_span,
                        "Trait conformance is not implemented yet.");
      unavailable = 1;
    } else if (d->kind == DECL_TRAIT) {
      diagnostics_error(
          errors, d->span,
          "A struct needs at least one field; traits are not implemented yet.");
      unavailable = 1;
    }
  }
  if (unavailable)
    return 1;
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind == DECL_FN)
      unavailable |= outside_self(errors, d);
  }
  return unavailable;
}

// Returns the function capacity: top-level declarations plus struct members.
static size_t collect_structs(SemaProgram *p, const Program *syntax,
                              Diagnostics *errors) {
  p->structs = arena_calloc(p->arena, syntax->count, sizeof(SemaStruct));
  size_t capacity = syntax->count;
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind != DECL_STRUCT)
      continue;
    capacity += d->as.structure.function_count;
    if (structure(p, d->name) || resolve_lookup(p, d->name)) {
      diagnostics_error(errors, d->span, "Duplicate or reserved struct name.");
      continue;
    }
    SemaStruct *s = &p->structs[p->struct_count];
    name_map_put(&p->struct_names, NULL, d->name, s);
    s->id = p->struct_count++;
    s->syntax = d;
    s->type = (SemaType){.kind = SEMA_STRUCT, .name = d->name, .structure = s};
    s->count = d->as.structure.count;
    s->fields = arena_calloc(p->arena, s->count, sizeof(SemaField));
  }
  return capacity;
}

static void collect_aliases(SemaProgram *p, const Program *syntax,
                            Diagnostics *errors) {
  p->aliases = arena_calloc(p->arena, syntax->count, sizeof(*p->aliases));
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind != DECL_TYPE)
      continue;
    int reserved = structure(p, d->name) || resolve_lookup(p, d->name) ||
                   alias(p, d->name);
    for (size_t j = SEMA_BOOL; j < SEMA_STRUCT; j++)
      reserved |= !strcmp(d->name, p->types[j].name);
    if (reserved) {
      diagnostics_error(errors, d->span, "Duplicate or reserved named type.");
      continue;
    }
    name_map_put(&p->alias_names, NULL, d->name, &p->aliases[p->alias_count]);
    p->aliases[p->alias_count++].syntax = d;
  }
}

// Named types exist before resolving fields, including forward references.
static void resolve_fields(Resolver *r) {
  SemaProgram *p = r->program;
  for (size_t i = 0; i < p->struct_count; i++) {
    SemaStruct *s = &p->structs[i];
    r->owner = s;
    for (size_t j = 0; j < s->count; j++) {
      Parameter f = s->syntax->as.structure.fields[j];
      for (size_t k = 0; k < j; k++)
        if (!strcmp(s->fields[k].name, f.name))
          diagnostics_error(r->errors, f.span, "Duplicate struct field.");
      if (f.borrow == BORROW_MUT)
        diagnostics_error(r->errors, f.span,
                          "Struct fields cannot be borrowed parameters.");
      s->fields[j] = (SemaField){.name = f.name,
                                 .span = f.span,
                                 .id = j,
                                 .type = resolve_type(r, f.type),
                                 .visibility = f.visibility};
    }
  }
}

static void collect_functions(Resolver *r, const Program *syntax,
                              size_t capacity) {
  SemaProgram *p = r->program;
  p->functions = arena_calloc(p->arena, capacity, sizeof(SemaFunction));
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind == DECL_FN)
      collect_function(r, d, NULL);
    else if (d->kind == DECL_STRUCT) {
      SemaStruct *owner = structure(p, d->name);
      if (owner && owner->syntax == d)
        for (size_t j = 0; j < d->as.structure.function_count; j++)
          collect_function(r, d->as.structure.functions[j], owner);
    } else if (d->kind != DECL_TYPE)
      diagnostics_error(r->errors, d->span,
                        "This top-level declaration is not implemented yet.");
  }
  if (!p->entry)
    diagnostics_error(r->errors, syntax->span, "A program must define main().");
}

static void resolve_bodies(Resolver *r) {
  SemaProgram *p = r->program;
  for (size_t i = 0; i < p->count; i++) {
    r->function = &p->functions[i];
    r->owner = r->function->symbol.owner;
    resolve_restore(r, NULL);
    r->depth = 0;
    for (size_t j = 0; j < r->function->symbol.arity; j++)
      resolve_push_local(r, r->function->symbol.params[j]);
    r->function->body = resolve_expr(r, r->function->syntax->as.function.body);
  }
}

SemaProgram *sema_resolve(Arena *a, const Program *syntax,
                          Diagnostics *errors) {
  SemaProgram *p = arena_calloc(a, 1, sizeof(*p));
  p->arena = a;
  p->module = syntax->span.file;
  name_map_init(&p->struct_names, a);
  name_map_init(&p->alias_names, a);
  name_map_init(&p->tag_names, a);
  name_map_init(&p->function_names, a);
  name_map_init(&p->method_names, a);
  declare_builtins(p);
  if (unavailable_declarations(syntax, errors))
    return p;
  Resolver r = {.arena = a, .program = p, .errors = errors};
  name_map_init(&r.local_names, a);
  size_t capacity = collect_structs(p, syntax, errors);
  collect_aliases(p, syntax, errors);
  resolve_aliases(&r);
  resolve_fields(&r);
  sema_update_equatable(p);
  collect_functions(&r, syntax, capacity);
  resolve_bodies(&r);
  p->resolved = errors->count == 0;
  return p;
}
