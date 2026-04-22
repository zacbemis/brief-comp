#include "lexer/lexer.h"
#include <ctype.h>
#include <string.h>

void lexer_init(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
}

static int is_at_end(Lexer *lexer) { return *lexer->current == '\0'; }

static char peek(Lexer *lexer) { return *lexer->current; }

static char peek_next(Lexer *lexer) {
  if (is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

static char advance(Lexer *lexer) {
  lexer->current++;
  return lexer->current[-1];
}

static Token make_token(Lexer *lexer, TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer->start;
  token.length = (size_t)(lexer->current - lexer->start);
  token.line = lexer->line;
  return token;
}

static Token err_token(Lexer *lexer, const char *message) {
  Token token;
  token.type = TOK_ERR;
  token.start = message;
  token.length = strlen(message);
  token.line = lexer->line;
  return token;
}

// Skip spaces, tabs, carriage returns, and comments. Newlines are NOT
// skipped here because they are emitted as TOK_NEW_LN tokens.
static void skip_whitespace_and_comments(Lexer *lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\t':
    case '\r':
      advance(lexer);
      break;
    case '/':
      if (peek_next(lexer) == '/') {
        while (!is_at_end(lexer) && peek(lexer) != '\n')
          advance(lexer);
      } else if (peek_next(lexer) == '*') {
        advance(lexer); // consume '/'
        advance(lexer); // consume '*'
        while (!is_at_end(lexer) &&
               !(peek(lexer) == '*' && peek_next(lexer) == '/')) {
          if (peek(lexer) == '\n')
            lexer->line++;
          advance(lexer);
        }
        if (!is_at_end(lexer)) {
          advance(lexer); // consume '*'
          advance(lexer); // consume '/'
        }
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

static int is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static TokenType identifier_type(Lexer *lexer) {
  size_t len = (size_t)(lexer->current - lexer->start);
  if (len == 3 && memcmp(lexer->start, "let", 3) == 0)
    return TOK_LET;
  if (len == 3 && memcmp(lexer->start, "var", 3) == 0)
    return TOK_VAR;
  if (len == 3 && memcmp(lexer->start, "shr", 3) == 0)
    return TOK_SHR;
  return TOK_IDENT;
}

static Token identifier(Lexer *lexer) {
  while (is_alpha(peek(lexer)) || is_digit(peek(lexer)))
    advance(lexer);
  return make_token(lexer, identifier_type(lexer));
}

static Token number(Lexer *lexer) {
  while (is_digit(peek(lexer)))
    advance(lexer);
  return make_token(lexer, TOK_INT_LIT);
}

Token lexer_next_token(Lexer *lexer) {
  skip_whitespace_and_comments(lexer);
  lexer->start = lexer->current;

  if (is_at_end(lexer))
    return make_token(lexer, TOK_EOF);

  char c = advance(lexer);

  if (is_alpha(c))
    return identifier(lexer);
  if (is_digit(c))
    return number(lexer);

  switch (c) {
  case '\n': {
    Token t = make_token(lexer, TOK_NEW_LN);
    lexer->line++;
    return t;
  }
  case ';':
    return make_token(lexer, TOK_SEMI);
  case '=':
    return make_token(lexer, TOK_EQUAL);
  case '+':
    return make_token(lexer, TOK_PLUS);
  case '(':
    return make_token(lexer, TOK_LPAREN);
  case ')':
    return make_token(lexer, TOK_RPAREN);
  case '{':
    return make_token(lexer, TOK_LBRACE);
  case '}':
    return make_token(lexer, TOK_RBRACE);
  case ',':
    return make_token(lexer, TOK_COMMA);
  }

  return err_token(lexer, "Unexpected character.");
}
