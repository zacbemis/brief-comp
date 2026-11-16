#include "lexer/newline.h"

static int continues(TokenType t) {
  switch (t) {
  case TOK_GREATER_THAN:
  case TOK_RSHIFT:
  case TOK_COMMA:
  case TOK_COLON:
  case TOK_PERIOD:
  case TOK_FAT_ARROW:
  case TOK_ARROW:
  case TOK_PIPE:
  case TOK_EQUAL:
  case TOK_PLUS:
  case TOK_MINUS:
  case TOK_STAR:
  case TOK_SLASH:
  case TOK_PERCENT:
  case TOK_STAR_STAR:
  case TOK_PLUS_EQUAL:
  case TOK_MINUS_EQUAL:
  case TOK_STAR_EQUAL:
  case TOK_SLASH_EQUAL:
  case TOK_PERCENT_EQUAL:
  case TOK_EQUAL_EQUAL:
  case TOK_BANG_EQUAL:
  case TOK_LESS_THAN:
  case TOK_LESS_THAN_EQUAL:
  case TOK_GREATER_THAN_EQUAL:
  case TOK_AND:
  case TOK_OR:
  case TOK_NOT:
  case TOK_TILDE:
  case TOK_BIT_AND:
  case TOK_BIT_OR:
  case TOK_BIT_XOR:
  case TOK_LSHIFT:
  case TOK_DOT_DOT:
  case TOK_DOT_DOT_EQUAL:
  case TOK_LET:
  case TOK_VAR:
  case TOK_CONST:
  case TOK_PUB:
  case TOK_IMPORT:
  case TOK_IF:
  case TOK_ELSE:
  case TOK_FOR:
  case TOK_IN:
  case TOK_WHILE:
    return 1;
  default:
    return 0;
  }
}

TokenArray newline_filter(Arena *arena, TokenArray raw) {
  TokenArray out = {.items = arena_calloc(arena, raw.count, sizeof(Token))};
  TokenType *brackets = arena_calloc(arena, raw.count, sizeof(*brackets));
  size_t depth = 0;
  for (size_t i = 0; i < raw.count;) {
    Token token = raw.items[i];
    TokenType t = token.type;
    if (t == TOK_NEWLINE) {
      size_t end = i + 1;
      while (end < raw.count && raw.items[end].type == TOK_NEWLINE)
        end++;
      TokenType previous = out.count ? out.items[out.count - 1].type : TOK_EOF;
      TokenType next = end < raw.count ? raw.items[end].type : TOK_EOF;
      int enclosed = depth && brackets[depth - 1] != TOK_LBRACE;
      // The bang of a :! error-symbol prefix still needs its tag.
      int symbol_bang = previous == TOK_BANG && out.count >= 2 &&
                        out.items[out.count - 2].type == TOK_COLON;
      if (!enclosed && !symbol_bang && !continues(previous) &&
          next != TOK_PIPE && next != TOK_PERIOD && next != TOK_DOT_LPAREN) {
        for (; i < end; i++)
          out.items[out.count++] = raw.items[i];
      }
      i = end;
      continue;
    }
    if (t == TOK_LPAREN || t == TOK_DOT_LPAREN || t == TOK_LBRACKET ||
        t == TOK_LBRACE)
      brackets[depth++] = t == TOK_DOT_LPAREN ? TOK_LPAREN : t;
    else if (t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE) {
      TokenType wanted = t == TOK_RPAREN     ? TOK_LPAREN
                         : t == TOK_RBRACKET ? TOK_LBRACKET
                                             : TOK_LBRACE;
      size_t match = depth;
      while (match && brackets[match - 1] != wanted)
        match--;
      if (match)
        depth = match - 1;
    }
    out.items[out.count++] = token;
    i++;
  }
  return out;
}
