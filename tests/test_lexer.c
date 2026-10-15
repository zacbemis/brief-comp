#include "lexer/lexer.h"
#include "test_utils.h"

#include <stdint.h>

static Arena test_arena;

static void init_lexer(Lexer *lexer, const char *text) {
  SourceFile *source =
      source_create(&test_arena, "test.brf", text, strlen(text));
  lexer_init(lexer, source, NULL);
}

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
  arena_init(&test_arena);
  TEST("empty source yields EOF") {
    Lexer lx;
    init_lexer(&lx, "");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_EOF, t.type);
  }

  TEST("single-char punctuation and operators") {
    Lexer lx;
    init_lexer(&lx, "=+-*/%;:,.?(){}[]");
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
    init_lexer(&lx, "++ -- ** += -= *= /= %= == != <= >= << >> => -> |> ..");
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
    expect(&lx, TOK_LSHIFT, "<<");
    expect(&lx, TOK_RSHIFT, ">>");
    expect(&lx, TOK_FAT_ARROW, "=>");
    expect(&lx, TOK_ARROW, "->");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("comparison and bitwise single-char operators") {
    Lexer lx;
    init_lexer(&lx, "< > ! ~ & | ^");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_TILDE, "~");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_BIT_XOR, "^");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("binding keywords are distinguished from identifiers") {
    Lexer lx;
    init_lexer(&lx, "let var const letx vary constant");
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
    init_lexer(&lx, "bool int uint shrt ushrt lng ulng flt dub chr str "
                    "Sym Self type mod import trait impl pub ovr types enum "
                    "intx sym self modern imports traits implicit public "
                    "override");
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
    expect(&lx, TOK_TYPE, "type");
    expect(&lx, TOK_MOD, "mod");
    expect(&lx, TOK_IMPORT, "import");
    expect(&lx, TOK_TRAIT, "trait");
    expect(&lx, TOK_IMPL, "impl");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_OVR, "ovr");
    expect(&lx, TOK_IDENT, "types");
    expect(&lx, TOK_IDENT, "enum");
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
    init_lexer(&lx, "if else for in while break continue ret iffy elsewhere "
                    "format inner whiley breaks continued retry");
    expect(&lx, TOK_IF, "if");
    expect(&lx, TOK_ELSE, "else");
    expect(&lx, TOK_FOR, "for");
    expect(&lx, TOK_IN, "in");
    expect(&lx, TOK_WHILE, "while");
    expect(&lx, TOK_BREAK, "break");
    expect(&lx, TOK_CONTINUE, "continue");
    expect(&lx, TOK_RET, "ret");
    expect(&lx, TOK_IDENT, "iffy");
    expect(&lx, TOK_IDENT, "elsewhere");
    expect(&lx, TOK_IDENT, "format");
    expect(&lx, TOK_IDENT, "inner");
    expect(&lx, TOK_IDENT, "whiley");
    expect(&lx, TOK_IDENT, "breaks");
    expect(&lx, TOK_IDENT, "continued");
    expect(&lx, TOK_IDENT, "retry");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("function types from docs use the Fn keyword") {
    Lexer lx;
    init_lexer(&lx, "keep: Fn<(T): bool> fn Fns");
    expect(&lx, TOK_IDENT, "keep");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_FN, "Fn");
    expect(&lx, TOK_LESS_THAN, "<");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "T");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BOOL, "bool");
    expect(&lx, TOK_GREATER_THAN, ">");
    expect(&lx, TOK_IDENT, "fn");
    expect(&lx, TOK_IDENT, "Fns");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("empty type and typed bindings from docs") {
    Lexer lx;
    init_lexer(&lx, "log(message: str): () {\n  let count: lng = 0\n}");
    expect(&lx, TOK_IDENT, "log");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "message");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_LBRACE, "{");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_IDENT, "count");
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
    init_lexer(&lx, "pub Collection<T> impl<T: HasUser> Vec<User>()");
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
    init_lexer(&lx, "Vec<Vec<int>>");
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
    init_lexer(&lx, "Sym<:ok(User), :err(str), :missing>");
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
    init_lexer(&lx, ":ok x:int x: int");
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
    init_lexer(&lx, "int[5] User[] arr[i]");
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
    init_lexer(&lx, "true false trueish");
    expect(&lx, TOK_BOOL_LIT, "true");
    expect(&lx, TOK_BOOL_LIT, "false");
    expect(&lx, TOK_IDENT, "trueish");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("integer literals") {
    Lexer lx;
    init_lexer(&lx, "0 5 12345");
    expect(&lx, TOK_INT_LIT, "0");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_INT_LIT, "12345");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("float, string, and character literals") {
    Lexer lx;
    init_lexer(&lx, "12.34 \"hello\" 'c' '\\n'");
    expect(&lx, TOK_FLOAT_LIT, "12.34");
    expect(&lx, TOK_STR_LIT, "\"hello\"");
    expect(&lx, TOK_CHAR_LIT, "'c'");
    expect(&lx, TOK_CHAR_LIT, "'\\n'");
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
    expect(&lx, TOK_IDENT, "len");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "arr");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("let binding expression from docs") {
    Lexer lx;
    init_lexer(&lx, "let x = 5");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("const binding from docs") {
    Lexer lx;
    init_lexer(&lx, "const MAX_USERS = 100");
    expect(&lx, TOK_CONST, "const");
    expect(&lx, TOK_IDENT, "MAX_USERS");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_INT_LIT, "100");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("line comments are skipped and do not consume newline") {
    Lexer lx;
    init_lexer(&lx, "let x = 5 // trailing comment\nvar");
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
    init_lexer(&lx, "let /* multi\nline */ y");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_NEWLINE, "\n");
    Token id = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_IDENT, id.type);
    ASSERT_EQ_STRN("y", id.start, id.length);
    ASSERT_EQ_INT(2, id.line);
  }

  TEST("newlines emit TOK_NEWLINE and bump line counter") {
    Lexer lx;
    init_lexer(&lx, "x\ny");
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
    init_lexer(&lx, "@");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, t.type);
  }

  TEST("if/else expression") {
    Lexer lx;
    init_lexer(&lx, "if y == 0 { err } else { ok }");
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
    init_lexer(&lx, "if age < 18 { ret :err(\"Age is too low\") }");
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
    init_lexer(&lx, "for user in users { }");
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
    init_lexer(&lx, "add(x: int, y: int): int { x + y }");
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
    init_lexer(&lx, "let add = (x: int, y: int) => x + y");
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
    init_lexer(&lx, "setAge(user: &User, age: int)");
    expect(&lx, TOK_IDENT, "setAge");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_IDENT, "user");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BIT_AND, "&");
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
    init_lexer(&lx, "User.new(\"Bob\", 20)? file.close()! a != b");
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

  TEST("logical operators are the keywords and/or") {
    Lexer lx;
    init_lexer(&lx, "a and b or c android order");
    expect(&lx, TOK_IDENT, "a");
    expect(&lx, TOK_AND, "and");
    expect(&lx, TOK_IDENT, "b");
    expect(&lx, TOK_OR, "or");
    expect(&lx, TOK_IDENT, "c");
    expect(&lx, TOK_IDENT, "android");
    expect(&lx, TOK_IDENT, "order");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("&& and || are not operators") {
    Lexer lx;
    init_lexer(&lx, "a && b || c");
    expect(&lx, TOK_IDENT, "a");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_IDENT, "b");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_IDENT, "c");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline is distinct from bitwise or") {
    Lexer lx;
    init_lexer(&lx, "a | b |> c");
    expect(&lx, TOK_IDENT, "a");
    expect(&lx, TOK_BIT_OR, "|");
    expect(&lx, TOK_IDENT, "b");
    expect(&lx, TOK_PIPE, "|>");
    expect(&lx, TOK_IDENT, "c");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("pipeline pattern match from docs") {
    Lexer lx;
    init_lexer(&lx, "age |> {\n< 18 => \"Minor\"\n18..100 => "
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
    init_lexer(&lx, "users.find(predicate) |> { :ok(user) => :found(user) "
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
    init_lexer(&lx, "mod User {\nimport std.Json\npub User {\nname: str\n}\n}");
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
    init_lexer(&lx, "pub trait HasUser { getUser(value: Self): User }");
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
    init_lexer(&lx, "impl Admin: HasUser { ovr pub getName(admin: Admin): "
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
    init_lexer(&lx, "some(i);; ; ;;;");
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
    init_lexer(&lx, "add(5, x)");
    expect(&lx, TOK_IDENT, "add");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_INT_LIT, "5");
    expect(&lx, TOK_COMMA, ",");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("all documented escapes are accepted in strings and characters") {
    const char escapes[] = {'n', 't', '\\', '"', '\'', '0'};
    for (size_t i = 0; i < sizeof(escapes); i++) {
      Lexer lx;
      char string_text[] = {'"', '\\', escapes[i], '"', '\0'};
      init_lexer(&lx, string_text);
      expect(&lx, TOK_STR_LIT, string_text);
      expect(&lx, TOK_EOF, NULL);
      char char_text[] = {'\'', '\\', escapes[i], '\'', '\0'};
      init_lexer(&lx, char_text);
      expect(&lx, TOK_CHAR_LIT, char_text);
      expect(&lx, TOK_EOF, NULL);
    }
  }

  TEST("unknown escapes are errors and recovery consumes the literal") {
    const char escapes[] = {'r', 'a', 'x', 'u', 'q'};
    for (size_t i = 0; i < sizeof(escapes); i++) {
      Lexer lx;
      char text[] = {'"', '\\', escapes[i], '"', ' ', 'x', '\0'};
      init_lexer(&lx, text);
      Token token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_INT(4, token.length);
      ASSERT_EQ_STRN("Unknown escape in string literal.", token.message,
                     strlen(token.message));
      ASSERT_TRUE(token.start == lx.source->text);
      expect(&lx, TOK_IDENT, "x");
      expect(&lx, TOK_EOF, NULL);
      text[0] = '\'';
      text[3] = '\'';
      init_lexer(&lx, text);
      token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_STRN("Unknown escape in character literal.", token.message,
                     strlen(token.message));
      expect(&lx, TOK_IDENT, "x");
      expect(&lx, TOK_EOF, NULL);
    }
  }

  TEST("unknown escapes highlight the first escape but keep the whole token") {
    const char *cases[] = {"\"café \\q\\r\" next", "'\\q' next",
                           "\"ok\\n café \\q\" next"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      SourceFile *source = source_create(&test_arena, "escape.brf", cases[i],
                                         strlen(cases[i]));
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &test_arena);
      Lexer lx;
      lexer_init(&lx, source, &diagnostics);
      Token error = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, error.type);
      ASSERT_EQ_INT(1, diagnostics.count);
      ASSERT_EQ_INT(0, error.span.start);
      ASSERT_TRUE(error.span.end > 2);
      if (diagnostics.count == 1) {
        Span highlight = diagnostics.items[0]->span;
        size_t expected = (size_t)(strstr(cases[i], "\\q") - cases[i]);
        ASSERT_TRUE(highlight.file == source);
        ASSERT_EQ_INT(expected, highlight.start);
        ASSERT_EQ_INT(expected + 2, highlight.end);
      }
      expect(&lx, TOK_IDENT, "next");
      expect(&lx, TOK_EOF, NULL);
    }
  }

  TEST("characters must contain exactly one decoded byte") {
    const char *invalid[] = {"''", "'ab'", "'\\n\\t'", "'\xc3\xa9'"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      Lexer lx;
      init_lexer(&lx, invalid[i]);
      Token token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_STRN("A character literal must contain exactly one byte.",
                     token.message, strlen(token.message));
      expect(&lx, TOK_EOF, NULL);
    }
    Lexer lx;
    init_lexer(&lx, "\"\" \"\xc3\xa9\"");
    expect(&lx, TOK_STR_LIT, "\"\"");
    expect(&lx, TOK_STR_LIT, "\"\xc3\xa9\"");
  }

  TEST("unterminated literals at EOF and trailing backslashes") {
    const char *strings[] = {"\"", "\"abc", "\"abc\\"};
    const char *characters[] = {"'", "'a", "'\\"};
    for (size_t i = 0; i < 3; i++) {
      Lexer lx;
      init_lexer(&lx, strings[i]);
      Token token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_STRN("Unterminated string.", token.message,
                     strlen(token.message));
      expect(&lx, TOK_EOF, NULL);
      init_lexer(&lx, characters[i]);
      token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_STRN("Unterminated character.", token.message,
                     strlen(token.message));
      expect(&lx, TOK_EOF, NULL);
    }
  }

  TEST("unterminated characters leave the newline available for recovery") {
    Lexer lx;
    init_lexer(&lx, "'a\nx");
    expect(&lx, TOK_ERR, "'a");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IDENT, "x");
    init_lexer(&lx, "'\\\nx");
    expect(&lx, TOK_ERR, "'\\");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IDENT, "x");
  }

  TEST("unterminated comments report the opening location only once") {
    const char *text = "x\n  /* comment\nmore";
    SourceFile *source =
        source_create(&test_arena, "comment.brf", text, strlen(text));
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &test_arena);
    Lexer lx;
    lexer_init(&lx, source, &diagnostics);
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_NEWLINE, "\n");
    Token error = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, error.type);
    ASSERT_EQ_INT(4, error.span.start);
    ASSERT_EQ_INT(source->length, error.span.end);
    ASSERT_EQ_INT(2, error.line);
    ASSERT_EQ_INT(1, diagnostics.count);
    ASSERT_EQ_STRN("Unterminated block comment.", error.message,
                   strlen(error.message));
    expect(&lx, TOK_EOF, NULL);
    expect(&lx, TOK_EOF, NULL);
    ASSERT_EQ_INT(1, diagnostics.count);
  }

  TEST("comment delimiters at EOF and consecutive comments") {
    Lexer lx;
    init_lexer(&lx, "/**//*ok*/// end");
    expect(&lx, TOK_EOF, NULL);
    init_lexer(&lx, "/*");
    expect(&lx, TOK_ERR, "/*");
    expect(&lx, TOK_EOF, NULL);
    init_lexer(&lx, "/* *");
    expect(&lx, TOK_ERR, "/* *");
    expect(&lx, TOK_EOF, NULL);
    init_lexer(&lx, "/");
    expect(&lx, TOK_SLASH, "/");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("block-comment newlines do not merge statements") {
    Lexer lx;
    init_lexer(&lx, "x /* one\n\ntwo */ y");
    expect(&lx, TOK_IDENT, "x");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_IDENT, "y");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("token spans refer to source and use starting lines for multiline "
       "tokens") {
    Lexer lx;
    init_lexer(&lx, " \tfoo\r\n  \"a\nb\" @");
    Token name = lexer_next_token(&lx);
    ASSERT_EQ_INT(2, name.span.start);
    ASSERT_EQ_INT(5, name.span.end);
    ASSERT_EQ_INT(1, name.line);
    ASSERT_TRUE(name.span.file == lx.source);
    ASSERT_TRUE(name.message == NULL);
    Token newline = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_NEWLINE, newline.type);
    ASSERT_EQ_INT(6, newline.span.start);
    Token string_token = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_STR_LIT, string_token.type);
    ASSERT_EQ_INT(9, string_token.span.start);
    ASSERT_EQ_INT(14, string_token.span.end);
    ASSERT_EQ_INT(2, string_token.line);
    Token error = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, error.type);
    ASSERT_EQ_STRN("@", error.start, error.length);
    ASSERT_EQ_INT(15, error.span.start);
    ASSERT_EQ_INT(16, error.span.end);
    ASSERT_EQ_INT(3, error.line);
    ASSERT_EQ_INT(4, source_position(lx.source, error.span.start).column);
    Token eof = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_EOF, eof.type);
    ASSERT_EQ_INT(16, eof.span.start);
    ASSERT_EQ_INT(16, eof.span.end);
    ASSERT_EQ_INT(0, eof.length);
  }

  TEST("embedded source zero bytes cannot silently truncate lexing") {
    const char text[] = {'x', '\0', 'y', ' ', '"', '\0', '"'};
    SourceFile *source =
        source_create(&test_arena, "zero.brf", text, sizeof(text));
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &test_arena);
    TokenArray tokens = lexer_tokenize(&test_arena, source, &diagnostics);
    ASSERT_EQ_INT(5, tokens.count);
    ASSERT_EQ_INT(2, diagnostics.count);
    ASSERT_EQ_INT(TOK_IDENT, tokens.items[0].type);
    ASSERT_EQ_INT(TOK_ERR, tokens.items[1].type);
    ASSERT_EQ_INT(TOK_IDENT, tokens.items[2].type);
    ASSERT_EQ_INT(TOK_ERR, tokens.items[3].type);
    ASSERT_EQ_INT(TOK_EOF, tokens.items[tokens.count - 1].type);
  }

  TEST("whole-file lexing collects multiple errors and preserves all tokens") {
    const char *text = "@\n\"\\q\"\n''\nlet x = 1\n/* unfinished";
    SourceFile *source =
        source_create(&test_arena, "errors.brf", text, strlen(text));
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &test_arena);
    TokenArray tokens = lexer_tokenize(&test_arena, source, &diagnostics);
    ASSERT_EQ_INT(4, diagnostics.count);
    ASSERT_EQ_INT(13, tokens.count);
    ASSERT_EQ_INT(TOK_LET, tokens.items[6].type);
    ASSERT_EQ_INT(TOK_EOF, tokens.items[tokens.count - 1].type);
    Lexer lx;
    lexer_init(&lx, source, NULL);
    for (size_t i = 0; i < tokens.count; i++) {
      Token streamed = lexer_next_token(&lx);
      ASSERT_EQ_INT(streamed.type, tokens.items[i].type);
      ASSERT_EQ_INT(streamed.span.start, tokens.items[i].span.start);
      ASSERT_EQ_INT(streamed.span.end, tokens.items[i].span.end);
    }
  }

  TEST("whole-file token arrays grow without losing earlier slices") {
    char text[600];
    for (size_t i = 0; i < sizeof(text); i += 2) {
      text[i] = 'x';
      text[i + 1] = '\n';
    }
    SourceFile *source =
        source_create(&test_arena, "many.brf", text, sizeof(text));
    TokenArray tokens = lexer_tokenize(&test_arena, source, NULL);
    ASSERT_EQ_INT(601, tokens.count);
    for (size_t i = 0; i < 600; i++) {
      ASSERT_EQ_INT(i % 2 ? TOK_NEWLINE : TOK_IDENT, tokens.items[i].type);
      ASSERT_EQ_INT(i, tokens.items[i].span.start);
      ASSERT_EQ_INT(i + 1, tokens.items[i].span.end);
    }
    ASSERT_EQ_INT(TOK_EOF, tokens.items[600].type);
    ASSERT_EQ_INT(301, tokens.items[600].line);
  }

  TEST("empty whole-file lexing returns one EOF token") {
    SourceFile *source = source_create(&test_arena, "empty.brf", "", 0);
    TokenArray tokens = lexer_tokenize(&test_arena, source, NULL);
    ASSERT_EQ_INT(1, tokens.count);
    ASSERT_EQ_INT(TOK_EOF, tokens.items[0].type);
    ASSERT_EQ_INT(0, tokens.items[0].span.start);
    ASSERT_EQ_INT(0, tokens.items[0].span.end);
  }

  TEST("arbitrary byte input makes progress and batch/stream lexing agree") {
    uint32_t state = 1;
    for (size_t sample = 0; sample < 1000; sample++) {
      Arena arena;
      arena_init(&arena);
      char text[64];
      for (size_t i = 0; i < sizeof(text); i++) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        text[i] = (char)(state >> 24);
      }
      SourceFile *source = source_create(&arena, "bytes.brf", text, sizeof(text));
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &arena);
      Lexer lx;
      lexer_init(&lx, source, NULL);
      Token streamed[66];
      size_t count = 0;
      size_t errors = 0;
      do {
        Token token = lexer_next_token(&lx);
        streamed[count++] = token;
        ASSERT_TRUE(token.span.file == source);
        ASSERT_TRUE(token.span.start <= token.span.end);
        ASSERT_TRUE(token.span.end <= source->length);
        ASSERT_TRUE(token.start == source->text + token.span.start);
        ASSERT_EQ_INT(token.span.end - token.span.start, token.length);
        if (token.type == TOK_ERR) {
          ASSERT_TRUE(token.message != NULL);
          errors++;
        }
      } while (streamed[count - 1].type != TOK_EOF && count < 66);
      ASSERT_EQ_INT(TOK_EOF, streamed[count - 1].type);
      if (streamed[count - 1].type == TOK_EOF) {
        TokenArray tokens = lexer_tokenize(&arena, source, &diagnostics);
        ASSERT_EQ_INT(count, tokens.count);
        ASSERT_EQ_INT(errors, diagnostics.count);
        if (count == tokens.count) {
          for (size_t i = 0; i < count; i++) {
            ASSERT_EQ_INT(streamed[i].type, tokens.items[i].type);
            ASSERT_EQ_INT(streamed[i].span.start, tokens.items[i].span.start);
            ASSERT_EQ_INT(streamed[i].span.end, tokens.items[i].span.end);
          }
        }
      }
      arena_destroy(&arena);
    }
  }

  arena_destroy(&test_arena);
  RUN_TESTS();
}
