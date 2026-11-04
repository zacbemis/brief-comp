#ifndef BRIEF_DIAGNOSTIC_H
#define BRIEF_DIAGNOSTIC_H

#include "common/source.h"
#include <stdio.h>

typedef struct DiagnosticNote DiagnosticNote;
struct DiagnosticNote {
  Span span;
  const char *message;
  DiagnosticNote *next;
};

typedef struct DiagnosticFix DiagnosticFix;
struct DiagnosticFix {
  Span span;
  const char *replacement;
  DiagnosticFix *next;
};

typedef struct {
  Span span;
  const char *message;
  DiagnosticNote *notes;
  DiagnosticNote *last_note;
  DiagnosticFix *fixes;
  DiagnosticFix *last_fix;
} Diagnostic;

typedef struct {
  Arena *arena;
  Diagnostic **items;
  size_t count;
  size_t capacity;
} Diagnostics;

void diagnostics_init(Diagnostics *diagnostics, Arena *arena);
// Messages and replacements are copied into the arena. Diagnostics, notes and
// fixes remain stable even when the list grows. All entries represent errors.
Diagnostic *diagnostics_error(Diagnostics *diagnostics, Span span,
                              const char *message);
void diagnostic_note(Diagnostics *diagnostics, Diagnostic *error, Span span,
                     const char *message);
// Attach only certainly correct edits, not guesses or alternative suggestions.
// All fixes on a diagnostic are intended to be applied together to the original
// source. An empty span inserts; an empty replacement deletes. Returns 0 for a
// missing source or invalid offsets (fix spans are never silently clamped).
int diagnostic_fix(Diagnostics *diagnostics, Diagnostic *error, Span span,
                   const char *replacement);
// Moves the newest diagnostic to `index`, keeping the others in order. A check
// made after a construct is complete can then still report in source order.
void diagnostics_move_last(Diagnostics *diagnostics, size_t index);
void diagnostics_print(const Diagnostics *diagnostics, FILE *output);

#endif
