#include "lexer/token.h"

const char *token_type_name(TokenType type) {
  switch (type) {
  case TOK_EOF:
    return "TOK_EOF";
  case TOK_ERR:
    return "TOK_ERR";
  case TOK_NEW_LN:
    return "TOK_NEW_LN";
  case TOK_LET:
    return "TOK_LET";
  case TOK_VAR:
    return "TOK_VAR";
  case TOK_SHR:
    return "TOK_SHR";
  case TOK_SEMI:
    return "TOK_SEMI";
  case TOK_IDENT:
    return "TOK_IDENT";
  case TOK_INT_LIT:
    return "TOK_INT_LIT";
  case TOK_EQUAL:
    return "TOK_EQUAL";
  case TOK_PLUS:
    return "TOK_PLUS";
  case TOK_LPAREN:
    return "TOK_LPAREN";
  case TOK_RPAREN:
    return "TOK_RPAREN";
  case TOK_LBRACE:
    return "TOK_LBRACE";
  case TOK_RBRACE:
    return "TOK_RBRACE";
  case TOK_COMMA:
    return "TOK_COMMA";
  default:
    return "TOK_UNKNOWN";
  }
}
