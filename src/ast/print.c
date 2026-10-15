#include "ast/ast.h"

static void expr(FILE *out, const Expr *e);
static void decl(FILE *out, const Decl *d);

static void spelling(FILE *out, Token t) {
  // Raw tabs, standalone CR, and other control bytes can occur in literals.
  // Keep dumps on one readable line without changing literal tokens in AST.
  for (size_t i = 0; i < t.length; i++) {
    unsigned char c = (unsigned char)t.start[i];
    if (c == '\t')
      fputs("\\t", out);
    else if (c == '\r')
      fputs("\\r", out);
    else if (c == '\n')
      fputs("\\n", out);
    else if (c < 32 || c == 127)
      fprintf(out, "\\x%02x", (unsigned)c);
    else
      fputc(c, out);
  }
}
static const char *binary(BinaryOp op) {
  static const char *names[] = {"+",  "-", "*",  "/",  "%",  "**",  "==",
                                "!=", "<", "<=", ">",  ">=", "and", "or",
                                "&",  "|", "^",  "<<", ">>", ".."};
  return names[op];
}
static void type(FILE *out, const Type *t) {
  if (!t) {
    fputc('_', out);
    return;
  }
  switch (t->kind) {
  case TYPE_NAME:
  case TYPE_SELF:
    fputs(t->as.name, out);
    break;
  case TYPE_UNIT:
    fputs("()", out);
    break;
  case TYPE_INFER:
    fputc('_', out);
    break;
  case TYPE_APPLY:
    fputs("(apply ", out);
    type(out, t->as.apply.base);
    for (size_t i = 0; i < t->as.apply.count; i++) {
      fputc(' ', out);
      type(out, t->as.apply.args[i]);
    }
    fputc(')', out);
    break;
  case TYPE_ARRAY:
    fputs("(array-type ", out);
    type(out, t->as.array.element);
    fputc(' ', out);
    spelling(out, t->as.array.size);
    fputc(')', out);
    break;
  case TYPE_SYM:
    fputs("(Sym", out);
    for (size_t i = 0; i < t->as.sym.count; i++) {
      SymEntry entry = t->as.sym.items[i];
      fputc(' ', out);
      if (entry.tag) {
        fprintf(out, "(:%s", entry.tag);
        if (entry.type) {
          fputc(' ', out);
          type(out, entry.type);
        }
        fputc(')', out);
      } else if (entry.type->kind == TYPE_INFER)
        fputc('_', out);
      else {
        fputs("(set ", out);
        type(out, entry.type);
        fputc(')', out);
      }
    }
    fputc(')', out);
    break;
  default:
    fputs("(unsupported-type)", out);
    break;
  }
}
static void parameter(FILE *out, const Parameter *p, const char *label) {
  fprintf(out, "(%s%s%s ", label, p->visibility == VISIBILITY_PUB ? "pub " : "",
          p->name);
  if (p->borrow == BORROW_MUT)
    fputs("& ", out);
  type(out, p->type);
  fputc(')', out);
}
static void statement(FILE *out, const Stmt *s) {
  switch (s->kind) {
  case STMT_RET:
    fputs("(ret", out);
    if (s->as.value) {
      fputc(' ', out);
      expr(out, s->as.value);
    }
    fputc(')', out);
    break;
  case STMT_BREAK:
    fputs("(break)", out);
    break;
  case STMT_CONTINUE:
    fputs("(continue)", out);
    break;
  case STMT_EXPR:
    expr(out, s->as.value);
    break;
  case STMT_ASSIGN: {
    static const char *ops[] = {"=", "+=", "-=", "*=", "/=", "%="};
    fprintf(out, "(%s ", ops[s->as.assign.op]);
    expr(out, s->as.assign.target);
    fputc(' ', out);
    expr(out, s->as.assign.value);
    fputc(')', out);
    break;
  }
  }
}
static void pattern(FILE *out, const Pattern *p) {
  switch (p->kind) {
  case PAT_BIND:
    fprintf(out, "(bind %s)", p->as.name);
    break;
  case PAT_WILDCARD:
    fputs("(wildcard)", out);
    break;
  case PAT_SET:
    fprintf(out, "(set %s)", p->as.name);
    break;
  case PAT_LITERAL:
    fputs("(literal ", out);
    expr(out, p->as.literal);
    fputc(')', out);
    break;
  case PAT_SYMBOL:
    fprintf(out, "(tag %s", p->as.symbol.tag);
    if (p->as.symbol.payload) {
      fputc(' ', out);
      pattern(out, p->as.symbol.payload);
    }
    fputc(')', out);
    break;
  case PAT_RANGE:
    fputs("(range ", out);
    expr(out, p->as.range.lower);
    fputc(' ', out);
    expr(out, p->as.range.upper);
    fputc(')', out);
    break;
  case PAT_COMPARE:
    fprintf(out, "(compare %s ", binary(p->as.compare.op));
    expr(out, p->as.compare.value);
    fputc(')', out);
    break;
  }
}
static void expr(FILE *out, const Expr *e) {
  if (!e) {
    fputs("none", out);
    return;
  }
  switch (e->kind) {
  case EXPR_NAME:
    fprintf(out, "(name %s)", e->as.name);
    break;
  case EXPR_UNIT:
    fputs("(unit)", out);
    break;
  case EXPR_INT:
  case EXPR_FLOAT:
  case EXPR_BOOL:
  case EXPR_STRING:
  case EXPR_CHAR: {
    const char *label = e->kind == EXPR_INT      ? "int"
                        : e->kind == EXPR_FLOAT  ? "float"
                        : e->kind == EXPR_BOOL   ? "bool"
                        : e->kind == EXPR_STRING ? "str"
                                                 : "chr";
    fprintf(out, "(%s ", label);
    spelling(out, e->as.literal);
    fputc(')', out);
    break;
  }
  case EXPR_SYMBOL:
    fprintf(out, "(symbol %s", e->as.symbol.tag);
    if (e->as.symbol.payload) {
      fputc(' ', out);
      expr(out, e->as.symbol.payload);
    }
    fputc(')', out);
    break;
  case EXPR_UNARY: {
    static const char *ops[] = {"-",     "!",      "~",     "pre++",
                                "pre--", "post++", "post--"};
    fprintf(out, "(%s ", ops[e->as.unary.op]);
    expr(out, e->as.unary.value);
    fputc(')', out);
    break;
  }
  case EXPR_BINARY:
    fprintf(out, "(%s ", binary(e->as.binary.op));
    expr(out, e->as.binary.left);
    fputc(' ', out);
    expr(out, e->as.binary.right);
    fputc(')', out);
    break;
  case EXPR_CALL:
    fputs("(call ", out);
    expr(out, e->as.call.callee);
    if (e->as.call.type_count) {
      fputs(" (types", out);
      for (size_t i = 0; i < e->as.call.type_count; i++) {
        fputc(' ', out);
        type(out, e->as.call.types[i]);
      }
      fputc(')', out);
    }
    for (size_t i = 0; i < e->as.call.count; i++) {
      fputc(' ', out);
      expr(out, e->as.call.args[i]);
    }
    fputc(')', out);
    break;
  case EXPR_FIELD:
    fputs("(field ", out);
    expr(out, e->as.field.object);
    fprintf(out, " %s)", e->as.field.name);
    break;
  case EXPR_INDEX:
    fputs("(index ", out);
    expr(out, e->as.index.object);
    fputc(' ', out);
    expr(out, e->as.index.index);
    fputc(')', out);
    break;
  case EXPR_STRUCT_LIT:
    fputs("(struct-value ", out);
    expr(out, e->as.structure.type);
    for (size_t i = 0; i < e->as.structure.count; i++) {
      fprintf(out, " (%s ", e->as.structure.fields[i].name);
      expr(out, e->as.structure.fields[i].value);
      fputc(')', out);
    }
    fputc(')', out);
    break;
  case EXPR_BLOCK:
    fputs("(block", out);
    for (size_t i = 0; i < e->as.block.count; i++) {
      BlockItem item = e->as.block.items[i];
      fputc(' ', out);
      if (item.kind == BLOCK_ITEM_DECL)
        decl(out, item.as.decl);
      else if (item.kind == BLOCK_ITEM_STMT)
        statement(out, item.as.stmt);
      else
        expr(out, item.as.expr);
    }
    fputc(')', out);
    break;
  case EXPR_IF:
    fputs("(if ", out);
    expr(out, e->as.conditional.condition);
    fputc(' ', out);
    expr(out, e->as.conditional.then_branch);
    fputc(' ', out);
    expr(out, e->as.conditional.else_branch);
    fputc(')', out);
    break;
  case EXPR_WHILE:
    fputs("(while ", out);
    expr(out, e->as.while_loop.condition);
    fputc(' ', out);
    expr(out, e->as.while_loop.body);
    fputc(')', out);
    break;
  case EXPR_FOR:
    fprintf(out, "(for %s ", e->as.for_loop.name);
    expr(out, e->as.for_loop.iterable);
    fputc(' ', out);
    expr(out, e->as.for_loop.body);
    fputc(')', out);
    break;
  case EXPR_MATCH:
    fputs("(match", out);
    for (size_t i = 0; i < e->as.match.count; i++) {
      fputs(" (arm ", out);
      pattern(out, e->as.match.arms[i].pattern);
      fputc(' ', out);
      expr(out, e->as.match.arms[i].value);
      fputc(')', out);
    }
    fputc(')', out);
    break;
  case EXPR_TRY:
  case EXPR_PANIC:
    fputs(e->kind == EXPR_TRY ? "(? " : "(panic ", out);
    expr(out, e->as.postfix);
    fputc(')', out);
    break;
  case EXPR_PIPE:
    fputs("(|> ", out);
    expr(out, e->as.pipe.value);
    fputc(' ', out);
    expr(out, e->as.pipe.stage);
    fputc(')', out);
    break;
  default:
    fputs("(unsupported-expression)", out);
    break;
  }
}
static void decl(FILE *out, const Decl *d) {
  const char *visibility = d->visibility == VISIBILITY_PUB ? "pub " : "";
  switch (d->kind) {
  case DECL_BINDING: {
    const char *kind = d->as.binding.kind == BIND_LET   ? "let"
                       : d->as.binding.kind == BIND_VAR ? "var"
                                                        : "const";
    fprintf(out, "(%s %s%s ", kind, visibility, d->name);
    type(out, d->as.binding.annotation);
    fputc(' ', out);
    expr(out, d->as.binding.value);
    fputc(')', out);
    break;
  }
  case DECL_FN:
    fprintf(out, "(fn %s%s (params", visibility, d->name);
    for (size_t i = 0; i < d->as.function.count; i++) {
      fputc(' ', out);
      parameter(out, &d->as.function.params[i], "");
    }
    fputs(") ", out);
    type(out, d->as.function.return_type);
    fputc(' ', out);
    expr(out, d->as.function.body);
    fputc(')', out);
    break;
  case DECL_STRUCT:
    fprintf(out, "(struct %s%s", visibility, d->name);
    for (size_t i = 0; i < d->as.structure.count; i++) {
      fputc(' ', out);
      parameter(out, &d->as.structure.fields[i], "field ");
    }
    fputc(')', out);
    break;
  case DECL_TYPE:
    fprintf(out, "(type %s%s ", visibility, d->name);
    type(out, d->as.alias);
    fputc(')', out);
    break;
  case DECL_IMPL:
    fputs("(impl ", out);
    type(out, d->as.impl.target);
    for (size_t i = 0; i < d->as.impl.count; i++) {
      fputc(' ', out);
      decl(out, d->as.impl.methods[i]);
    }
    fputc(')', out);
    break;
  default:
    fputs("(unsupported-declaration)", out);
    break;
  }
}
void ast_print(FILE *out, const Program *program) {
  fputs("(program", out);
  for (size_t i = 0; i < program->count; i++) {
    fputc(' ', out);
    decl(out, program->items[i]);
  }
  fputs(")\n", out);
}
