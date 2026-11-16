#include "ir/lower/internal.h"

#include <stdio.h>
#include <string.h>

static void use_type(const SemaType *t, unsigned char *used,
                     const SemaSymSet **pending, size_t *count) {
  if (t && t->kind == SEMA_SYM && !used[t->symbols->id]) {
    used[t->symbols->id] = 1;
    pending[(*count)++] = t->symbols;
  }
}
// Growing error sets during inference intern every intermediate union. Emit
// only the sets lowered code can name, plus their nested payload sets, so the
// generated C stays linear in the sets a program actually uses.
static void used_symbol_sets(Arena *arena, const SemaProgram *program,
                             IrProgram *p) {
  size_t total = program->symbol_set_count ? program->symbol_set_count : 1;
  unsigned char *used = arena_calloc(arena, total, 1);
  const SemaSymSet **pending = arena_calloc(arena, total, sizeof(*pending));
  size_t count = 0;
  use_type(p->entry_outcome, used, pending, &count);
  for (size_t i = 0; i < p->struct_count; i++)
    for (size_t j = 0; j < p->structs[i].count; j++)
      use_type(p->structs[i].fields[j].type, used, pending, &count);
  for (size_t i = 0; i < p->count; i++) {
    const IrFunction *f = &p->functions[i];
    use_type(f->return_type, used, pending, &count);
    for (size_t j = 0; j < f->variable_count; j++)
      use_type(f->variables[j], used, pending, &count);
    for (size_t j = 0; j < f->temporaries; j++)
      use_type(f->temporary_types[j], used, pending, &count);
    for (size_t j = 0; j < f->count; j++)
      use_type(f->instructions[j].type, used, pending, &count);
  }
  while (count) {
    const SemaSymSet *s = pending[--count];
    for (size_t i = 0; i < s->count; i++)
      for (size_t j = 0; j < s->variants[i].payload_count; j++)
        use_type(s->variants[i].payloads[j], used, pending, &count);
  }
  // Each set is pushed at most once, so the marked sets fit in `pending`.
  p->symbol_sets = pending;
  for (const SemaSymSet *s = program->symbol_sets; s; s = s->next)
    if (used[s->id])
      p->symbol_sets[p->symbol_set_count++] = s;
}
IrProgram *ir_lower(Arena *arena, const SemaProgram *program) {
  if (!program->checked)
    return NULL;
  IrProgram *p = arena_calloc(arena, 1, sizeof(*p));
  p->count = program->count;
  p->entry = program->entry->id;
  p->structs = program->structs;
  p->struct_count = program->struct_count;
  p->ok = program->ok;
  p->entry_outcome = program->entry->outcome_type;
  p->functions = arena_calloc(arena, p->count, sizeof(IrFunction));
  for (size_t i = 0; i < p->count; i++) {
    const SemaFunction *source = &program->functions[i];
    IrFunction *f = &p->functions[i];
    f->name = source->symbol.name;
    if (source->symbol.owner) {
      const char *owner = source->symbol.owner->type.name;
      size_t length = strlen(owner) + strlen(f->name) + 64;
      char *name = arena_alloc(arena, length);
      snprintf(name, length, "%zu_%s_%s", strlen(owner), owner, f->name);
      f->method = 1;
      // Top-level names receive a different namespace in emission.
      f->name = name;
    }
    f->span = source->symbol.span;
    f->return_type = source->symbol.return_type;
    f->variable_count = source->variable_count;
    f->parameter_count = source->symbol.arity;
    f->variables =
        arena_calloc(arena, f->variable_count, sizeof(*f->variables));
    for (size_t j = 0; j < f->variable_count; j++)
      f->variables[j] = source->variables[j]->type;
    Lower l = {.arena = arena, .program = program, .function = f};
    size_t value = lower_expr(&l, source->body);
    if (!l.returned) {
      size_t result = f->return_type->kind == SEMA_UNIT ? IR_NO_VALUE : value;
      result = lower_convert(&l, result, f->return_type, f->span);
      lower_cleanup(&l, NULL, result, f->span);
      lower_emit(
          &l, (IrInstruction){.op = IR_RETURN, .span = f->span, .a = result});
    }
  }
  used_symbol_sets(arena, program, p);
  return p;
}
