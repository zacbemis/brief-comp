#include "lexer/lexer.h"
#include "test_utils.h"

// Helper: assert that the next token has the given type and lexeme.
static void expect(Lexer *lx, TokenType type, const char *lexeme) {
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

int main(void) {
  TEST("empty source yields EOF") {
    Lexer lx;
    lexer_init(&lx, "");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_EOF, t.type);
  }

  TEST("single-char punctuation and operators") {
    Lexer lx;
    lexer_init(&lx, "=+(){},");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("keywords let and var are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "let var letx vary");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_VAR, "var");
    expect(&lx, TOK_IDENT, "letx");
    expect(&lx, TOK_IDENT, "vary");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("integer literals") {
    Lexer lx;
    lexer_init(&lx, "0 5 12345");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_INT_LIT, "12345");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("let binding expression from docs") {
    Lexer lx;
    lexer_init(&lx, "let x = 5");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("line comments are skipped and do not consume newline") {
    Lexer lx;
    lexer_init(&lx, "let x = 5 // trailing comment\nvar");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_NEW_LN, "\n");
    expect(&lx, TOK_VAR, "var");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("block comments are skipped and tracked across lines") {
    Lexer lx;
    lexer_init(&lx, "let /* multi\nline */ y");
    expect(&lx, TOK_LET, "let");
    Token id = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_IDENT, id.type);
    ASSERT_EQ_STRN("y", id.start, id.length);
    ASSERT_EQ_INT(2, id.line);
  }

  TEST("newlines emit TOK_NEW_LN and bump line counter") {
    Lexer lx;
    lexer_init(&lx, "x\ny");
    Token a = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_IDENT, a.type);
    ASSERT_EQ_INT(1, a.line);
    Token nl = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_NEW_LN, nl.type);
    Token b = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_IDENT, b.type);
    ASSERT_EQ_INT(2, b.line);
  }

  TEST("unexpected character produces an error token") {
    Lexer lx;
    lexer_init(&lx, "@");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, t.type);
  }

  TEST("function call with args tokenizes correctly") {
    Lexer lx;
    lexer_init(&lx, "add(5, x)");
    expect(&lx, TOK_IDENT, "add");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  RUN_TESTS();
}
