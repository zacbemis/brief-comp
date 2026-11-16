#include "lexer_support.h"

int main(void) {
  arena_init(&test_arena);
  TEST("line comments are skipped and do not consume newline") {
    Lexer lx;
    init_lexer(&lx, "let x = 5 // trailing comment\nvar");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_LOWER_NAME, "x");
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
    ASSERT_EQ_INT(TOK_LOWER_NAME, id.type);
    ASSERT_EQ_STRN("y", id.start, id.length);
    ASSERT_EQ_INT(2, id.line);
  }

  TEST("newlines emit TOK_NEWLINE and bump line counter") {
    Lexer lx;
    init_lexer(&lx, "x\ny");
    Token a = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_LOWER_NAME, a.type);
    ASSERT_EQ_INT(1, a.line);
    Token nl = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_NEWLINE, nl.type);
    Token b = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_LOWER_NAME, b.type);
    ASSERT_EQ_INT(2, b.line);
  }

  TEST("unexpected character produces an error token") {
    Lexer lx;
    init_lexer(&lx, "@");
    Token t = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, t.type);
  }

  TEST("a multi-byte character outside a literal is one error token") {
    Lexer lx;
    init_lexer(&lx, "let \xe6\x97\xa5 = \xc3\xa9\x80");
    expect(&lx, TOK_LET, "let");
    expect(&lx, TOK_ERR, "\xe6\x97\xa5");
    expect(&lx, TOK_EQUAL, "=");
    expect(&lx, TOK_ERR, "\xc3\xa9");
    expect(&lx, TOK_ERR, "\x80");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("unterminated comments report the opening location only once") {
    const char *text = "x\n  /* comment\nmore";
    SourceFile *source =
        source_create(&test_arena, "comment.brf", text, strlen(text));
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &test_arena);
    Lexer lx;
    lexer_init(&lx, source, &diagnostics);
    expect(&lx, TOK_LOWER_NAME, "x");
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
    expect(&lx, TOK_LOWER_NAME, "x");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LOWER_NAME, "y");
    expect(&lx, TOK_EOF, NULL);
  }

  TEST("token spans track CRLF lines and escaped newlines") {
    Lexer lx;
    init_lexer(&lx, " \tfoo\r\n  \"a\\nb\"\n  @");
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
    ASSERT_EQ_INT(15, string_token.span.end);
    ASSERT_EQ_INT(2, string_token.line);
    expect(&lx, TOK_NEWLINE, "\n");
    Token error = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_ERR, error.type);
    ASSERT_EQ_STRN("@", error.start, error.length);
    ASSERT_EQ_INT(18, error.span.start);
    ASSERT_EQ_INT(19, error.span.end);
    ASSERT_EQ_INT(3, error.line);
    ASSERT_EQ_INT(3, source_position(lx.source, error.span.start).column);
    Token eof = lexer_next_token(&lx);
    ASSERT_EQ_INT(TOK_EOF, eof.type);
    ASSERT_EQ_INT(19, eof.span.start);
    ASSERT_EQ_INT(19, eof.span.end);
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
    ASSERT_EQ_INT(TOK_LOWER_NAME, tokens.items[0].type);
    ASSERT_EQ_INT(TOK_ERR, tokens.items[1].type);
    ASSERT_EQ_INT(TOK_LOWER_NAME, tokens.items[2].type);
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
      text[i] = "xX_"[(i / 2) % 3];
      text[i + 1] = '\n';
    }
    SourceFile *source =
        source_create(&test_arena, "many.brf", text, sizeof(text));
    TokenArray tokens = lexer_tokenize(&test_arena, source, NULL);
    ASSERT_EQ_INT(601, tokens.count);
    for (size_t i = 0; i < 600; i++) {
      const TokenType kinds[] = {TOK_LOWER_NAME, TOK_UPPER_NAME, TOK_WILDCARD};
      ASSERT_EQ_INT(i % 2 ? TOK_NEWLINE : kinds[(i / 2) % 3],
                    tokens.items[i].type);
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
      SourceFile *source =
          source_create(&arena, "bytes.brf", text, sizeof(text));
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
