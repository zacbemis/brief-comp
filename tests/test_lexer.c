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
    lexer_init(&lx, "=+-*/%;:,.?(){}[]");
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
    expect(&lx, TOK_QUESTION, "?");
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
    lexer_init(&lx, "++ -- ** += -= *= /= %= == != <= >= && || << >> => -> "
                    "|> .. ..<");
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
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_DOT_DOT_LESS, "..<");
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

  TEST("binding keywords are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "let var const letx vary constant");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_VAR, "var");
    expect(&lx, TOK_CONST, "const");
    expect(&lx, TOK_IDENT, "letx");
    expect(&lx, TOK_IDENT, "vary");
    expect(&lx, TOK_IDENT, "constant");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("type and definition keywords are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "bool int uint shrt ushrt lng ulng flt dub chr str "
                    "Sym Self enum mod import trait impl pub ovr intx sym "
                    "self modern imports traits implicit public override");
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
    expect(&lx, TOK_SYM, "Sym");
    expect(&lx, TOK_SELF, "Self");
    expect(&lx, TOK_ENUM, "enum");
    expect(&lx, TOK_MOD, "mod");
    expect(&lx, TOK_IMPORT, "import");
    expect(&lx, TOK_TRAIT, "trait");
    expect(&lx, TOK_IMPL, "impl");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_OVR, "ovr");
    expect(&lx, TOK_IDENT, "intx");
    expect(&lx, TOK_IDENT, "sym");
    expect(&lx, TOK_IDENT, "self");
    expect(&lx, TOK_IDENT, "modern");
    expect(&lx, TOK_IDENT, "imports");
    expect(&lx, TOK_IDENT, "traits");
    expect(&lx, TOK_IDENT, "implicit");
    expect(&lx, TOK_IDENT, "public");
    expect(&lx, TOK_IDENT, "override");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("control flow keywords are distinguished from identifiers") {
    Lexer lx;
    lexer_init(&lx, "if else for in while ret iffy elsewhere format inner "
                    "whiley retry");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_ELSE, "else");
    expect(&lx, TOK_FOR, "for");
    expect(&lx, TOK_IN, "in");
    expect(&lx, TOK_WHILE, "while");
    expect(&lx, TOK_RET, "ret");
    expect(&lx, TOK_IDENT, "iffy");
    expect(&lx, TOK_IDENT, "elsewhere");
    expect(&lx, TOK_IDENT, "format");
    expect(&lx, TOK_IDENT, "inner");
    expect(&lx, TOK_IDENT, "whiley");
    expect(&lx, TOK_IDENT, "retry");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("keywords dropped from the syntax are plain identifiers") {
    Lexer lx;
    lexer_init(&lx, "mut shr unit struct module priv match return");
    expect(&lx, TOK_IDENT, "mut");
    expect(&lx, TOK_IDENT, "shr");
    expect(&lx, TOK_IDENT, "unit");
    expect(&lx, TOK_IDENT, "struct");
    expect(&lx, TOK_IDENT, "module");
    expect(&lx, TOK_IDENT, "priv");
    expect(&lx, TOK_IDENT, "match");
    expect(&lx, TOK_IDENT, "return");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("generic syntax from docs uses angle brackets") {
    Lexer lx;
    lexer_init(&lx, "pub Collection<T> impl<T: HasUser> Vec<User>()");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_IDENT, "Collection");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_IDENT, "T");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_IMPL, "impl");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_IDENT, "T");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "HasUser");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_IDENT, "Vec");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("nested generics close with a single shift token") {
    Lexer lx;
    lexer_init(&lx, "Vec<Vec<int>>");
    expect(&lx, TOK_IDENT, "Vec");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_IDENT, "Vec");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RSHIFT, ">>");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("Sym type from docs uses angle brackets and colon-prefixed symbols") {
    Lexer lx;
    lexer_init(&lx, "Sym<:ok(User), :err(str), :missing>");
    expect(&lx, TOK_SYM, "Sym");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "ok");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "err");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "missing");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("symbol prefix and type annotation colons do not depend on spacing") {
    Lexer lx;
    lexer_init(&lx, ":ok x:int x: int");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "ok");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("array type and indexing syntax from docs uses brackets") {
    Lexer lx;
    lexer_init(&lx, "int[5] User[] arr[i]");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_IDENT, "arr");
    expect(&lx, TOK_LBRACKET, "[");
    expect(&lx, TOK_IDENT, "i");
    expect(&lx, TOK_RBRACKET, "]");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("identifiers and boolean literals") {
    Lexer lx;
    lexer_init(&lx, "true false trueish");
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

  TEST("range operators from docs") {
    Lexer lx;
    lexer_init(&lx, "18..100 18..<100 0..len(arr)");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_DOT_DOT_LESS, "..<");
    expect(&lx, TOK_INT_LIT, "100");
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

  TEST("const binding from docs") {
    Lexer lx;
    lexer_init(&lx, "const MAX_USERS = 100");
    expect(&lx, TOK_CONST, "const");
    expect(&lx, TOK_IDENT, "MAX_USERS");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "100");
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

  TEST("if/else expression") {
    Lexer lx;
    lexer_init(&lx, "if y == 0 { err } else { ok }");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_EQUAL_EQUAL, "==");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_IDENT, "err");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_ELSE, "else");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_IDENT, "ok");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("early return from docs") {
    Lexer lx;
    lexer_init(&lx, "if age < 18 { ret :err(\"Age is too low\") }");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_IDENT, "age");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RET, "ret");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "err");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_STR_LIT, "\"Age is too low\"");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("for-in loop from docs") {
    Lexer lx;
    lexer_init(&lx, "for user in users { }");
    expect(&lx, TOK_FOR, "for");
    expect(&lx, TOK_IDENT, "user");
    expect(&lx, TOK_IN, "in");
    expect(&lx, TOK_IDENT, "users");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("function declaration from docs") {
    Lexer lx;
    lexer_init(&lx, "add(x: int, y: int): int { x + y }");
    expect(&lx, TOK_IDENT, "add");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("lambda from docs") {
    Lexer lx;
    lexer_init(&lx, "let add = (x: int, y: int) => x + y");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_IDENT, "add");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("mutable borrow parameter from docs") {
    Lexer lx;
    lexer_init(&lx, "setAge(user: &User, age: int)");
    expect(&lx, TOK_IDENT, "setAge");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "user");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_AND, "&");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_IDENT, "age");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_INT, "int");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("propagate and panic postfix operators from docs") {
    Lexer lx;
    lexer_init(&lx, "User.new(\"Bob\", 20)? file.close()! a != b");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_IDENT, "new");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_STR_LIT, "\"Bob\"");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_INT_LIT, "20");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_QUESTION, "?");
    expect(&lx, TOK_IDENT, "file");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_IDENT, "close");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_IDENT, "a");
    expect(&lx, TOK_BANG_EQUAL, "!=");
    expect(&lx, TOK_IDENT, "b");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline is distinct from bitwise and logical or") {
    Lexer lx;
    lexer_init(&lx, "a | b || c |> d");
    expect(&lx, TOK_IDENT, "a");
    expect(&lx, TOK_OR, "|");
    expect(&lx, TOK_IDENT, "b");
    expect(&lx, TOK_OR_OR, "||");
    expect(&lx, TOK_IDENT, "c");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_IDENT, "d");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline pattern match from docs") {
    Lexer lx;
    lexer_init(&lx, "age |> {\n< 18 => \"Minor\"\n18..<100 => "
                    "\"Adult\"\n>= 100 => \"Senior\"\n}");
    expect(&lx, TOK_IDENT, "age");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_STR_LIT, "\"Minor\"");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_INT_LIT, "18");
    expect(&lx, TOK_DOT_DOT_LESS, "..<");
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
    lexer_init(&lx, "users.find(predicate) |> { :ok(user) => :found(user) "
                    ":none => :missing }");
    expect(&lx, TOK_IDENT, "users");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_IDENT, "find");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "predicate");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "ok");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "user");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "found");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "user");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "none");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "missing");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("module with import and struct from docs") {
    Lexer lx;
    lexer_init(&lx, "mod User {\nimport std.Json\npub User {\nname: str\n}\n}");
    expect(&lx, TOK_MOD, "mod");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IMPORT, "import");
    expect(&lx, TOK_IDENT, "std");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_IDENT, "Json");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IDENT, "name");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("trait declaration from docs") {
    Lexer lx;
    lexer_init(&lx, "pub trait HasUser { getUser(value: Self): User }");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_TRAIT, "trait");
    expect(&lx, TOK_IDENT, "HasUser");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_IDENT, "getUser");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "value");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_SELF, "Self");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "User");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("impl with override from docs") {
    Lexer lx;
    lexer_init(&lx, "impl Admin: HasUser { ovr pub getName(admin: Admin): "
                    "str }");
    expect(&lx, TOK_IMPL, "impl");
    expect(&lx, TOK_IDENT, "Admin");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "HasUser");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_OVR, "ovr");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_IDENT, "getName");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "admin");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_IDENT, "Admin");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_RBRACE, "}");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("double semicolon is a single SEMI_SEMI token") {
    Lexer lx;
    lexer_init(&lx, "some(i);; ; ;;;");
    expect(&lx, TOK_IDENT, "some");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "i");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_SEMI_SEMI, ";;");
    expect(&lx, TOK_SEMI, ";");
    expect(&lx, TOK_SEMI_SEMI, ";;");
    expect(&lx, TOK_SEMI, ";");
    expect(&lx, TOK_EOF, NULL);
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
