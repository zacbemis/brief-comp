#include "lexer_support.h"

int main(void) {
  arena_init(&test_arena);
  TEST("empty source yields EOF") {
    Lexer lx;
    init_lexer(&lx, "");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_EOF, t.type);
  }

  TEST("names classify by their first byte and '_' is a separate wildcard") {
    Lexer lx;
    init_lexer(&lx, "a Z alpha Alpha aB Ab x_2 X_2 BOOL Bool _");
    expect(&lx, TOK_LOWER_NAME, "a");
    expect(&lx, TOK_UPPER_NAME, "Z");
    expect(&lx, TOK_LOWER_NAME, "alpha");
    expect(&lx, TOK_UPPER_NAME, "Alpha");
    expect(&lx, TOK_LOWER_NAME, "aB");
    expect(&lx, TOK_UPPER_NAME, "Ab");
    expect(&lx, TOK_LOWER_NAME, "x_2");
    expect(&lx, TOK_UPPER_NAME, "X_2");
    expect(&lx, TOK_UPPER_NAME, "BOOL");
    expect(&lx, TOK_UPPER_NAME, "Bool");
    expect(&lx, TOK_WILDCARD, "_");
    expect(&lx, TOK_EOF, NULL);
    for (unsigned i = 0; i < 26; i++) {
      char lower[] = "aType9_name", upper[] = "Avalue_1";
      lower[0] = (char)('a' + i);
      upper[0] = (char)('A' + i);
      init_lexer(&lx, lower);
      expect(&lx, TOK_LOWER_NAME, lower);
      expect(&lx, TOK_EOF, NULL);
      init_lexer(&lx, upper);
      expect(&lx, TOK_UPPER_NAME, upper);
      expect(&lx, TOK_EOF, NULL);
    }
    ASSERT_EQ_STRN("TOK_UPPER_NAME", token_type_name(TOK_UPPER_NAME),
                   strlen(token_type_name(TOK_UPPER_NAME)));
    ASSERT_EQ_STRN("TOK_LOWER_NAME", token_type_name(TOK_LOWER_NAME),
                   strlen(token_type_name(TOK_LOWER_NAME)));
    ASSERT_EQ_STRN("TOK_WILDCARD", token_type_name(TOK_WILDCARD),
                   strlen(token_type_name(TOK_WILDCARD)));
    for (int t = TOK_EOF; t <= TOK_RBRACKET; t++)
      ASSERT_EQ_INT(t == TOK_UPPER_NAME || t == TOK_LOWER_NAME,
                    token_is_name((TokenType)t));
  }

  TEST("underscore-prefixed names report one complete lexeme and recover") {
    const char *bad[] = {"_name", "__", "__Name", "_0", "_if", "_Sym"};
    const char *message =
        "Names must start with a letter; '_' alone is a wildcard.";
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char text[128];
        snprintf(text, sizeof(text), "\t%s%sReady _ next", bad[i],
                 ending ? "\r\n" : "\n");
        SourceFile *source =
            source_create(&test_arena, "names.brf", text, strlen(text));
        Diagnostics diagnostics;
        diagnostics_init(&diagnostics, &test_arena);
        TokenArray tokens = lexer_tokenize(&test_arena, source, &diagnostics);
        ASSERT_EQ_INT(6, tokens.count);
        ASSERT_EQ_INT(1, diagnostics.count);
        ASSERT_EQ_INT(TOK_ERR, tokens.items[0].type);
        ASSERT_EQ_STRN(bad[i], tokens.items[0].start, tokens.items[0].length);
        ASSERT_EQ_STRN(message, tokens.items[0].message,
                       strlen(tokens.items[0].message));
        ASSERT_EQ_INT(1, tokens.items[0].span.start);
        ASSERT_EQ_INT(1 + strlen(bad[i]), tokens.items[0].span.end);
        ASSERT_EQ_INT(TOK_NEWLINE, tokens.items[1].type);
        ASSERT_EQ_INT(TOK_UPPER_NAME, tokens.items[2].type);
        ASSERT_EQ_INT(TOK_WILDCARD, tokens.items[3].type);
        ASSERT_EQ_INT(TOK_LOWER_NAME, tokens.items[4].type);
        ASSERT_EQ_INT(TOK_EOF, tokens.items[5].type);
        ASSERT_EQ_INT(2, tokens.items[2].line);
        if (diagnostics.count) {
          const Diagnostic *d = diagnostics.items[0];
          ASSERT_EQ_STRN(message, d->message, strlen(d->message));
          ASSERT_EQ_INT(tokens.items[0].span.start, d->span.start);
          ASSERT_EQ_INT(tokens.items[0].span.end, d->span.end);
          ASSERT_TRUE(d->fixes == NULL); // Removing '_' could create a keyword.
        }
        Lexer streamed;
        lexer_init(&streamed, source, NULL);
        for (size_t j = 0; j < tokens.count; j++) {
          Token token = lexer_next_token(&streamed);
          ASSERT_EQ_INT(tokens.items[j].type, token.type);
          ASSERT_EQ_INT(tokens.items[j].span.start, token.span.start);
          ASSERT_EQ_INT(tokens.items[j].span.end, token.span.end);
        }
        init_lexer(&streamed, bad[i]); // The invalid name may end at EOF.
        expect(&streamed, TOK_ERR, bad[i]);
        expect(&streamed, TOK_EOF, NULL);
      }
    }
  }

  TEST("index opener is one adjacent two-byte token with exact spans") {
    const char *text = "rows.(0) 1.(2) 1.5.(3) ..(4) .((5))";
    const TokenType expected[] = {
        TOK_LOWER_NAME, TOK_DOT_LPAREN, TOK_INT_LIT, TOK_RPAREN,
        TOK_INT_LIT,    TOK_DOT_LPAREN, TOK_INT_LIT, TOK_RPAREN,
        TOK_FLOAT_LIT,  TOK_DOT_LPAREN, TOK_INT_LIT, TOK_RPAREN,
        TOK_DOT_DOT,    TOK_LPAREN,     TOK_INT_LIT, TOK_RPAREN,
        TOK_DOT_LPAREN, TOK_LPAREN,     TOK_INT_LIT, TOK_RPAREN,
        TOK_RPAREN,     TOK_EOF};
    SourceFile *source =
        source_create(&test_arena, "indices.brf", text, strlen(text));
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &test_arena);
    TokenArray tokens = lexer_tokenize(&test_arena, source, &diagnostics);
    ASSERT_EQ_INT(0, diagnostics.count);
    ASSERT_EQ_INT(sizeof(expected) / sizeof(*expected), tokens.count);
    Lexer streamed;
    lexer_init(&streamed, source, NULL);
    for (size_t i = 0; i < tokens.count; i++) {
      Token t = tokens.items[i], next = lexer_next_token(&streamed);
      if (i < sizeof(expected) / sizeof(*expected))
        ASSERT_EQ_INT(expected[i], t.type);
      ASSERT_EQ_INT(t.type, next.type);
      ASSERT_EQ_INT(t.span.start, next.span.start);
      ASSERT_EQ_INT(t.span.end, next.span.end);
      ASSERT_TRUE(t.span.file == source);
      ASSERT_TRUE(t.start == source->text + t.span.start);
      ASSERT_EQ_INT(t.length, t.span.end - t.span.start);
      if (t.type == TOK_DOT_LPAREN) {
        ASSERT_EQ_INT(2, t.length);
        ASSERT_EQ_STRN(".(", t.start, t.length);
      }
    }
    ASSERT_EQ_INT(0, strcmp("TOK_DOT_LPAREN", token_type_name(TOK_DOT_LPAREN)));
    const char *gaps[] = {". (", ".\t(", ".\n(", ".\r\n(", ". /* keep */ ("};
    for (size_t i = 0; i < sizeof(gaps) / sizeof(*gaps); i++) {
      Lexer lx;
      init_lexer(&lx, gaps[i]);
      expect(&lx, TOK_PERIOD, ".");
      Token t = lexer_next_token(&lx);
      if (t.type == TOK_NEWLINE)
        t = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_LPAREN, t.type);
      expect(&lx, TOK_EOF, NULL);
    }
    Lexer lx;
    init_lexer(&lx, "\".(\" // .(\n/* .( */ .(");
    expect(&lx, TOK_STR_LIT, "\".(\"");
    expect(&lx, TOK_NEWLINE, NULL);
    expect(&lx, TOK_DOT_LPAREN, ".(");
    expect(&lx, TOK_EOF, NULL);
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
    expect(&lx, TOK_LOWER_NAME, "letx");
    expect(&lx, TOK_LOWER_NAME, "vary");
    expect(&lx, TOK_LOWER_NAME, "constant");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("error-symbol prefix remains separate colon and bang tokens") {
    Lexer lx;
    init_lexer(&lx, ":!bad :bad! : /* keep */ !bad");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_LOWER_NAME, "bad");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_LOWER_NAME, "bad");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_COLON, ":");
    expect(&lx, TOK_BANG, "!");
    expect(&lx, TOK_LOWER_NAME, "bad");
    expect(&lx, TOK_EOF, NULL);
  }
  TEST("Sym is an ordinary name, with case and prefix boundaries") {
    Lexer lx;
    init_lexer(&lx, "Sym sym Symmetry Sym_2");
    expect(&lx, TOK_UPPER_NAME, "Sym");
    expect(&lx, TOK_LOWER_NAME, "sym");
    expect(&lx, TOK_UPPER_NAME, "Symmetry");
    expect(&lx, TOK_UPPER_NAME, "Sym_2");
    expect(&lx, TOK_EOF, NULL);
  }
  TEST("trait and ovr are ordinary names, including case and prefix "
       "boundaries") {
    const char *text = "trait ovr Trait Ovr traits ovride trait_2 ovr2";
    const TokenType types[] = {TOK_LOWER_NAME, TOK_LOWER_NAME, TOK_UPPER_NAME,
                               TOK_UPPER_NAME, TOK_LOWER_NAME, TOK_LOWER_NAME,
                               TOK_LOWER_NAME, TOK_LOWER_NAME};
    SourceFile *source =
        source_create(&test_arena, "names.brf", text, strlen(text));
    Diagnostics d;
    diagnostics_init(&d, &test_arena);
    TokenArray tokens = lexer_tokenize(&test_arena, source, &d);
    ASSERT_EQ_INT(0, d.count);
    ASSERT_EQ_INT(9, tokens.count);
    Lexer stream;
    lexer_init(&stream, source, NULL);
    for (size_t i = 0; i < tokens.count; i++) {
      Token t = lexer_next_token(&stream);
      ASSERT_EQ_INT(tokens.items[i].type, t.type);
      ASSERT_EQ_INT(tokens.items[i].span.start, t.span.start);
      ASSERT_EQ_INT(tokens.items[i].span.end, t.span.end);
      if (i < sizeof(types) / sizeof(*types)) {
        ASSERT_EQ_INT(types[i], t.type);
        ASSERT_TRUE(token_is_name(t.type));
      }
    }
  }
  TEST("self alone is a keyword, not Self or identifier prefixes") {
    Lexer lx;
    init_lexer(&lx, "self Self selfish self_ self1 selfSelf &self");
    expect(&lx, TOK_SELF, "self");
    expect(&lx, TOK_UPPER_NAME, "Self");
    expect(&lx, TOK_LOWER_NAME, "selfish");
    expect(&lx, TOK_LOWER_NAME, "self_");
    expect(&lx, TOK_LOWER_NAME, "self1");
    expect(&lx, TOK_LOWER_NAME, "selfSelf");
    expect(&lx, TOK_BIT_AND, "&");
    expect(&lx, TOK_SELF, "self");
    expect(&lx, TOK_EOF, NULL);
    ASSERT_TRUE(!token_is_name(TOK_SELF) && !token_is_identifier(TOK_SELF));
  }
  TEST("type and definition keywords are distinguished from identifiers") {
    Lexer lx;
    init_lexer(&lx, "bool int uint shrt ushrt lng ulng flt dbl chr str "
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
    expect(&lx, TOK_DBL, "dbl");
    expect(&lx, TOK_CHR, "chr");
    expect(&lx, TOK_STR, "str");
    expect(&lx, TOK_UPPER_NAME, "Sym");
    expect(&lx, TOK_UPPER_NAME, "Self");
    expect(&lx, TOK_LOWER_NAME, "type");
    expect(&lx, TOK_MOD, "mod");
    expect(&lx, TOK_IMPORT, "import");
    expect(&lx, TOK_LOWER_NAME, "trait");
    expect(&lx, TOK_LOWER_NAME, "impl");
    expect(&lx, TOK_PUB, "pub");
    expect(&lx, TOK_LOWER_NAME, "ovr");
    expect(&lx, TOK_LOWER_NAME, "types");
    expect(&lx, TOK_LOWER_NAME, "enum");
    expect(&lx, TOK_LOWER_NAME, "intx");
    expect(&lx, TOK_LOWER_NAME, "sym");
    expect(&lx, TOK_SELF, "self");
    expect(&lx, TOK_LOWER_NAME, "modern");
    expect(&lx, TOK_LOWER_NAME, "imports");
    expect(&lx, TOK_LOWER_NAME, "traits");
    expect(&lx, TOK_LOWER_NAME, "implicit");
    expect(&lx, TOK_LOWER_NAME, "public");
    expect(&lx, TOK_LOWER_NAME, "override");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("dbl is a keyword; dub is an ordinary name") {
    Lexer lx;
    init_lexer(&lx, "dbl dub dblx dubx Dbl Dub dbl_1 dub_1 \"dub\"");
    expect(&lx, TOK_DBL, "dbl");
    expect(&lx, TOK_LOWER_NAME, "dub");
    expect(&lx, TOK_LOWER_NAME, "dblx");
    expect(&lx, TOK_LOWER_NAME, "dubx");
    expect(&lx, TOK_UPPER_NAME, "Dbl");
    expect(&lx, TOK_UPPER_NAME, "Dub");
    expect(&lx, TOK_LOWER_NAME, "dbl_1");
    expect(&lx, TOK_LOWER_NAME, "dub_1");
    expect(&lx, TOK_STR_LIT, "\"dub\"");
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
    expect(&lx, TOK_LOWER_NAME, "iffy");
    expect(&lx, TOK_LOWER_NAME, "elsewhere");
    expect(&lx, TOK_LOWER_NAME, "format");
    expect(&lx, TOK_LOWER_NAME, "inner");
    expect(&lx, TOK_LOWER_NAME, "whiley");
    expect(&lx, TOK_LOWER_NAME, "breaks");
    expect(&lx, TOK_LOWER_NAME, "continued");
    expect(&lx, TOK_LOWER_NAME, "retry");
    expect(&lx, TOK_EOF, NULL);
  }

  arena_destroy(&test_arena);
  RUN_TESTS();
}
