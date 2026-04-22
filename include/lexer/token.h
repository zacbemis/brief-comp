#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

typedef enum {
  TOK_EOF,
  TOK_ERR,    // error
  TOK_NEW_LN, // New Line

  TOK_LET,     // let
  TOK_VAR,     // var
  TOK_SEMI,    // ;
  TOK_SHR,     // share
  TOK_IDENT,   // identifier
  TOK_INT_LIT, // literal

  TOK_EQUAL,  // =
  TOK_PLUS,   // +
  TOK_MINUS,  // minus
  TOK_STAR,   // *
  TOK_SLASH,  // /
  TOK_LPAREN, // (
  TOK_RPAREN, // )
  TOK_LBRACE, // {
  TOK_RBRACE, // }
  TOK_COMMA,  // ,
  TOK_PERIOD  // .
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  size_t length;
  int line;
} Token;

const char *token_type_name(TokenType type);

#endif
