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

typedef struct {
  Span span;
  const char *message;
  DiagnosticNote *notes;
  DiagnosticNote *last_note;
} Diagnostic;

typedef struct {
  Arena *arena;
  Diagnostic **items;
  size_t count;
  size_t capacity;
} Diagnostics;

void diagnostics_init(Diagnostics *diagnostics, Arena *arena);
// Messages are copied into the arena. Diagnostics and notes remain stable
// even when the list grows. All entries currently represent errors.
Diagnostic *diagnostics_error(Diagnostics *diagnostics, Span span,
                              const char *message);
void diagnostic_note(Diagnostics *diagnostics, Diagnostic *error, Span span,
                     const char *message);
void diagnostics_print(const Diagnostics *diagnostics, FILE *output);

#endif
