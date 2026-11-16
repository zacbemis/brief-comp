#ifndef BRIEF_TESTS_LEXER_SUPPORT_H
#define BRIEF_TESTS_LEXER_SUPPORT_H

#include "lexer/lexer.h"
#include "test_utils.h"

#include <stdint.h>

static Arena test_arena;

static inline void init_lexer(Lexer *lexer, const char *text) {
  SourceFile *source =
      source_create(&test_arena, "test.brf", text, strlen(text));
  lexer_init(lexer, source, NULL);
}

// Helper: assert that the next token has the given type and lexeme.
static inline void expect(Lexer *lx, TokenType type, const char *lexeme) {
  Token t = lexer_next_token(lx);
  if (t.type != type) {
    fprintf(stderr,
            "  [FAIL] %s: expected token %s, got %s (\"%.*s\") at %s:%d\n",
            brief_current_test, token_type_name(type), token_type_name(t.type),
            (int)t.length, t.start, __FILE__, __LINE__);
    brief_tests_failed++;
    return;
  }
  if (lexeme != NULL) {
    size_t exp_len = strlen(lexeme);
    if (t.length != exp_len || memcmp(t.start, lexeme, exp_len) != 0) {
      fprintf(stderr,
              "  [FAIL] %s: expected lexeme \"%s\", got \"%.*s\" at %s:%d\n",
              brief_current_test, lexeme, (int)t.length, t.start, __FILE__,
              __LINE__);
      brief_tests_failed++;
    }
  }
}

#endif
