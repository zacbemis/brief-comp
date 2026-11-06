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
  if (!sema_number(source) && source->kind != SEMA_STR &&
      source->kind != SEMA_CHR)
    return &p->types[SEMA_ERROR];
  int infallible = source == target;
  if (source->kind == SEMA_CHR)
    infallible = 1; // An unsigned byte, 0-255, fits every numeric target.
  else if (sema_integer(source) && sema_integer(target))
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
  SemaTag *failure =
      sema_tag(p, p->module, source->kind == SEMA_STR ? "invalid" : "overflow");
  if (!failure || (failure->kind_declared && failure->kind != SEMA_TAG_ERROR))
    return &p->types[SEMA_ERROR];
  // Intrinsic errors are known only for fallible domains. Do not reserve
  // these names eagerly or overwrite a user's explicit neutral declaration.
  // An implicit kind has no fictitious source spelling/span.
  failure->kind = SEMA_TAG_ERROR;
  failure->kind_declared = 1;
  SemaVariant variants[] = {{p->ok, &target, 1},
                            {failure, source->kind == SEMA_STR ? &source : NULL,
                             source->kind == SEMA_STR ? 1 : 0}};
  return sema_symbol_set(p, variants, 2);
}
