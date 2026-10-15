#include "common/diagnostic.h"
#include "lexer/lexer.h"

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
  if (argc != 3 || strcmp(argv[1], "dump-tokens") != 0) {
    fputs("usage: brief dump-tokens file.brf\n"
          "Only lexing is implemented; parsing and compilation come later.\n",
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
