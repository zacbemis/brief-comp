#include "sema/sema.h"

#include <string.h>

int sema_integer(const SemaType *t) {
  return t && t->kind >= SEMA_SHRT && t->kind <= SEMA_ULNG;
}
int sema_number(const SemaType *t) {
  return sema_integer(t) || (t && (t->kind == SEMA_FLT || t->kind == SEMA_DBL));
}
int sema_reference(const SemaType *t) {
  return t &&
         (t->kind == SEMA_STR || t->kind == SEMA_STRUCT || t->kind == SEMA_SYM);
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
