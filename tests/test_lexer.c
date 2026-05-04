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
    lexer_init(&lx, "=+-*/%;:,.(){}[]");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_MINUS, "-");
    expect(&lx, TOK_STAR, "*");
    expect(&lx, TOK_SLASH, "/");
    expect(&lx, TOK_PERCENT, "%");
    expect(&lx, TOK_SEMI, ";");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("multi-char operators use the longest match") {
    Lexer lx;
    lexer_init(&lx, "++ -- ** += -= *= /= %= == != <= >= && || << >> => ->");
    expect(&lx, TOK_PLUS_PLUS, "++");
    expect(&lx, TOK_MINUS_MINUS, "--");
    expect(&lx, TOK_STAR_STAR, "**");
    expect(&lx, TOK_PLUS_EQUAL, "+=");
    expect(&lx, TOK_MINUS_EQUAL, "-=");
    expect(&lx, TOK_STAR_EQUAL, "*=");
    expect(&lx, TOK_SLASH_EQUAL, "/=");
    expect(&lx, TOK_PERCENT_EQUAL, "%=");
    expect(&lx, TOK_EQUAL_EQUAL, "==");
    expect(&lx, TOK_BANG_EQUAL, "!=");
    expect(&lx, TOK_LESS_THAN_EQUAL, "<=");
    expect(&lx, TOK_GREATER_THAN_EQUAL, ">=");
    expect(&lx, TOK_AND_AND, "&&");
    expect(&lx, TOK_OR_OR, "||");
    expect(&lx, TOK_LSHIFT, "<<");
    expect(&lx, TOK_RSHIFT, ">>");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_ARROW, "->");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("comparison and bitwise single-char operators") {
    Lexer lx;
    lexer_init(&lx, "< > ! ~ & | ^");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_TILDE, "~");
    expect(&lx, TOK_AND, "&");
    expect(&lx, TOK_OR, "|");
    expect(&lx, TOK_XOR, "^");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("keywords let and var are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "let var mut shr letx vary mutual shrewd");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_VAR, "var");
    expect(&lx, TOK_MUT, "mut");
    expect(&lx, TOK_SHR, "shr");
    expect(&lx, TOK_IDENT, "letx");
    expect(&lx, TOK_IDENT, "vary");
    expect(&lx, TOK_IDENT, "mutual");
    expect(&lx, TOK_IDENT, "shrewd");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("type and definition keywords are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "unit bool int uint shrt ushrt lng ulng flt dub chr str "
                    "struct enum intx structed");
    expect(&lx, TOK_UNIT, "unit");
    expect(&lx, TOK_BOOL, "bool");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_UINT, "uint");
    expect(&lx, TOK_SHRT, "shrt");
    expect(&lx, TOK_USHRT, "ushrt");
    expect(&lx, TOK_LNG, "lng");
    expect(&lx, TOK_ULNG, "ulng");
    expect(&lx, TOK_FLT, "flt");
    expect(&lx, TOK_DUB, "dub");
    expect(&lx, TOK_CHR, "chr");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_STRUCT, "struct");
    expect(&lx, TOK_ENUM, "enum");
    expect(&lx, TOK_IDENT, "intx");
    expect(&lx, TOK_IDENT, "structed");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("generic type syntax from docs uses angle brackets") {
    Lexer lx;
    lexer_init(&lx, "struct Box<T> Result<int, str>");
    expect(&lx, TOK_STRUCT, "struct");
    expect(&lx, TOK_IDENT, "Box");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_IDENT, "T");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_IDENT, "Result");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("array type and indexing syntax from docs uses brackets") {
    Lexer lx;
    lexer_init(&lx, "int[] arr arr[i]");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_IDENT, "arr");
    expect(&lx, TOK_IDENT, "arr");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_IDENT, "i");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("ret and boolean literals") {
    Lexer lx;
    lexer_init(&lx, "ret return true false trueish");
    expect(&lx, TOK_RETURN, "ret");
    expect(&lx, TOK_IDENT, "return");
    expect(&lx, TOK_BOOL_LIT, "true");
    expect(&lx, TOK_BOOL_LIT, "false");
    expect(&lx, TOK_IDENT, "trueish");
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

  TEST("float, string, and character literals") {
    Lexer lx;
    lexer_init(&lx, "12.34 \"hello\" 'c' '\\n'");
    expect(&lx, TOK_FLOAT_LIT, "12.34");
    expect(&lx, TOK_STR_LIT, "\"hello\"");
    expect(&lx, TOK_CHAR_LIT, "'c'");
    expect(&lx, TOK_CHAR_LIT, "'\\n'");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("range operator from docs") {
    Lexer lx;
    lexer_init(&lx, "0..len(arr)");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_IDENT, "len");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "arr");
    expect(&lx, TOK_RPAREN, ")");
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
    expect(&lx, TOK_NEWLINE, "\n");
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

  TEST("newlines emit TOK_NEWLINE and bump line counter") {
    Lexer lx;
    lexer_init(&lx, "x\ny");
    Token a = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_IDENT, a.type);
    ASSERT_EQ_INT(1, a.line);
    Token nl = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_NEWLINE, nl.type);
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
