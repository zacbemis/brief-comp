#include "driver/compile.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"

void compilation_init(Compilation *c, Arena *arena, SourceFile *source) {
  *c = (Compilation){.arena = arena, .source = source};
  diagnostics_init(&c->diagnostics, arena);
  intern_init(&c->names, arena);
}

void compilation_lex(Compilation *c) {
  c->tokens = lexer_tokenize(c->arena, c->source, &c->diagnostics);
  c->filtered = newline_filter(c->arena, c->tokens);
}

void compilation_parse(Compilation *c) {
  c->syntax = parse_program(c->arena, &c->names, c->filtered, &c->diagnostics);
}

void compilation_resolve(Compilation *c) {
  c->program = sema_resolve(c->arena, c->syntax, &c->diagnostics);
}

void compilation_check(Compilation *c) {
  sema_check(c->program, &c->diagnostics);
}

int compilation_lower(Compilation *c) {
  c->ir = ir_lower(c->arena, c->program);
  if (c->ir)
    ir_insert_rc(c->arena, c->ir);
  return c->ir != NULL;
}
