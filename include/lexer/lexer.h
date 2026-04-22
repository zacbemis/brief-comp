#ifndef LEXER_H
#define LEXER_H

#include "lexer/token.h"

typedef struct {
  const char *source;
  const char *start;
  const char *current;
  int line;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next_token(Lexer *lexer);

#endif
