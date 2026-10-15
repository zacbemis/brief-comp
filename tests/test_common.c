#include "common/diagnostic.h"
#include "common/intern.h"
#include "test_utils.h"

#include <stdint.h>

static char *printed_diagnostics(Arena *arena, const Diagnostics *diagnostics) {
  FILE *output = tmpfile();
  if (!output)
    return NULL;
  diagnostics_print(diagnostics, output);
  if (fflush(output) != 0 || fseek(output, 0, SEEK_END) != 0) {
    fclose(output);
    return NULL;
  }
  long length = ftell(output);
  if (length < 0 || fseek(output, 0, SEEK_SET) != 0) {
    fclose(output);
    return NULL;
  }
  char *text = arena_alloc(arena, (size_t)length + 1);
  size_t count = fread(text, 1, (size_t)length, output);
  text[count] = '\0';
  fclose(output);
  return text;
}

int main(void) {
  Arena arena;
  arena_init(&arena);

  TEST("arena alignment and stable allocations across blocks") {
    char *first = arena_strdup(&arena, "kept");
    for (size_t i = 0; i < 1000; i++) {
      void *allocation = arena_alloc(&arena, i);
      ASSERT_EQ_INT(0, (uintptr_t)allocation % _Alignof(max_align_t));
      if (i)
        memset(allocation, 0xa5, i);
    }
    ASSERT_EQ_STRN("kept", first, strlen(first));
    char *large = arena_alloc(&arena, 20000);
    memset(large, 'x', 20000);
    ASSERT_EQ_INT('x', large[19999]);
    ASSERT_EQ_STRN("kept", first, strlen(first));
  }

  TEST("arena calloc and length-delimited strings") {
    int *numbers = arena_calloc(&arena, 100, sizeof(*numbers));
    for (size_t i = 0; i < 100; i++)
      ASSERT_EQ_INT(0, numbers[i]);
    const char bytes[] = {'a', '\0', 'b'};
    char *copy = arena_strndup(&arena, bytes, sizeof(bytes));
    ASSERT_TRUE(memcmp(bytes, copy, sizeof(bytes)) == 0);
    ASSERT_EQ_INT(0, copy[3]);
    ASSERT_TRUE(arena_alloc(&arena, 0) != NULL);
    ASSERT_EQ_STRN("", arena_strndup(&arena, "unused", 0), 0);
  }

  TEST("arena can be destroyed twice and reused") {
    Arena temporary;
    arena_init(&temporary);
    arena_alloc(&temporary, 1);
    arena_destroy(&temporary);
    ASSERT_TRUE(temporary.blocks == NULL);
    arena_destroy(&temporary);
    char *text = arena_strdup(&temporary, "again");
    ASSERT_EQ_STRN("again", text, strlen(text));
    arena_destroy(&temporary);
  }

  TEST("source text and path are owned and offsets are indexed") {
    char path[] = "sample.brf";
    char text[] = "abc\r\ndef\n";
    SourceFile *source = source_create(&arena, path, text, strlen(text));
    path[0] = 'X';
    text[0] = 'X';
    ASSERT_EQ_STRN("sample.brf", source->path, strlen(source->path));
    ASSERT_EQ_STRN("abc\r\ndef\n", source->text, source->length);
    ASSERT_EQ_INT(3, source->line_count);
    ASSERT_EQ_INT(0, source->line_starts[0]);
    ASSERT_EQ_INT(5, source->line_starts[1]);
    ASSERT_EQ_INT(9, source->line_starts[2]);
    SourcePosition position = source_position(source, 4); // LF on first line.
    ASSERT_EQ_INT(1, position.line);
    ASSERT_EQ_INT(5, position.column);
    position = source_position(source, 7);
    ASSERT_EQ_INT(2, position.line);
    ASSERT_EQ_INT(3, position.column);
    position = source_position(source, 100);
    ASSERT_EQ_INT(3, position.line);
    ASSERT_EQ_INT(1, position.column);
    size_t length;
    const char *line = source_line(source, 1, &length);
    ASSERT_EQ_STRN("abc", line, length);
    line = source_line(source, 2, &length);
    ASSERT_EQ_STRN("def", line, length);
    line = source_line(source, 3, &length);
    ASSERT_EQ_STRN("", line, length);
    ASSERT_TRUE(source_line(source, 0, &length) == NULL);
    ASSERT_TRUE(source_line(source, 4, &length) == NULL);
    Span span = span_make(source, 100, 0);
    ASSERT_EQ_INT(9, span.start);
    ASSERT_EQ_INT(9, span.end);
    ASSERT_TRUE(span.file == source);
    ASSERT_TRUE(span_make(NULL, 1, 2).file == NULL);
  }

  TEST("empty source and embedded zero bytes") {
    SourceFile *empty = source_create(&arena, "empty.brf", "", 0);
    ASSERT_EQ_INT(1, empty->line_count);
    ASSERT_EQ_INT(1, source_position(empty, 0).line);
    ASSERT_EQ_INT(1, source_position(empty, 0).column);
    const char bytes[] = {'a', '\0', '\n', 'b'};
    SourceFile *binary =
        source_create(&arena, "binary.brf", bytes, sizeof(bytes));
    ASSERT_EQ_INT(4, binary->length);
    ASSERT_EQ_INT(2, binary->line_count);
    ASSERT_EQ_INT(0, binary->text[4]);
    ASSERT_EQ_INT(2, source_position(binary, 3).line);
  }

  TEST("source loading reads beyond one buffer and handles I/O errors") {
    const char *path = "brief-test-source.tmp";
    FILE *output = fopen(path, "wb");
    ASSERT_TRUE(output != NULL);
    if (output) {
      for (size_t i = 0; i < 10000; i++)
        fputc(i == 5000 ? '\0' : 'a', output);
      ASSERT_EQ_INT(0, fclose(output));
      const char *error = "not reset";
      SourceFile *source = source_load(&arena, path, &error);
      ASSERT_TRUE(source != NULL);
      ASSERT_TRUE(error == NULL);
      if (source) {
        ASSERT_EQ_INT(10000, source->length);
        ASSERT_EQ_INT(0, source->text[5000]);
        ASSERT_EQ_INT('a', source->text[9999]);
      }
      ASSERT_EQ_INT(0, remove(path));
      ASSERT_TRUE(source_load(&arena, path, &error) == NULL);
      ASSERT_TRUE(error != NULL && error[0] != '\0');
      ASSERT_TRUE(source_load(&arena, path, NULL) == NULL);
    }
  }

  TEST("source loading accepts an empty file") {
    const char *path = "brief-test-empty.tmp";
    FILE *output = fopen(path, "wb");
    ASSERT_TRUE(output != NULL);
    if (output) {
      ASSERT_EQ_INT(0, fclose(output));
      const char *error;
      SourceFile *source = source_load(&arena, path, &error);
      ASSERT_TRUE(source != NULL && error == NULL);
      if (source) {
        ASSERT_EQ_INT(0, source->length);
        ASSERT_EQ_INT(1, source->line_count);
      }
      ASSERT_EQ_INT(0, remove(path));
    }
  }

  TEST("interning compares contents, owns slices, and keeps pointers stable") {
    InternTable table;
    intern_init(&table, &arena);
    char name[] = "UserSuffix";
    const char *user = intern_n(&table, name, 4);
    name[0] = 'X';
    ASSERT_TRUE(user == intern(&table, "User"));
    ASSERT_TRUE(user != intern(&table, "user"));
    ASSERT_TRUE(intern(&table, "") == intern_n(&table, "anything", 0));
    const char *saved[2000];
    for (size_t i = 0; i < 2000; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "name%zu", i);
      saved[i] = intern(&table, buffer);
    }
    for (size_t i = 0; i < 2000; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "name%zu", i);
      ASSERT_TRUE(saved[i] == intern(&table, buffer));
    }
    ASSERT_TRUE(user == intern(&table, "User"));
    ASSERT_EQ_INT(2003, table.count);
  }

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

  TEST("diagnostic UTF-8 widths and tabs use display columns, not byte columns") {
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
         "error: test\n  --> utf8.brf:1:7\n  |\n1 | café    @\n  |         ^\n"},
        {"é\t@\n", 3, 4,
         "error: test\n  --> utf8.brf:1:4\n  |\n1 | é   @\n  |     ^\n"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      Diagnostics diagnostics;
      diagnostics_init(&diagnostics, &arena);
      SourceFile *source = source_create(&arena, "utf8.brf", cases[i].text,
                                         strlen(cases[i].text));
      diagnostics_error(&diagnostics,
                        span_make(source, cases[i].start, cases[i].end), "test");
      char *printed = printed_diagnostics(&arena, &diagnostics);
      ASSERT_TRUE(printed != NULL);
      if (printed)
        ASSERT_EQ_STRN(cases[i].expected, printed, strlen(printed));
    }
  }

  arena_destroy(&arena);
  RUN_TESTS();
}
