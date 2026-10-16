#include "codegen/c.h"
#include "runtime_text.h"
#include <inttypes.h>

static void string_literal(FILE *out, const unsigned char *bytes,
                           size_t length) {
  fputc('"', out);
  for (size_t i = 0; i < length; i++) {
    unsigned char c = bytes[i];
    switch (c) {
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputs("\\t", out);
      break;
    case '\\':
      fputs("\\\\", out);
      break;
    case '"':
      fputs("\\\"", out);
      break;
    case '?':
      fputs("\\?", out);
      break;
    default:
      if (c < 32 || c >= 127)
        fprintf(out, "\\%03o", (unsigned int)c);
      else
        fputc(c, out);
    }
  }
  fputc('"', out);
}
static void line(FILE *out, Span span) {
  if (!span.file)
    return;
  fprintf(out, "#line %zu ", source_position(span.file, span.start).line);
  const unsigned char *path = (const unsigned char *)span.file->path;
  // Source paths use the same fixed-width, trigraph-safe escaping as strings.
  size_t length = 0;
  while (path[length])
    length++;
  string_literal(out, path, length);
  fputc('\n', out);
}
static void ctype(FILE *out, const SemaType *type) {
  static const char *names[] = {
      "void",    "void",     "void",     "void",    "_Bool",
      "uint8_t", "int16_t",  "uint16_t", "int32_t", "uint32_t",
      "int64_t", "uint64_t", "float",    "double",  "const brt_string *"};
  if (type->kind == SEMA_STRUCT)
    fprintf(out, "brs_%s *", type->name);
  else
    fputs(names[type->kind], out);
}
static void function_name(FILE *out, const IrFunction *f) {
  fprintf(out, "%s%s", f->method ? "brm_" : "brf_", f->name);
}
static unsigned bits(SemaTypeKind kind) {
  return kind <= SEMA_USHRT ? 16 : kind <= SEMA_UINT ? 32 : 64;
}
static int signed_integer(SemaTypeKind kind) {
  return kind == SEMA_SHRT || kind == SEMA_INT || kind == SEMA_LNG;
}
static void signature(FILE *out, const IrFunction *f) {
  ctype(out, f->return_type);
  fputc(' ', out);
  function_name(out, f);
  fputc('(', out);
  int comma = 0;
  for (size_t j = 0; j < f->parameter_count; j++) {
    if (f->variables[j]->kind == SEMA_UNIT)
      continue;
    if (comma)
      fputs(", ", out);
    ctype(out, f->variables[j]);
    fprintf(out, " brt_var_%zu", j);
    comma = 1;
  }
  if (!comma)
    fputs("void", out);
  fputc(')', out);
}
static void structures(FILE *out, const IrProgram *p) {
  for (size_t i = 0; i < p->struct_count; i++)
    fprintf(out, "typedef struct brs_%s brs_%s;\n", p->structs[i].type.name,
            p->structs[i].type.name);
  for (size_t i = 0; i < p->struct_count; i++) {
    const SemaStruct *s = &p->structs[i];
    fprintf(out, "struct brs_%s {\n  brt_object brt_header;\n", s->type.name);
    for (size_t j = 0; j < s->count; j++) {
      if (s->fields[j].type->kind == SEMA_UNIT)
        continue;
      fputs("  ", out);
      ctype(out, s->fields[j].type);
      fprintf(out, " brt_field_%zu;\n", j);
    }
    fputs("};\n", out);
    if (s->equatable)
      fprintf(out, "_Bool brt_struct_%s_equal(brs_%s *, brs_%s *);\n",
              s->type.name, s->type.name, s->type.name);
  }
  for (size_t i = 0; i < p->struct_count; i++) {
    const SemaStruct *s = &p->structs[i];
    const char *name = s->type.name;
    fprintf(out,
            "void brt_struct_%s_destroy(brt_object *object) {\n  brs_%s *value "
            "= (brs_%s *)object;\n  (void)value;\n",
            name, name, name);
    for (size_t j = 0; j < s->count; j++)
      if (sema_reference(s->fields[j].type))
        fprintf(out, "  brt_release(value->brt_field_%zu);\n", j);
    fprintf(out, "}\nbrs_%s *brt_struct_%s_new(", name, name);
    int comma = 0;
    for (size_t j = 0; j < s->count; j++) {
      if (s->fields[j].type->kind == SEMA_UNIT)
        continue;
      if (comma)
        fputs(", ", out);
      ctype(out, s->fields[j].type);
      fprintf(out, " f%zu", j);
      comma = 1;
    }
    if (!comma)
      fputs("void", out);
    fprintf(out,
            ") {\n  brs_%s *value = brt_alloc(sizeof(*value), "
            "brt_struct_%s_destroy);\n",
            name, name);
    for (size_t j = 0; j < s->count; j++) {
      if (s->fields[j].type->kind == SEMA_UNIT)
        continue;
      if (sema_reference(s->fields[j].type))
        fprintf(out, "  brt_retain(f%zu);\n", j);
      fprintf(out, "  value->brt_field_%zu = f%zu;\n", j, j);
    }
    fputs("  return value;\n}\n", out);
    if (!s->equatable)
      continue;
    fprintf(out,
            "_Bool brt_struct_%s_equal(brs_%s *a, brs_%s *b) {\n  (void)a; "
            "(void)b;\n  return 1",
            name, name, name);
    for (size_t j = 0; j < s->count; j++) {
      const SemaType *t = s->fields[j].type;
      if (t->kind == SEMA_UNIT)
        continue;
      if (t->kind == SEMA_STR)
        fprintf(out,
                "\n    && brt_compare(a->brt_field_%zu, b->brt_field_%zu) == 0",
                j, j);
      else if (t->kind == SEMA_STRUCT)
        fprintf(
            out,
            "\n    && brt_struct_%s_equal(a->brt_field_%zu, b->brt_field_%zu)",
            t->name, j, j);
      else
        fprintf(out, "\n    && a->brt_field_%zu == b->brt_field_%zu", j, j);
    }
    fputs(";\n}\n", out);
  }
}
static void binary(FILE *out, const IrInstruction *in) {
  BinaryOp op = in->as.binary;
  SemaTypeKind kind = in->type->kind;
  const char *operators[] = {"+",  "-", "*",  "/",  "%",  NULL, "==",
                             "!=", "<", "<=", ">",  ">=", NULL, NULL,
                             "&",  "|", "^",  NULL, NULL};
  const char *helpers[] = {"add", "sub", "mul", "div",    "rem",   "pow", NULL,
                           NULL,  NULL,  NULL,  NULL,     NULL,    NULL,  NULL,
                           NULL,  NULL,  NULL,  "lshift", "rshift"};
  if ((kind >= SEMA_SHRT && kind <= SEMA_ULNG && helpers[op]) ||
      (kind == SEMA_STR && op == BINARY_ADD))
    fputs("  brt_locate(__FILE__, __LINE__);", out);
  fprintf(out, "  brt_tmp_%zu = ", in->result);
  if (kind == SEMA_UNIT)
    fputs(op == BINARY_EQ ? "1" : "0", out);
  else if (kind == SEMA_STR) {
    if (op == BINARY_ADD)
      fprintf(out, "brt_concat(brt_tmp_%zu, brt_tmp_%zu)", in->a, in->b);
    else
      fprintf(out, "brt_compare(brt_tmp_%zu, brt_tmp_%zu) %s 0", in->a, in->b,
              operators[op]);
  } else if (kind == SEMA_STRUCT) {
    fprintf(out, "%sbrt_struct_%s_equal(brt_tmp_%zu, brt_tmp_%zu)",
            op == BINARY_NE ? "!" : "", in->type->name, in->a, in->b);
  } else if (kind >= SEMA_SHRT && kind <= SEMA_ULNG && helpers[op]) {
    fputc('(', out);
    ctype(out, in->type);
    fprintf(out, ")brt_%s_%c(brt_tmp_%zu, brt_tmp_%zu, %u)", helpers[op],
            signed_integer(kind) ? 's' : 'u', in->a, in->b, bits(kind));
  } else if (op == BINARY_POW)
    fprintf(out, "%s(brt_tmp_%zu, brt_tmp_%zu)",
            kind == SEMA_FLT ? "powf" : "pow", in->a, in->b);
  else if (op == BINARY_DIV && (kind == SEMA_FLT || kind == SEMA_DUB))
    fprintf(out, "brt_div_%s(brt_tmp_%zu, brt_tmp_%zu)",
            kind == SEMA_FLT ? "flt" : "dub", in->a, in->b);
  else
    fprintf(out, "brt_tmp_%zu %s brt_tmp_%zu", in->a, operators[op], in->b);
  fputs(";\n", out);
}
int emit_c(FILE *out, const IrProgram *p) {
  if (!p || !p->ownership_inserted)
    return 0;
  fputs("/* Generated by Brief. Self-contained C11; link with -lm. */\n", out);
  fwrite(brief_runtime_text, 1, sizeof(brief_runtime_text) - 1, out);
  fputc('\n', out);
  structures(out, p);
  for (size_t i = 0; i < p->count; i++) {
    signature(out, &p->functions[i]);
    fputs(";\n", out);
  }
  for (size_t i = 0; i < p->count; i++) {
    const IrFunction *f = &p->functions[i];
    line(out, f->span);
    signature(out, f);
    fputs(" {\n", out);
    for (size_t j = 0; j < f->variable_count; j++) {
      if (f->variables[j]->kind == SEMA_UNIT ||
          f->variables[j]->kind == SEMA_NEVER)
        continue;
      if (j >= f->parameter_count) {
        fputs("  ", out);
        ctype(out, f->variables[j]);
        fprintf(out, " brt_var_%zu = {0};\n", j);
      }
      fprintf(out, "  (void)brt_var_%zu;\n", j);
    }
    for (size_t j = 0; j < f->temporaries; j++) {
      fputs("  ", out);
      ctype(out, f->temporary_types[j]);
      fprintf(out, " brt_tmp_%zu = {0};\n  (void)brt_tmp_%zu;\n", j, j);
    }
    for (size_t j = 0; j < f->count; j++) {
      const IrInstruction *in = &f->instructions[j];
      line(out, in->span);
      switch (in->op) {
      case IR_STRING: {
        size_t id = in->result;
        fprintf(out, "  static const unsigned char brt_bytes_%zu[] = ", id);
        string_literal(out, in->as.string.bytes, in->as.string.length);
        fputs(";\n", out);
        fprintf(out,
                "  static const brt_string brt_literal_%zu = {{0, NULL}, %zu, "
                "brt_bytes_%zu};\n  brt_tmp_%zu = &brt_literal_%zu;\n",
                id, in->as.string.length, id, id, id);
        break;
      }
      case IR_CONSTANT:
        fprintf(out, "  brt_tmp_%zu = ", in->result);
        if (in->type->kind == SEMA_FLT || in->type->kind == SEMA_DUB)
          fprintf(out, "%a%s", in->as.constant.real,
                  in->type->kind == SEMA_FLT ? "f" : "");
        else if (in->as.constant.negative && in->as.constant.integer)
          fprintf(out, "(-INT64_C(%" PRIu64 ") - INT64_C(1))",
                  in->as.constant.integer - 1);
        else
          fprintf(out, "UINT64_C(%" PRIu64 ")", in->as.constant.integer);
        fputs(";\n", out);
        break;
      case IR_CALL:
      case IR_NEW: {
        if (in->op == IR_NEW || in->as.call.builtin == SEMA_BUILTIN_STR)
          fputs("  brt_locate(__FILE__, __LINE__);", out);
        fputs("  ", out);
        if (in->result != IR_NO_VALUE)
          fprintf(out, "brt_tmp_%zu = ", in->result);
        if (in->op == IR_NEW)
          fprintf(out, "brt_struct_%s_new", in->type->name);
        else if (in->as.call.builtin == SEMA_BUILTIN_PRINT)
          fputs("brt_print", out);
        else if (in->as.call.builtin == SEMA_BUILTIN_STR) {
          SemaTypeKind k = in->type->kind;
          fprintf(out, "brt_str_%s",
                  k == SEMA_BOOL      ? "bool"
                  : k == SEMA_FLT     ? "flt"
                  : k == SEMA_DUB     ? "dub"
                  : signed_integer(k) ? "s"
                                      : "u");
        } else
          function_name(out, &p->functions[in->as.call.function]);
        fputc('(', out);
        int comma = 0;
        for (size_t k = 0; k < in->as.call.count; k++)
          if (in->as.call.args[k] != IR_NO_VALUE) {
            fprintf(out, "%sbrt_tmp_%zu", comma ? ", " : "",
                    in->as.call.args[k]);
            comma = 1;
          }
        fputs(");\n", out);
        break;
      }
      case IR_RETURN:
        if (in->a == IR_NO_VALUE)
          fputs("  return;\n", out);
        else
          fprintf(out, "  return brt_tmp_%zu;\n", in->a);
        break;
      case IR_LOAD:
        fprintf(out, "  brt_tmp_%zu = brt_var_%zu;\n", in->result, in->a);
        break;
      case IR_STORE:
        fprintf(out, "  brt_var_%zu = brt_tmp_%zu;\n", in->a, in->b);
        break;
      case IR_COPY:
        fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu;\n", in->result, in->a);
        break;
      case IR_FIELD_LOAD:
        fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu->brt_field_%zu;\n",
                in->result, in->a, in->field->id);
        break;
      case IR_FIELD_STORE:
        fprintf(out, "  brt_tmp_%zu->brt_field_%zu = brt_tmp_%zu;\n", in->a,
                in->field->id, in->b);
        break;
      case IR_RETAIN:
        fprintf(out,
                "  brt_locate(__FILE__, __LINE__); brt_retain(brt_tmp_%zu);\n",
                in->a);
        break;
      case IR_DROP_TEMP:
        fprintf(out, "  brt_release(brt_tmp_%zu); brt_tmp_%zu = NULL;\n", in->a,
                in->a);
        break;
      case IR_DROP_VAR:
        fprintf(out, "  brt_release(brt_var_%zu); brt_var_%zu = NULL;\n", in->a,
                in->a);
        break;
      case IR_DROP_FIELD:
        fprintf(out,
                "  brt_release(brt_tmp_%zu->brt_field_%zu); "
                "brt_tmp_%zu->brt_field_%zu = NULL;\n",
                in->a, in->field->id, in->a, in->field->id);
        break;
      case IR_BINARY:
        binary(out, in);
        break;
      case IR_UNARY: {
        SemaTypeKind kind = in->type->kind;
        if (in->as.unary == UNARY_NEG && kind >= SEMA_SHRT && kind <= SEMA_ULNG)
          fputs("  brt_locate(__FILE__, __LINE__);", out);
        fprintf(out, "  brt_tmp_%zu = ", in->result);
        if (in->as.unary == UNARY_NEG && kind >= SEMA_SHRT &&
            kind <= SEMA_ULNG) {
          fputc('(', out);
          ctype(out, in->type);
          fprintf(out, ")brt_neg_%c(brt_tmp_%zu, %u)",
                  signed_integer(kind) ? 's' : 'u', in->a, bits(kind));
        } else if (in->as.unary == UNARY_BIT_NOT && !signed_integer(kind)) {
          fputc('(', out);
          ctype(out, in->type);
          fprintf(out, ")brt_not_u(brt_tmp_%zu, %u)", in->a, bits(kind));
        } else
          fprintf(out, "%cbrt_tmp_%zu",
                  in->as.unary == UNARY_NEG   ? '-'
                  : in->as.unary == UNARY_NOT ? '!'
                                              : '~',
                  in->a);
        fputs(";\n", out);
        break;
      }
      case IR_LABEL:
        fprintf(out, "brt_label_%zu: ;\n", in->label);
        break;
      case IR_JUMP:
        fprintf(out, "  goto brt_label_%zu;\n", in->label);
        break;
      case IR_BRANCH_FALSE:
        fprintf(out, "  if (!brt_tmp_%zu) goto brt_label_%zu;\n", in->a,
                in->label);
        break;
      case IR_UNREACHABLE:
        fputs("  brt_locate(__FILE__, __LINE__); brt_panic(\"unreachable "
              "control flow\");\n",
              out);
        break;
      }
    }
    fputs("}\n\n", out);
  }
  fputs("#line 1 \"<brief entry>\"\nint main(void) {\n  ", out);
  if (sema_reference(p->functions[p->entry].return_type))
    fputs("brt_release(", out);
  function_name(out, &p->functions[p->entry]);
  fputs("()", out);
  if (sema_reference(p->functions[p->entry].return_type))
    fputc(')', out);
  fputs(";\n  brt_finish();\n  if (fflush(stdout) == EOF) {\n    "
        "fputs(\"error: cannot flush standard output\\n\", stderr);\n    "
        "return 1;\n  }\n  return 0;\n}\n",
        out);
  return !ferror(out);
}
