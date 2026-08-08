#include "lexer/lexer.h"
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

static int match(Lexer *lexer, char expected) {
  if (is_at_end(lexer) || *lexer->current != expected)
    return 0;
  lexer->current++;
  return 1;
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
// skipped here because they are emitted as TOK_NEWLINE tokens.
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
  if (len == 2 && memcmp(lexer->start, "if", 2) == 0)
    return TOK_IF;
  if (len == 3 && memcmp(lexer->start, "chr", 3) == 0)
    return TOK_CHR;
  if (len == 3 && memcmp(lexer->start, "dub", 3) == 0)
    return TOK_DUB;
  if (len == 3 && memcmp(lexer->start, "flt", 3) == 0)
    return TOK_FLT;
  if (len == 3 && memcmp(lexer->start, "for", 3) == 0)
    return TOK_FOR;
  if (len == 3 && memcmp(lexer->start, "int", 3) == 0)
    return TOK_INT;
  if (len == 3 && memcmp(lexer->start, "let", 3) == 0)
    return TOK_LET;
  if (len == 3 && memcmp(lexer->start, "lng", 3) == 0)
    return TOK_LNG;
  if (len == 3 && memcmp(lexer->start, "mut", 3) == 0)
    return TOK_MUT;
  if (len == 3 && memcmp(lexer->start, "pub", 3) == 0)
    return TOK_PUB;
  if (len == 3 && memcmp(lexer->start, "shr", 3) == 0)
    return TOK_SHR;
  if (len == 3 && memcmp(lexer->start, "str", 3) == 0)
    return TOK_STR;
  if (len == 3 && memcmp(lexer->start, "var", 3) == 0)
    return TOK_VAR;
  if (len == 4 && memcmp(lexer->start, "bool", 4) == 0)
    return TOK_BOOL;
  if (len == 4 && memcmp(lexer->start, "else", 4) == 0)
    return TOK_ELSE;
  if (len == 4 && memcmp(lexer->start, "enum", 4) == 0)
    return TOK_ENUM;
  if (len == 4 && memcmp(lexer->start, "priv", 4) == 0)
    return TOK_PRIV;
  if (len == 4 && memcmp(lexer->start, "shrt", 4) == 0)
    return TOK_SHRT;
  if (len == 4 && memcmp(lexer->start, "true", 4) == 0)
    return TOK_BOOL_LIT;
  if (len == 4 && memcmp(lexer->start, "uint", 4) == 0)
    return TOK_UINT;
  if (len == 4 && memcmp(lexer->start, "ulng", 4) == 0)
    return TOK_ULNG;
  if (len == 4 && memcmp(lexer->start, "unit", 4) == 0)
    return TOK_UNIT;
  if (len == 5 && memcmp(lexer->start, "false", 5) == 0)
    return TOK_BOOL_LIT;
  if (len == 5 && memcmp(lexer->start, "match", 5) == 0)
    return TOK_MATCH;
  if (len == 5 && memcmp(lexer->start, "while", 5) == 0)
    return TOK_WHILE;
  if (len == 5 && memcmp(lexer->start, "ushrt", 5) == 0)
    return TOK_USHRT;
  if (len == 6 && memcmp(lexer->start, "module", 6) == 0)
    return TOK_MODULE;
  if (len == 6 && memcmp(lexer->start, "struct", 6) == 0)
    return TOK_STRUCT;
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

  if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
    advance(lexer);
    while (is_digit(peek(lexer)))
      advance(lexer);
    return make_token(lexer, TOK_FLOAT_LIT);
  }

  return make_token(lexer, TOK_INT_LIT);
}

static Token string(Lexer *lexer) {
  while (!is_at_end(lexer) && peek(lexer) != '"') {
    if (peek(lexer) == '\n')
      lexer->line++;
    if (peek(lexer) == '\\' && peek_next(lexer) != '\0')
      advance(lexer);
    advance(lexer);
  }

  if (is_at_end(lexer))
    return err_token(lexer, "Unterminated string.");

  advance(lexer);
  return make_token(lexer, TOK_STR_LIT);
}

static Token character(Lexer *lexer) {
  while (!is_at_end(lexer) && peek(lexer) != '\'' && peek(lexer) != '\n') {
    if (peek(lexer) == '\\' && peek_next(lexer) != '\0')
      advance(lexer);
    advance(lexer);
  }

  if (is_at_end(lexer) || peek(lexer) == '\n')
    return err_token(lexer, "Unterminated character.");

  advance(lexer);
  return make_token(lexer, TOK_CHAR_LIT);
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
    Token t = make_token(lexer, TOK_NEWLINE);
    lexer->line++;
    return t;
  }
  case '"':
    return string(lexer);
  case '\'':
    return character(lexer);
  case ';':
    if (match(lexer, ';'))
      return make_token(lexer, TOK_SEMI_SEMI);
    return make_token(lexer, TOK_SEMI);
  case ':':
    return make_token(lexer, TOK_COLON);
  case ',':
    return make_token(lexer, TOK_COMMA);
  case '.':
    if (match(lexer, '.'))
      return make_token(lexer, TOK_DOT_DOT);
    return make_token(lexer, TOK_PERIOD);
  case '=':
    if (match(lexer, '='))
      return make_token(lexer, TOK_EQUAL_EQUAL);
    if (match(lexer, '>'))
      return make_token(lexer, TOK_FAT_ARROW);
    return make_token(lexer, TOK_EQUAL);
  case '+':
    if (match(lexer, '+'))
      return make_token(lexer, TOK_PLUS_PLUS);
    if (match(lexer, '='))
      return make_token(lexer, TOK_PLUS_EQUAL);
    return make_token(lexer, TOK_PLUS);
  case '-':
    if (match(lexer, '-'))
      return make_token(lexer, TOK_MINUS_MINUS);
    if (match(lexer, '='))
      return make_token(lexer, TOK_MINUS_EQUAL);
    if (match(lexer, '>'))
      return make_token(lexer, TOK_ARROW);
    return make_token(lexer, TOK_MINUS);
  case '*':
    if (match(lexer, '*'))
      return make_token(lexer, TOK_STAR_STAR);
    if (match(lexer, '='))
      return make_token(lexer, TOK_STAR_EQUAL);
    return make_token(lexer, TOK_STAR);
  case '/':
    if (match(lexer, '='))
      return make_token(lexer, TOK_SLASH_EQUAL);
    return make_token(lexer, TOK_SLASH);
  case '%':
    if (match(lexer, '='))
      return make_token(lexer, TOK_PERCENT_EQUAL);
    return make_token(lexer, TOK_PERCENT);
  case '!':
    if (match(lexer, '='))
      return make_token(lexer, TOK_BANG_EQUAL);
    return make_token(lexer, TOK_BANG);
  case '<':
    if (match(lexer, '<'))
      return make_token(lexer, TOK_LSHIFT);
    if (match(lexer, '='))
      return make_token(lexer, TOK_LESS_THAN_EQUAL);
    return make_token(lexer, TOK_LESS_THAN);
  case '>':
    if (match(lexer, '>'))
      return make_token(lexer, TOK_RSHIFT);
    if (match(lexer, '='))
      return make_token(lexer, TOK_GREATER_THAN_EQUAL);
    return make_token(lexer, TOK_GREATER_THAN);
  case '&':
    if (match(lexer, '&'))
      return make_token(lexer, TOK_AND_AND);
    return make_token(lexer, TOK_AND);
  case '|':
    if (match(lexer, '|'))
      return make_token(lexer, TOK_OR_OR);
    return make_token(lexer, TOK_OR);
  case '~':
    return make_token(lexer, TOK_TILDE);
  case '^':
    return make_token(lexer, TOK_XOR);
  case '(':
    return make_token(lexer, TOK_LPAREN);
  case ')':
    return make_token(lexer, TOK_RPAREN);
  case '{':
    return make_token(lexer, TOK_LBRACE);
  case '}':
    return make_token(lexer, TOK_RBRACE);
  case '[':
    return make_token(lexer, TOK_LBRACKET);
  case ']':
    return make_token(lexer, TOK_RBRACKET);
  }

  return err_token(lexer, "Unexpected character.");
}
