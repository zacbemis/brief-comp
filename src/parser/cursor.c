#include "parser/internal.h"

#include <string.h>

void parser_push(Parser *p, List *list, const void *item, size_t size) {
  list->data =
      arena_grow(p->arena, list->data, list->count, &list->capacity, size, 4);
  memcpy((char *)list->data + list->count++ * size, item, size);
}

void parser_recover(Parser *p, size_t start, int enclosed) {
  size_t braces = 0, parens = 0, brackets = 0;
  for (size_t i = start; i < p->tokens.count; i++) {
    TokenType t = p->tokens.items[i].type;
    if (i >= p->position) {
      if (t == TOK_EOF)
        return;
      if (t == TOK_RBRACE && !braces) {
        if (!enclosed)
          take(p);
        return;
      }
      if (t == TOK_NEWLINE && !braces && !parens && !brackets) {
        take(p);
        return;
      }
      take(p);
    }
    switch (t) {
    case TOK_LBRACE:
      braces++;
      break;
    case TOK_RBRACE:
      if (braces)
        braces--;
      break;
    case TOK_LPAREN:
    case TOK_DOT_LPAREN:
      parens++;
      break;
    case TOK_RPAREN:
      if (parens)
        parens--;
      break;
    case TOK_LBRACKET:
      brackets++;
      break;
    case TOK_RBRACKET:
      if (brackets)
        brackets--;
      break;
    default:
      break;
    }
  }
}

int parser_separator(Parser *p, TokenType end) {
  if (at(p, end) || at(p, TOK_EOF))
    return 1;
  if (match(p, TOK_NEWLINE)) {
    newlines(p);
    return 1;
  }
  error(p, "Expected a newline between items.");
  return 0;
}
