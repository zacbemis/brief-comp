#include "sema/sema.h"

const SemaType *sema_conversion(SemaProgram *p, const SemaType *source,
                                const SemaType *target) {
  if (!sema_number(target))
    return &p->types[SEMA_ERROR];
  if (source->kind == SEMA_ERROR || source->kind == SEMA_NEVER)
    return source;
  // An explicit conversion target is an independent success-type anchor.
  // Subsequent SCC passes add failures once the source domain becomes known.
  if (source->kind == SEMA_UNKNOWN)
    return sema_success(p, target);
  if (!sema_number(source) && source->kind != SEMA_STR)
    return &p->types[SEMA_ERROR];
  int infallible = source == target;
  if (sema_integer(source) && sema_integer(target))
    infallible = source->is_signed == target->is_signed
                     ? source->bits <= target->bits
                     : !source->is_signed && target->is_signed &&
                           source->bits < target->bits;
  else if (sema_integer(source) && !sema_integer(target))
    infallible = 1; // All language integers fit either floating range; round.
  else if (source->kind == SEMA_FLT && target->kind == SEMA_DUB)
    infallible = 1;
  if (infallible)
    return sema_success(p, target);
  SemaVariant variants[] = {
      {p->ok, target},
      {sema_tag(p, source->kind == SEMA_STR ? "invalid" : "overflow"),
       source->kind == SEMA_STR ? source : NULL}};
  return sema_symbol_set(p, variants, 2);
}
