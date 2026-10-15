#ifndef LEXER_H
#define LEXER_H

#include "common/diagnostic.h"
#include "lexer/token.h"

typedef struct {
  const SourceFile *source;
  Diagnostics *diagnostics; // Optional; error tokens are still returned.
  const char *start;
  const char *current;
  const char *end;
  const char *block_comment_start;
} Lexer;

void lexer_init(Lexer *lexer, const SourceFile *source,
                Diagnostics *diagnostics);
Token lexer_next_token(Lexer *lexer);
// Arena-owned array for parser lookahead/rewinding. Keeps all raw newlines,
// including those inside block comments. Lexing continues after each error.
TokenArray lexer_tokenize(Arena *arena, const SourceFile *source,
                          Diagnostics *diagnostics);

#endif
