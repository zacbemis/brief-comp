#ifndef BRIEF_NEWLINE_H
#define BRIEF_NEWLINE_H

#include "lexer/token.h"

// Produces an arena-owned array without changing the raw lexer tokens. EOF
// and spans are preserved. Delimiter errors are left for the parser.
TokenArray newline_filter(Arena *arena, TokenArray raw);

#endif
