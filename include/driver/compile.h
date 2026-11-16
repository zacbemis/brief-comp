#ifndef BRIEF_DRIVER_COMPILE_H
#define BRIEF_DRIVER_COMPILE_H

#include "common/intern.h"
#include "ir/ir.h"
#include "lexer/token.h"

// One source file through the compiler stages. Each stage reads the previous
// stage's result and appends to the shared diagnostics; callers decide whether
// to continue after errors.
typedef struct {
  Arena *arena;
  SourceFile *source;
  Diagnostics diagnostics;
  InternTable names;
  TokenArray tokens;   // Raw lexer output.
  TokenArray filtered; // Newline-filtered tokens, the parser's input.
  Program *syntax;
  SemaProgram *program;
  IrProgram *ir;
} Compilation;

void compilation_init(Compilation *c, Arena *arena, SourceFile *source);
void compilation_lex(Compilation *c);
void compilation_parse(Compilation *c);
void compilation_resolve(Compilation *c);
// Type checking and return inference on the resolved program.
void compilation_check(Compilation *c);
// Lowers a checked program and inserts ownership. Returns 0 when the program
// cannot be lowered.
int compilation_lower(Compilation *c);

#endif
