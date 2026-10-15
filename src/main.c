#include "codegen/c.h"
#include "common/diagnostic.h"
#include "driver/native.h"
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

typedef enum { DUMP_TOKENS, DUMP_AST, EMIT_C, BUILD, RUN } Command;
typedef struct {
  Command command;
  const char *source;
  const char *output;
  int debug;
} Options;
static int arguments(int argc, char **argv, Options *o) {
  if (argc < 3)
    return 0;
  if (strcmp(argv[1], "dump-tokens") == 0)
    o->command = DUMP_TOKENS;
  else if (strcmp(argv[1], "dump-ast") == 0)
    o->command = DUMP_AST;
  else if (strcmp(argv[1], "emit-c") == 0)
    o->command = EMIT_C;
  else if (strcmp(argv[1], "build") == 0)
    o->command = BUILD;
  else if (strcmp(argv[1], "run") == 0)
    o->command = RUN;
  else
    return 0;
  if (o->command <= EMIT_C) {
    o->source = argv[2];
    return argc == 3;
  }
  int flags = 1;
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (flags && strcmp(arg, "--") == 0)
      flags = 0;
    else if (flags && strcmp(arg, "--debug") == 0)
      o->debug = 1;
    else if (flags && strcmp(arg, "-o") == 0 && o->command == BUILD) {
      if (o->output || ++i == argc || !*argv[i])
        return 0;
      o->output = argv[i];
    } else if (flags && arg[0] == '-')
      return 0;
    else if (o->source || !*arg)
      return 0;
    else
      o->source = arg;
  }
  if (!o->output)
    o->output = "a.out";
  return o->source != NULL;
}
int main(int argc, char **argv) {
  Options options = {0};
  if (!arguments(argc, argv, &options)) {
    fputs("usage: brief dump-tokens file.brf\n"
          "       brief dump-ast file.brf\n"
          "       brief emit-c file.brf\n"
          "       brief build [--debug] file.brf [-o executable]\n"
          "       brief run [--debug] file.brf\n",
          stderr);
    return 2;
  }

  Arena arena;
  arena_init(&arena);
  Diagnostics diagnostics;
  diagnostics_init(&diagnostics, &arena);
  const char *load_error;
  SourceFile *source = source_load(&arena, options.source, &load_error);
  if (!source) {
    fprintf(stderr, "error: cannot read %s: %s\n", options.source, load_error);
    arena_destroy(&arena);
    return 1;
  }

  TokenArray tokens = lexer_tokenize(&arena, source, &diagnostics);
  if (options.command != DUMP_TOKENS) {
    InternTable names;
    intern_init(&names, &arena);
    TokenArray filtered = newline_filter(&arena, tokens);
    Program *program = parse_program(&arena, &names, filtered, &diagnostics);
    if (diagnostics.count) {
      diagnostics_print(&diagnostics, stderr);
      arena_destroy(&arena);
      return 1;
    }
    int result = 0;
    if (options.command == DUMP_AST)
      ast_print(stdout, program);
    else {
      SemaProgram *checked = sema_resolve(&arena, program, &diagnostics);
      sema_check(checked, &diagnostics);
      if (diagnostics.count) {
        diagnostics_print(&diagnostics, stderr);
        result = 1;
      } else {
        IrProgram *ir = ir_lower(&arena, checked);
        if (options.command == EMIT_C) {
          if (!emit_c(stdout, ir) || fflush(stdout) != 0) {
            fputs("error: cannot write generated C\n", stderr);
            result = 1;
          }
        } else
          result = native_build(&arena, ir, options.output, options.debug,
                                options.command == RUN);
      }
    }
    arena_destroy(&arena);
    return result;
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
