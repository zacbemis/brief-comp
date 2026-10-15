#ifndef BRIEF_PARSER_H
#define BRIEF_PARSER_H

#include "ast/ast.h"
#include "common/diagnostic.h"
#include "common/intern.h"

// Input is a newline-filtered token array ending in EOF. Successful nodes
// survive syntax errors; diagnostics accumulate and parsing resumes at an
// enclosing statement/declaration boundary. Names use the supplied interner.
Program *parse_program(Arena *arena, InternTable *names, TokenArray tokens,
                       Diagnostics *diagnostics);

#endif
