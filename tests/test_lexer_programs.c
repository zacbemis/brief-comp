#include "lexer_support.h"

int main(void) {
  arena_init(&test_arena);
  TEST("if/else expression") {
    Lexer lx;
    init_lexer(&lx, "if y == 0 { err } else { ok }");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_EQUAL_EQUAL, "==");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_LOWER_NAME, "err");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_ELSE, "else");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_LOWER_NAME, "ok");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("early return from docs") {
    Lexer lx;
    init_lexer(&lx, "if age < 18 { ret :!err[\"Age is too low\"] }");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_LOWER_NAME, "age");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RET, "ret");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_LOWER_NAME, "err");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_STR_LIT, "\"Age is too low\"");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("for-in loop from docs") {
    Lexer lx;
    init_lexer(&lx, "for user in users { }");
    expect(&lx, TOK_FOR, "for");
    expect(&lx, TOK_LOWER_NAME, "user");
    expect(&lx, TOK_IN, "in");
    expect(&lx, TOK_LOWER_NAME, "users");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("function declaration from docs") {
    Lexer lx;
    init_lexer(&lx, "add(x: int, y: int) -> int { x + y }");
    expect(&lx, TOK_LOWER_NAME, "add");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_ARROW, "->");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("lambda from docs") {
    Lexer lx;
    init_lexer(&lx, "let add = (x: int, y: int) => x + y");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_LOWER_NAME, "add");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("mutable borrow parameter from docs") {
    Lexer lx;
    init_lexer(&lx, "setAge(user: &User, age: int)");
    expect(&lx, TOK_LOWER_NAME, "setAge");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "user");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_LOWER_NAME, "age");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("propagate and panic postfix operators from docs") {
    Lexer lx;
    init_lexer(&lx, "User.new(\"Bob\", 20)? file.close()! a != b");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LOWER_NAME, "new");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_STR_LIT, "\"Bob\"");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_INT_LIT, "20");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_QUESTION, "?");
    expect(&lx, TOK_LOWER_NAME, "file");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LOWER_NAME, "close");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_LOWER_NAME, "a");
    expect(&lx, TOK_BANG_EQUAL, "!=");
    expect(&lx, TOK_LOWER_NAME, "b");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("logical operators are the keywords and/or/not") {
    Lexer lx;
    init_lexer(
        &lx, "a and b or not c android order notifier not1 not_thing NOT Not");
    expect(&lx, TOK_LOWER_NAME, "a");
    expect(&lx, TOK_AND, "and");
    expect(&lx, TOK_LOWER_NAME, "b");
    expect(&lx, TOK_OR, "or");
    expect(&lx, TOK_NOT, "not");
    expect(&lx, TOK_LOWER_NAME, "c");
    expect(&lx, TOK_LOWER_NAME, "android");
    expect(&lx, TOK_LOWER_NAME, "order");
    expect(&lx, TOK_LOWER_NAME, "notifier");
    expect(&lx, TOK_LOWER_NAME, "not1");
    expect(&lx, TOK_LOWER_NAME, "not_thing");
    expect(&lx, TOK_UPPER_NAME, "NOT");
    expect(&lx, TOK_UPPER_NAME, "Not");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("&& and || are not operators") {
    Lexer lx;
    init_lexer(&lx, "a && b || c");
    expect(&lx, TOK_LOWER_NAME, "a");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_LOWER_NAME, "b");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_LOWER_NAME, "c");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline is distinct from bitwise or") {
    Lexer lx;
    init_lexer(&lx, "a | b |> c");
    expect(&lx, TOK_LOWER_NAME, "a");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_LOWER_NAME, "b");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_LOWER_NAME, "c");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline pattern match from docs") {
    Lexer lx;
    init_lexer(&lx, "age |> {\n< 18 => \"Minor\"\n18..100 => "
                    "\"Adult\"\n>= 100 => \"Senior\"\n}");
    expect(&lx, TOK_LOWER_NAME, "age");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_STR_LIT, "\"Minor\"");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_STR_LIT, "\"Adult\"");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_GREATER_THAN_EQUAL, ">=");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_STR_LIT, "\"Senior\"");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline symbol match from docs") {
    Lexer lx;
    init_lexer(&lx, "users.find(predicate) |> { :ok[user] => :found[user] "
                    ":none => :missing }");
    expect(&lx, TOK_LOWER_NAME, "users");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LOWER_NAME, "find");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "predicate");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "ok");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_LOWER_NAME, "user");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "found");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_LOWER_NAME, "user");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "none");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "missing");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("module with import and struct from docs") {
    Lexer lx;
    init_lexer(&lx, "mod User {\nimport std.Json\npub User {\nname: str\n}\n}");
    expect(&lx, TOK_MOD, "mod");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IMPORT, "import");
    expect(&lx, TOK_LOWER_NAME, "std");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_UPPER_NAME, "Json");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_UPPER_NAME, "User");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LOWER_NAME, "name");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("double semicolon is a single SEMI_SEMI token") {
    Lexer lx;
    init_lexer(&lx, "some(i);; ; ;;;");
    expect(&lx, TOK_LOWER_NAME, "some");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_LOWER_NAME, "i");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_SEMI_SEMI, ";;");
    expect(&lx, TOK_SEMI, ";");
    expect(&lx, TOK_SEMI_SEMI, ";;");
    expect(&lx, TOK_SEMI, ";");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("function call with args tokenizes correctly") {
    Lexer lx;
    init_lexer(&lx, "add(5, x)");
    expect(&lx, TOK_LOWER_NAME, "add");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  arena_destroy(&test_arena);
  RUN_TESTS();
}
