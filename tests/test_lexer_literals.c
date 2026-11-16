#include "lexer_support.h"

int main(void) {
  arena_init(&test_arena);
  TEST("identifiers and boolean literals") {
    Lexer lx;
    init_lexer(&lx, "true false trueish");
    expect(&lx, TOK_BOOL_LIT, "true");
    expect(&lx, TOK_BOOL_LIT, "false");
    expect(&lx, TOK_LOWER_NAME, "trueish");
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

  TEST("source exponents are source-backed errors without consuming newlines") {
    const char *spellings[] = {"1e2",
                               "1E2",
                               "1e+2",
                               "1E-2",
                               "0e0",
                               "0.0e0",
                               "1.25e10",
                               "1.25E+10",
                               "1.25E-10",
                               "01e002",
                               "1e99999999999999999999",
                               "1e",
                               "1E",
                               "1e+",
                               "1E-",
                               "0.5E+",
                               "0.5e"};
    const char *endings[] = {"\n", "\r\n"};
    const char *message =
        "Exponent notation is not supported in source number literals.";
    for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++) {
      for (unsigned negative = 0; negative < 2; negative++) {
        for (size_t j = 0; j < sizeof(endings) / sizeof(endings[0]); j++) {
          char text[256];
          snprintf(text, sizeof(text), "%s%s%skept", negative ? "-" : "",
                   spellings[i], endings[j]);
          SourceFile *source =
              source_create(&test_arena, "exponents.brf", text, strlen(text));
          Diagnostics d;
          diagnostics_init(&d, &test_arena);
          Lexer lx;
          lexer_init(&lx, source, &d);
          if (negative)
            expect(&lx, TOK_MINUS, "-");
          Token t = lexer_next_token(&lx);
          ASSERT_EQ_INT(TOK_ERR, t.type);
          ASSERT_EQ_INT(negative, t.span.start);
          ASSERT_EQ_INT(negative + strlen(spellings[i]), t.span.end);
          ASSERT_TRUE(t.start == source->text + negative);
          ASSERT_EQ_STRN(spellings[i], t.start, t.length);
          ASSERT_TRUE(t.message != NULL);
          if (t.message)
            ASSERT_EQ_STRN(message, t.message, strlen(t.message));
          ASSERT_EQ_INT(1, d.count);
          if (d.count) {
            ASSERT_EQ_STRN(message, d.items[0]->message,
                           strlen(d.items[0]->message));
            ASSERT_EQ_INT(t.span.start, d.items[0]->span.start);
            ASSERT_EQ_INT(t.span.end, d.items[0]->span.end);
          }
          expect(&lx, TOK_NEWLINE, "\n");
          Token kept = lexer_next_token(&lx);
          ASSERT_EQ_INT(TOK_LOWER_NAME, kept.type);
          ASSERT_EQ_STRN("kept", kept.start, kept.length);
          ASSERT_EQ_INT(2, source_position(source, kept.span.start).line);
          ASSERT_EQ_INT(1, source_position(source, kept.span.start).column);
          expect(&lx, TOK_EOF, "");
          Diagnostics bulk_errors;
          diagnostics_init(&bulk_errors, &test_arena);
          TokenArray bulk = lexer_tokenize(&test_arena, source, &bulk_errors);
          ASSERT_EQ_INT(1, bulk_errors.count);
          ASSERT_EQ_INT(4 + negative, bulk.count);
          ASSERT_EQ_INT(TOK_ERR, bulk.items[negative].type);
          ASSERT_EQ_INT(t.span.start, bulk.items[negative].span.start);
          ASSERT_EQ_INT(t.span.end, bulk.items[negative].span.end);
        }
      }
    }
  }
  TEST("exponent errors preserve identifier-like suffixes, delimiters and "
       "text") {
    Lexer lx;
    init_lexer(&lx, "1else 1equal 1e2name 1E_2 1e+name 1.0e2name e2 E2");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_ELSE, "else");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_LOWER_NAME, "equal");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_LOWER_NAME, "e2name");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_UPPER_NAME, "E_2");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_LOWER_NAME, "e");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_LOWER_NAME, "name");
    expect(&lx, TOK_FLOAT_LIT, "1.0");
    expect(&lx, TOK_LOWER_NAME, "e2name");
    expect(&lx, TOK_LOWER_NAME, "e2");
    expect(&lx, TOK_UPPER_NAME, "E2");
    expect(&lx, TOK_EOF, "");
    init_lexer(&lx, "1e2+3 1E-2..4 0.5e+2.foo(1e2)");
    expect(&lx, TOK_ERR, "1e2");
    expect(&lx, TOK_PLUS, "+");
    expect(&lx, TOK_INT_LIT, "3");
    expect(&lx, TOK_ERR, "1E-2");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_INT_LIT, "4");
    expect(&lx, TOK_ERR, "0.5e+2");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LOWER_NAME, "foo");
    expect(&lx, TOK_LPAREN, "(");
    expect(&lx, TOK_ERR, "1e2");
    expect(&lx, TOK_RPAREN, ")");
    expect(&lx, TOK_EOF, "");
    init_lexer(&lx, "\"1e2\" \"1E-2\" 'e' 'E' 1.25 1..2 1.e2");
    expect(&lx, TOK_STR_LIT, "\"1e2\"");
    expect(&lx, TOK_STR_LIT, "\"1E-2\"");
    expect(&lx, TOK_CHAR_LIT, "'e'");
    expect(&lx, TOK_CHAR_LIT, "'E'");
    expect(&lx, TOK_FLOAT_LIT, "1.25");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_DOT_DOT, "..");
    expect(&lx, TOK_INT_LIT, "2");
    expect(&lx, TOK_INT_LIT, "1");
    expect(&lx, TOK_PERIOD, ".");
    expect(&lx, TOK_LOWER_NAME, "e2");
    expect(&lx, TOK_EOF, "");
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
      expect(&lx, TOK_LOWER_NAME, "x");
      expect(&lx, TOK_EOF, NULL);
      text[0] = '\'';
      text[3] = '\'';
      init_lexer(&lx, text);
      token = lexer_next_token(&lx);
      ASSERT_EQ_INT(TOK_ERR, token.type);
      ASSERT_EQ_STRN("Unknown escape in character literal.", token.message,
                     strlen(token.message));
      expect(&lx, TOK_LOWER_NAME, "x");
      expect(&lx, TOK_EOF, NULL);
    }
  }

  TEST("unknown escapes highlight the first escape but keep the whole token") {
    const char *cases[] = {"\"café \\q\\r\" next", "'\\q' next",
                           "\"ok\\n café \\q\" next"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      SourceFile *source =
          source_create(&test_arena, "escape.brf", cases[i], strlen(cases[i]));
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
      expect(&lx, TOK_LOWER_NAME, "next");
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

  TEST("unterminated strings stop at LF and CRLF and report later errors") {
    const char *prefixes[] = {"\"", "\"abc", "\"abc\\", "\"bad\\q"};
    const char *newlines[] = {"\n", "\r\n"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
      for (size_t j = 0; j < sizeof(newlines) / sizeof(newlines[0]); j++) {
        char text[64];
        snprintf(text, sizeof(text), "%s%s@ \"ok\"\n", prefixes[i],
                 newlines[j]);
        SourceFile *source =
            source_create(&test_arena, "recovery.brf", text, strlen(text));
        Diagnostics diagnostics;
        diagnostics_init(&diagnostics, &test_arena);
        Lexer lx;
        lexer_init(&lx, source, &diagnostics);
        Token string_error = lexer_next_token(&lx);
        ASSERT_EQ_INT(TOK_ERR, string_error.type);
        ASSERT_EQ_STRN(prefixes[i], string_error.start, string_error.length);
        ASSERT_EQ_STRN("Unterminated string.", string_error.message,
                       strlen(string_error.message));
        ASSERT_EQ_INT(0, string_error.span.start);
        ASSERT_EQ_INT(strlen(prefixes[i]), string_error.span.end);
        expect(&lx, TOK_NEWLINE, "\n");
        Token later_error = lexer_next_token(&lx);
        ASSERT_EQ_INT(TOK_ERR, later_error.type);
        ASSERT_EQ_STRN("@", later_error.start, later_error.length);
        ASSERT_EQ_INT(2, later_error.line);
        ASSERT_EQ_INT(1,
                      source_position(source, later_error.span.start).column);
        expect(&lx, TOK_STR_LIT, "\"ok\"");
        expect(&lx, TOK_NEWLINE, "\n");
        expect(&lx, TOK_EOF, NULL);
        ASSERT_EQ_INT(2, diagnostics.count);
        Diagnostics batch_diagnostics;
        diagnostics_init(&batch_diagnostics, &test_arena);
        TokenArray tokens =
            lexer_tokenize(&test_arena, source, &batch_diagnostics);
        ASSERT_EQ_INT(6, tokens.count);
        ASSERT_EQ_INT(2, batch_diagnostics.count);
        ASSERT_EQ_INT(TOK_STR_LIT, tokens.items[3].type);
        ASSERT_EQ_INT(TOK_EOF, tokens.items[tokens.count - 1].type);
      }
    }
  }

  TEST("unterminated characters leave the newline available for recovery") {
    Lexer lx;
    init_lexer(&lx, "'a\nx");
    expect(&lx, TOK_ERR, "'a");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LOWER_NAME, "x");
    init_lexer(&lx, "'\\\nx");
    expect(&lx, TOK_ERR, "'\\");
    expect(&lx, TOK_NEWLINE, "\n");
    expect(&lx, TOK_LOWER_NAME, "x");
  }

  arena_destroy(&test_arena);
  RUN_TESTS();
}
