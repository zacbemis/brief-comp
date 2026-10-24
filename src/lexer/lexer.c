#include "lexer/lexer.h"

#include <stdint.h>
#include <string.h>

void lexer_init(Lexer *lexer, const SourceFile *source,
                Diagnostics *diagnostics) {
  *lexer = (Lexer){.source = source,
                   .diagnostics = diagnostics,
                   .start = source->text,
                   .current = source->text,
                   .end = source->text + source->length};
}

static int is_at_end(Lexer *lexer) { return lexer->current == lexer->end; }

static char peek(Lexer *lexer) {
  return is_at_end(lexer) ? '\0' : *lexer->current;
}

static char peek_next(Lexer *lexer) {
  if (lexer->end - lexer->current < 2)
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
  size_t start = (size_t)(lexer->start - lexer->source->text);
  size_t end = (size_t)(lexer->current - lexer->source->text);
  return (Token){.type = type,
                 .span = span_make(lexer->source, start, end),
                 .start = lexer->start,
                 .length = end - start,
                 .line = source_position(lexer->source, start).line};
}

// The token still covers the full source lexeme for recovery; the diagnostic
// can highlight just the offending part of it.
static Token err_token_at(Lexer *lexer, const char *message, Span highlight) {
  Token token = make_token(lexer, TOK_ERR);
  token.message = message;
  if (lexer->diagnostics)
    diagnostics_error(lexer->diagnostics, highlight, message);
  return token;
}

static Token err_token(Lexer *lexer, const char *message) {
  Span span = span_make(lexer->source,
                        (size_t)(lexer->start - lexer->source->text),
                        (size_t)(lexer->current - lexer->source->text));
  return err_token_at(lexer, message, span);
}

// Emit newlines even inside block comments: otherwise a comment spanning
// lines could accidentally join two statements before the newline filter.
// Returns true when it produces a newline or an unterminated-comment error.
static int skip_whitespace_and_comments(Lexer *lexer, Token *token) {
  for (;;) {
    if (lexer->block_comment_start) {
      while (!is_at_end(lexer)) {
        if (peek(lexer) == '*' && peek_next(lexer) == '/') {
          advance(lexer);
          advance(lexer);
          lexer->block_comment_start = NULL;
          break;
        }
        if (peek(lexer) == '\n') {
          lexer->start = lexer->current;
          advance(lexer);
          *token = make_token(lexer, TOK_NEWLINE);
          return 1;
        }
        advance(lexer);
      }
      if (lexer->block_comment_start) {
        lexer->start = lexer->block_comment_start;
        lexer->block_comment_start = NULL;
        *token = err_token(lexer, "Unterminated block comment.");
        return 1;
      }
      continue;
    }
    if (is_at_end(lexer))
      return 0;
    switch (peek(lexer)) {
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
        lexer->block_comment_start = lexer->current;
        advance(lexer);
        advance(lexer);
      } else {
        return 0;
      }
      break;
    default:
      return 0;
    }
  }
}

static int is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static TokenType identifier_type(Lexer *lexer) {
  size_t len = (size_t)(lexer->current - lexer->start);
  if (len == 2 && memcmp(lexer->start, "Fn", 2) == 0)
    return TOK_FN;
  if (len == 2 && memcmp(lexer->start, "if", 2) == 0)
    return TOK_IF;
  if (len == 2 && memcmp(lexer->start, "in", 2) == 0)
    return TOK_IN;
  if (len == 2 && memcmp(lexer->start, "or", 2) == 0)
    return TOK_OR;
  if (len == 3 && memcmp(lexer->start, "Sym", 3) == 0)
    return TOK_SYM;
  if (len == 3 && memcmp(lexer->start, "and", 3) == 0)
    return TOK_AND;
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
  if (len == 3 && memcmp(lexer->start, "mod", 3) == 0)
    return TOK_MOD;
  if (len == 3 && memcmp(lexer->start, "ovr", 3) == 0)
    return TOK_OVR;
  if (len == 3 && memcmp(lexer->start, "pub", 3) == 0)
    return TOK_PUB;
  if (len == 3 && memcmp(lexer->start, "ret", 3) == 0)
    return TOK_RET;
  if (len == 3 && memcmp(lexer->start, "str", 3) == 0)
    return TOK_STR;
  if (len == 3 && memcmp(lexer->start, "var", 3) == 0)
    return TOK_VAR;
  if (len == 4 && memcmp(lexer->start, "Self", 4) == 0)
    return TOK_SELF;
  if (len == 4 && memcmp(lexer->start, "bool", 4) == 0)
    return TOK_BOOL;
  if (len == 4 && memcmp(lexer->start, "else", 4) == 0)
    return TOK_ELSE;
  if (len == 4 && memcmp(lexer->start, "impl", 4) == 0)
    return TOK_IMPL;
  if (len == 4 && memcmp(lexer->start, "shrt", 4) == 0)
    return TOK_SHRT;
  if (len == 4 && memcmp(lexer->start, "true", 4) == 0)
    return TOK_BOOL_LIT;
  if (len == 4 && memcmp(lexer->start, "type", 4) == 0)
    return TOK_TYPE;
  if (len == 4 && memcmp(lexer->start, "uint", 4) == 0)
    return TOK_UINT;
  if (len == 4 && memcmp(lexer->start, "ulng", 4) == 0)
    return TOK_ULNG;
  if (len == 5 && memcmp(lexer->start, "break", 5) == 0)
    return TOK_BREAK;
  if (len == 5 && memcmp(lexer->start, "const", 5) == 0)
    return TOK_CONST;
  if (len == 5 && memcmp(lexer->start, "false", 5) == 0)
    return TOK_BOOL_LIT;
  if (len == 5 && memcmp(lexer->start, "trait", 5) == 0)
    return TOK_TRAIT;
  if (len == 5 && memcmp(lexer->start, "ushrt", 5) == 0)
    return TOK_USHRT;
  if (len == 5 && memcmp(lexer->start, "while", 5) == 0)
    return TOK_WHILE;
  if (len == 6 && memcmp(lexer->start, "import", 6) == 0)
    return TOK_IMPORT;
  if (len == 8 && memcmp(lexer->start, "continue", 8) == 0)
    return TOK_CONTINUE;
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

  TokenType type = TOK_INT_LIT;
  if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
    advance(lexer);
    while (is_digit(peek(lexer)))
      advance(lexer);
    type = TOK_FLOAT_LIT;
  }

  if (peek(lexer) == 'e' || peek(lexer) == 'E') {
    const char *end = lexer->current + 1;
    if (end < lexer->end && (*end == '+' || *end == '-'))
      end++;
    while (end < lexer->end && is_digit(*end))
      end++;
    // Diagnose exponent-shaped suffixes (including an unfinished marker/sign)
    // as one source-backed error, without swallowing a following line/operator.
    // Identifier-like suffixes such as 1else or 1e2name keep their old tokens.
    if (end == lexer->end || !is_alpha(*end)) {
      lexer->current = end;
      return err_token(
          lexer, "Exponent notation is not supported in source number literals.");
    }
  }

  return make_token(lexer, type);
}

static int valid_escape(char c) {
  return c == 'n' || c == 't' || c == '\\' || c == '"' || c == '\'' || c == '0';
}

static int at_line_break(Lexer *lexer) {
  return peek(lexer) == '\n' ||
         (peek(lexer) == '\r' && peek_next(lexer) == '\n');
}

static Token literal(Lexer *lexer, char quote, TokenType type) {
  const char *error = NULL;
  Span error_span = {0};
  size_t bytes = 0;
  while (!is_at_end(lexer) && peek(lexer) != quote) {
    // Leave the line ending available for statement separation and recovery.
    if (at_line_break(lexer))
      break;
    size_t start = (size_t)(lexer->current - lexer->source->text);
    char c = advance(lexer);
    if (c == '\\') {
      if (is_at_end(lexer) || at_line_break(lexer))
        break;
      if (!valid_escape(advance(lexer)) && !error) {
        error = quote == '"' ? "Unknown escape in string literal."
                             : "Unknown escape in character literal.";
        error_span = span_make(lexer->source, start, start + 2);
      }
    } else if (c == '\0' && !error) {
      error = "Zero byte in literal; use \\0 instead.";
      error_span = span_make(lexer->source, start, start + 1);
    }
    bytes++;
  }
  if (is_at_end(lexer) || peek(lexer) != quote)
    return err_token(lexer, quote == '"' ? "Unterminated string."
                                         : "Unterminated character.");
  advance(lexer);
  if (error)
    return err_token_at(lexer, error, error_span);
  if (quote == '\'' && bytes != 1)
    return err_token(lexer,
                     "A character literal must contain exactly one byte.");
  return make_token(lexer, type);
}

Token lexer_next_token(Lexer *lexer) {
  Token trivia;
  if (skip_whitespace_and_comments(lexer, &trivia))
    return trivia;
  lexer->start = lexer->current;

  if (is_at_end(lexer))
    return make_token(lexer, TOK_EOF);

  char c = advance(lexer);

  if (is_alpha(c))
    return identifier(lexer);
  if (is_digit(c))
    return number(lexer);

  switch (c) {
  case '\n':
    return make_token(lexer, TOK_NEWLINE);
  case '"':
    return literal(lexer, '"', TOK_STR_LIT);
  case '\'':
    return literal(lexer, '\'', TOK_CHAR_LIT);
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
  case '?':
    return make_token(lexer, TOK_QUESTION);
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
    return make_token(lexer, TOK_BIT_AND);
  case '|':
    if (match(lexer, '>'))
      return make_token(lexer, TOK_PIPE);
    return make_token(lexer, TOK_BIT_OR);
  case '~':
    return make_token(lexer, TOK_TILDE);
  case '^':
    return make_token(lexer, TOK_BIT_XOR);
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

  // Report a multi-byte UTF-8 character once, not once per byte.
  unsigned char lead = (unsigned char)c;
  int continuation = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
  for (int i = 0;
       i < continuation && ((unsigned char)peek(lexer) & 0xc0) == 0x80; i++)
    advance(lexer);
  return err_token(lexer, "Unexpected character.");
}

TokenArray lexer_tokenize(Arena *arena, const SourceFile *source,
                          Diagnostics *diagnostics) {
  Lexer lexer;
  lexer_init(&lexer, source, diagnostics);
  TokenArray tokens = {0};
  size_t capacity = 0;
  for (;;) {
    if (tokens.count == capacity) {
      size_t grown_capacity = capacity ? capacity * 2 : 64;
      if (grown_capacity < capacity)
        grown_capacity = SIZE_MAX;
      Token *items = arena_calloc(arena, grown_capacity, sizeof(*items));
      if (tokens.count)
        memcpy(items, tokens.items, tokens.count * sizeof(*items));
      tokens.items = items;
      capacity = grown_capacity;
    }
    Token token = lexer_next_token(&lexer);
    tokens.items[tokens.count++] = token;
    if (token.type == TOK_EOF)
      return tokens;
  }
}
