#include "common_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);

  TEST(
      "diagnostics print exact locations, source, markers, and ordered notes") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    SourceFile *source =
        source_create(&arena, "test.brf", "let x = 1\n  @ bad\n", 18);
    char message[] = "Unexpected character.";
    Diagnostic *error =
        diagnostics_error(&diagnostics, span_make(source, 12, 13), message);
    message[0] = 'X';
    diagnostic_note(&diagnostics, error, span_make(NULL, 0, 0), "remove it");
    diagnostic_note(&diagnostics, error, span_make(source, 4, 5),
                    "declared here");
    char *printed = printed_diagnostics(&arena, &diagnostics);
    ASSERT_TRUE(printed != NULL);
    const char *expected = "error: Unexpected character.\n"
                           "  --> test.brf:2:3\n"
                           "  |\n"
                           "2 |   @ bad\n"
                           "  |   ^\n"
                           "note: remove it\n"
                           "note: declared here\n"
                           "  --> test.brf:1:5\n"
                           "  |\n"
                           "1 | let x = 1\n"
                           "  |     ^\n";
    if (printed)
      ASSERT_EQ_STRN(expected, printed, strlen(printed));
  }

  TEST(
      "diagnostic list growth does not invalidate errors or note attachments") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    Diagnostic *first =
        diagnostics_error(&diagnostics, span_make(NULL, 0, 0), "first");
    for (size_t i = 0; i < 100; i++)
      diagnostics_error(&diagnostics, span_make(NULL, 0, 0), "another");
    diagnostic_note(&diagnostics, first, span_make(NULL, 0, 0), "still here");
    ASSERT_TRUE(diagnostics.items[0] == first);
    ASSERT_EQ_INT(101, diagnostics.count);
    ASSERT_EQ_STRN("still here", first->notes->message,
                   strlen(first->notes->message));
  }

  TEST("diagnostic tabs, EOF, multiline spans, and multidigit line gutters") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    const char *text = "\n\n\n\n\n\n\n\n\n\tbad\n";
    SourceFile *source = source_create(&arena, "tabs.brf", text, strlen(text));
    diagnostics_error(&diagnostics, span_make(source, 10, source->length),
                      "bad");
    diagnostics_error(&diagnostics,
                      span_make(source, source->length, source->length),
                      "at EOF");
    char *printed = printed_diagnostics(&arena, &diagnostics);
    ASSERT_TRUE(printed != NULL);
    if (printed) {
      ASSERT_TRUE(strstr(printed, "tabs.brf:10:2\n") != NULL);
      ASSERT_TRUE(strstr(printed, "10 |     bad\n   |     ^~~\n") != NULL);
      ASSERT_TRUE(strstr(printed, "tabs.brf:11:1\n") != NULL);
      ASSERT_TRUE(strstr(printed, "11 | \n   | ^\n") != NULL);
    }
  }

  TEST("diagnostic fixes own replacements and print after ordered notes") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    const char *text = "if !ready {\n";
    SourceFile *source = source_create(&arena, "fix.brf", text, strlen(text));
    Span span = span_make(source, 3, 4);
    Diagnostic *error = diagnostics_error(&diagnostics, span, "Use not.");
    diagnostic_note(&diagnostics, error, span_make(NULL, 0, 0), "a note");
    char replacement[] = "not ";
    ASSERT_EQ_INT(1, diagnostic_fix(&diagnostics, error, span, replacement));
    replacement[0] = 'X';
    ASSERT_TRUE(error->fixes == error->last_fix);
    ASSERT_TRUE(error->fixes->next == NULL);
    ASSERT_TRUE(error->fixes->span.file == source);
    ASSERT_EQ_INT(3, error->fixes->span.start);
    ASSERT_EQ_INT(4, error->fixes->span.end);
    ASSERT_EQ_STRN("not ", error->fixes->replacement,
                   strlen(error->fixes->replacement));
    char *printed = printed_diagnostics(&arena, &diagnostics);
    ASSERT_TRUE(printed != NULL);
    if (printed)
      ASSERT_EQ_STRN("error: Use not.\n"
                     "  --> fix.brf:1:4\n"
                     "  |\n"
                     "1 | if !ready {\n"
                     "  |    ^\n"
                     "note: a note\n"
                     "help: write `if not ready {`\n",
                     printed, strlen(printed));
  }

  TEST("fix previews support insertion, deletion, EOF and multi-line edits") {
    const struct {
      const char *text;
      size_t start, end;
      const char *replacement, *help;
    } cases[] = {
        {"abc", 1, 2, "XY", "help: write `aXYc`\n"},
        {"abc", 0, 0, "!", "help: write `!abc`\n"},
        {"abc", 3, 3, "!", "help: write `abc!`\n"},
        {"", 0, 0, "new", "help: write `new`\n"},
        {"abc", 1, 2, "", "help: write `ac`\n"},
        {"abc", 0, 3, "", "help: write ``\n"},
        {"first\r\nsecond\r\n", 2, 9, "X", "help: write `fiXcond`\n"},
        {"first\nsecond", 0, 5, "one\ntwo", "help: write `one\\ntwo`\n"},
        {"\tcafé && ok\r\n", 7, 9, "and", "help: write `\\tcafé and ok`\n"},
        {"abc\r\n", 4, 4, "X", "help: write `abc\\rX`\n"},
        {"abc\n", 4, 4, "X", "help: write `X`\n"},
        {"x", 0, 1, "\n\r\t\033\177`",
         "help: write `\\n\\r\\t\\x1b\\x7f\\``\n"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &arena);
      SourceFile *source = source_create(&arena, "fix.brf", cases[i].text,
                                         strlen(cases[i].text));
      Span span = span_make(source, cases[i].start, cases[i].end);
      Diagnostic *error = diagnostics_error(&diagnostics, span, "test");
      ASSERT_EQ_INT(
          1, diagnostic_fix(&diagnostics, error, span, cases[i].replacement));
      char *printed = printed_diagnostics(&arena, &diagnostics);
      ASSERT_TRUE(printed != NULL);
      if (printed) {
        char *help = strstr(printed, "help:");
        ASSERT_TRUE(help != NULL);
        if (help)
          ASSERT_EQ_STRN(cases[i].help, help, strlen(help));
      }
    }
  }

  TEST("fixes on one line preview together; others keep their own lines") {
    const struct {
      const char *text;
      size_t spans[3][2];
      const char *replacements[3];
      const char *help;
    } cases[] = {
        // Both brackets of an index, written in either order.
        {"a[i]", {{1, 2}, {3, 4}}, {".(", ")"}, "help: write `a.(i)`\n"},
        {"a[i]", {{3, 4}, {1, 2}}, {")", ".("}, "help: write `a.(i)`\n"},
        // Nested edits stay disjoint, and insertions at one offset keep their
        // written order.
        {"a[b[0]]",
         {{1, 2}, {3, 4}, {5, 6}},
         {".(", ".(", ")"},
         "help: write `a.(b.(0)]`\n"},
        {"ab", {{1, 1}, {1, 1}}, {"x", "y"}, "help: write `axyb`\n"},
        // Overlapping fixes cannot be combined, so each previews alone.
        {"abc",
         {{0, 2}, {1, 3}},
         {"X", "Y"},
         "help: write `Xc`\nhelp: write `aY`\n"},
        // A multi-line fix previews alone; the single-line one still groups.
        {"ab\ncd",
         {{1, 4}, {0, 0}},
         {"-", "^"},
         "help: write `a-d`\nhelp: write `^ab`\n"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &arena);
      SourceFile *source = source_create(&arena, "fix.brf", cases[i].text,
                                         strlen(cases[i].text));
      Diagnostic *error =
          diagnostics_error(&diagnostics, span_make(source, 0, 0), "test");
      for (size_t j = 0; j < 3 && cases[i].replacements[j]; j++)
        ASSERT_EQ_INT(1, diagnostic_fix(&diagnostics, error,
                                        span_make(source, cases[i].spans[j][0],
                                                  cases[i].spans[j][1]),
                                        cases[i].replacements[j]));
      char *printed = printed_diagnostics(&arena, &diagnostics);
      ASSERT_TRUE(printed != NULL);
      if (printed) {
        char *help = strstr(printed, "help:");
        ASSERT_TRUE(help != NULL);
        if (help)
          ASSERT_EQ_STRN(cases[i].help, help, strlen(help));
      }
    }
  }

  TEST("fix lists remain stable across growth and identify other locations") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    SourceFile *a = source_create(&arena, "a.brf", "old\nold", 7);
    SourceFile *b = source_create(&arena, "b.brf", "old", 3);
    Diagnostic *error =
        diagnostics_error(&diagnostics, span_make(a, 0, 3), "test");
    ASSERT_EQ_INT(
        1, diagnostic_fix(&diagnostics, error, span_make(a, 4, 7), "new"));
    DiagnosticFix *first = error->fixes;
    for (size_t i = 0; i < 100; i++)
      diagnostics_error(&diagnostics, span_make(NULL, 0, 0), "another");
    ASSERT_EQ_INT(
        1, diagnostic_fix(&diagnostics, error, span_make(b, 0, 3), "other"));
    ASSERT_TRUE(error == diagnostics.items[0]);
    ASSERT_TRUE(first == error->fixes);
    ASSERT_TRUE(first->next == error->last_fix);
    ASSERT_TRUE(error->last_fix->next == NULL);
    ASSERT_TRUE(first->span.file == a);
    ASSERT_TRUE(first->next->span.file == b);
    Diagnostic *last = diagnostics.items[100];
    ASSERT_EQ_INT(1,
                  diagnostic_fix(&diagnostics, last, span_make(b, 3, 3), "!"));
    char *printed = printed_diagnostics(&arena, &diagnostics);
    ASSERT_TRUE(printed != NULL);
    if (printed) {
      ASSERT_TRUE(strstr(printed, "help: a.brf:2:1: write `new`\n"
                                  "help: b.brf:1:1: write `other`\n") != NULL);
      ASSERT_TRUE(strstr(printed, "help: b.brf:1:4: write `old!`\n") != NULL);
    }
  }

  TEST("the newest diagnostic moves to an earlier index in order") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    const char *names[] = {"a", "b", "c", "late"};
    for (size_t i = 0; i < 4; i++)
      diagnostics_error(&diagnostics, span_make(NULL, 0, 0), names[i]);
    Diagnostic *late = diagnostics.items[3];
    diagnostics_move_last(&diagnostics, 1);
    const char *moved[] = {"a", "late", "b", "c"};
    ASSERT_EQ_INT(4, diagnostics.count);
    for (size_t i = 0; i < 4; i++)
      ASSERT_EQ_INT(0, strcmp(moved[i], diagnostics.items[i]->message));
    ASSERT_TRUE(late == diagnostics.items[1]);
    // The last index, and indices past the end, leave the list unchanged.
    diagnostics_move_last(&diagnostics, 3);
    diagnostics_move_last(&diagnostics, 4);
    for (size_t i = 0; i < 4; i++)
      ASSERT_EQ_INT(0, strcmp(moved[i], diagnostics.items[i]->message));
    diagnostics_move_last(&diagnostics, 0);
    ASSERT_EQ_INT(0, strcmp("c", diagnostics.items[0]->message));
    ASSERT_EQ_INT(0, strcmp("b", diagnostics.items[3]->message));
  }

  TEST("fixes reject invalid source spans instead of changing their offsets") {
    Diagnostics diagnostics;
    diagnostics_init(&diagnostics, &arena);
    SourceFile *source = source_create(&arena, "fix.brf", "x", 1);
    Diagnostic *error =
        diagnostics_error(&diagnostics, span_make(NULL, 0, 0), "test");
    const Span invalid[] = {{NULL, 0, 0},
                            {source, 1, 0},
                            {source, 0, 2},
                            {source, 2, 2},
                            {source, 0, SIZE_MAX}};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      ASSERT_EQ_INT(0, diagnostic_fix(&diagnostics, error, invalid[i], "new"));
      ASSERT_TRUE(error->fixes == NULL && error->last_fix == NULL);
    }
    ASSERT_EQ_INT(1, diagnostics.count);
    const char bytes[] = {'a', '\0', 'b'};
    SourceFile *binary =
        source_create(&arena, "binary.brf", bytes, sizeof(bytes));
    ASSERT_EQ_INT(
        1, diagnostic_fix(&diagnostics, error, span_make(binary, 2, 3), "c"));
    char *printed = printed_diagnostics(&arena, &diagnostics);
    ASSERT_TRUE(printed != NULL);
    if (printed)
      ASSERT_EQ_STRN("error: test\nhelp: binary.brf:1:3: write `a\\x00c`\n",
                     printed, strlen(printed));
  }

  TEST("diagnostic UTF-8 widths and tabs use display columns, not byte "
       "columns") {
    const struct {
      const char *text;
      size_t start;
      size_t end;
      const char *expected;
    } cases[] = {
        {"café\n", 0, 5,
         "error: test\n  --> utf8.brf:1:1\n  |\n1 | café\n  | ^~~~\n"},
        {"café @\n", 6, 7,
         "error: test\n  --> utf8.brf:1:7\n  |\n1 | café @\n  |      ^\n"},
        {"café\t@\n", 6, 7,
         "error: test\n  --> utf8.brf:1:7\n  |\n1 | café    @\n  |         "
         "^\n"},
        {"é\t@\n", 3, 4,
         "error: test\n  --> utf8.brf:1:4\n  |\n1 | é   @\n  |     ^\n"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &arena);
      SourceFile *source = source_create(&arena, "utf8.brf", cases[i].text,
                                         strlen(cases[i].text));
      diagnostics_error(&diagnostics,
                        span_make(source, cases[i].start, cases[i].end),
                        "test");
      char *printed = printed_diagnostics(&arena, &diagnostics);
      ASSERT_TRUE(printed != NULL);
      if (printed)
        ASSERT_EQ_STRN(cases[i].expected, printed, strlen(printed));
    }
  }

  arena_destroy(&arena);
  RUN_TESTS();
}
