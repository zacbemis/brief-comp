#include "lexer_support.h"

int main(void) {
  arena_init(&test_arena);
  TEST("function types from docs use the Fn keyword") {
    Lexer lx;
    init_lexer(&lx, "keep: Fn<(T): bool> fn Fns");
    expect(&lx, TOK_LOWER_NAME, "keep");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_FN, "Fn");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_UPPER_NAME, "T");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BOOL, "bool");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_LOWER_NAME, "fn");
    expect(&lx, TOK_UPPER_NAME, "Fns");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("empty type and typed bindings from docs") {
    Lexer lx;
    init_lexer(&lx, "log(message: str) -> () {\n  let count: lng = 0\n}");
    expect(&lx, TOK_LOWER_NAME, "log");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "message");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_ARROW, "->");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_LOWER_NAME, "count");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LNG, "lng");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("keywords dropped from the syntax are plain identifiers") {
    Lexer lx;
    init_lexer(&lx, "mut shr unit struct module priv match return");
    expect(&lx, TOK_LOWER_NAME, "mut");
    expect(&lx, TOK_LOWER_NAME, "shr");
    expect(&lx, TOK_LOWER_NAME, "unit");
    expect(&lx, TOK_LOWER_NAME, "struct");
    expect(&lx, TOK_LOWER_NAME, "module");
    expect(&lx, TOK_LOWER_NAME, "priv");
    expect(&lx, TOK_LOWER_NAME, "match");
    expect(&lx, TOK_LOWER_NAME, "return");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("generic syntax from docs uses angle brackets") {
    Lexer lx;
    init_lexer(&lx, "pub Collection<T> impl<T: HasUser> Vec<User>()");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_UPPER_NAME, "Collection");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_UPPER_NAME, "T");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_LOWER_NAME, "impl");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_UPPER_NAME, "T");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_UPPER_NAME, "HasUser");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_UPPER_NAME, "Vec");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("nested generics use separate brackets; shifts stay operator tokens") {
    Lexer lx;
    init_lexer(&lx, "Vec[Vec[int]] >> 1");
    expect(&lx, TOK_UPPER_NAME, "Vec");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_UPPER_NAME, "Vec");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_RSHIFT, ">>");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("union types use bars and bracketed payloads") {
    Lexer lx;
    // Mix an explicit error kind with neutral tags, without inferring outcomes.
    init_lexer(&lx, ":ok[User] | :!err[str] | :missing");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "ok");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_LOWER_NAME, "err");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "missing");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("symbol prefix and type annotation colons do not depend on spacing") {
    Lexer lx;
    init_lexer(&lx, ":ok x:int x: int");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "ok");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("array type and indexing syntax from docs uses brackets") {
    Lexer lx;
    init_lexer(&lx, "int[5] User[] arr[i]");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_LOWER_NAME, "arr");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_LOWER_NAME, "i");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("range operator from docs") {
    Lexer lx;
    init_lexer(&lx, "18..100 0..len(arr)");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_LOWER_NAME, "len");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "arr");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("let binding expression from docs") {
    Lexer lx;
    init_lexer(&lx, "let x = 5");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("const binding from docs") {
    Lexer lx;
    init_lexer(&lx, "const MAX_USERS = 100");
    expect(&lx, TOK_CONST, "const");
    expect(&lx, TOK_UPPER_NAME, "MAX_USERS");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_EOF, NULL);
  }

  arena_destroy(&test_arena);
  RUN_TESTS();
}
