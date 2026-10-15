#include "common/diagnostic.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"

#include <stdio.h>
#include <string.h>

static void print_lexeme(const Token *token) {
  fputc('"', stdout);
  for (size_t i = 0; i < token->length; i++) {
    unsigned char c = (unsigned char)token->start[i];
    switch (c) {
    case '\n':
      fputs("\\n", stdout);
      break;
    case '\r':
      fputs("\\r", stdout);
      break;
    case '\t':
      fputs("\\t", stdout);
      break;
    case '\\':
      fputs("\\\\", stdout);
      break;
    case '"':
      fputs("\\\"", stdout);
      break;
    default:
      if (c < 32 || c == 127)
        printf("\\x%02x", (unsigned int)c);
      else
        fputc(c, stdout);
      break;
    }
  }
  fputc('"', stdout);
}

int main(int argc, char **argv) {
  if (argc != 3 || (strcmp(argv[1], "dump-tokens") != 0 &&
                    strcmp(argv[1], "dump-ast") != 0)) {
    fputs("usage: brief dump-tokens file.brf\n"
          "       brief dump-ast file.brf\n"
          "Compilation is not implemented yet.\n",
          stderr);
    return 2;
  }

  Arena arena;
  arena_init(&arena);
  Diagnostics diagnostics;
  diagnostics_init(&diagnostics, &arena);
  const char *load_error;
  SourceFile *source = source_load(&arena, argv[2], &load_error);
  if (!source) {
    fprintf(stderr, "error: cannot read %s: %s\n", argv[2], load_error);
    arena_destroy(&arena);
    return 1;
  }

  TokenArray tokens = lexer_tokenize(&arena, source, &diagnostics);
  if (strcmp(argv[1], "dump-ast") == 0) {
    InternTable names;
    intern_init(&names, &arena);
    TokenArray filtered = newline_filter(&arena, tokens);
    Program *program = parse_program(&arena, &names, filtered, &diagnostics);
    if (diagnostics.count) {
      diagnostics_print(&diagnostics, stderr);
      arena_destroy(&arena);
      return 1;
    }
    ast_print(stdout, program);
    arena_destroy(&arena);
    return 0;
  }
  if (diagnostics.count) {
    diagnostics_print(&diagnostics, stderr);
    arena_destroy(&arena);
    return 1;
  }
  for (size_t i = 0; i < tokens.count; i++) {
    const Token *token = &tokens.items[i];
    SourcePosition position = source_position(source, token->span.start);
    printf("%zu:%zu %s ", position.line, position.column,
           token_type_name(token->type));
    print_lexeme(token);
    fputc('\n', stdout);
  }
  arena_destroy(&arena);
  return 0;
}
