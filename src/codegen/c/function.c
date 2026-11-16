#include "codegen/c/internal.h"

#include <inttypes.h>
#include <math.h>

static void binary(FILE *out, const IrFunction *function,
                   const IrInstruction *in) {
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
  if ((op == BINARY_LSHIFT || op == BINARY_RSHIFT) &&
      function->temporary_types[in->b] != in->type) {
    // Operands have already been evaluated once, left to right. Check the
    // count in its own type before the helper's signed/unsigned conversion.
    fputs("  if (", out);
    if (function->temporary_types[in->b]->is_signed)
      fprintf(out, "brt_tmp_%zu < 0 || ", in->b);
    fprintf(out, "brt_tmp_%zu >= %u) brt_panic(\"invalid shift count\");",
            in->b, c_bits(kind));
  }
  fprintf(out, "  brt_tmp_%zu = ", in->result);
  if (kind == SEMA_UNIT)
    fputs(op == BINARY_EQ ? "1" : "0", out);
  else if (kind == SEMA_STR) {
    if (op == BINARY_ADD)
      fprintf(out, "brt_concat(brt_tmp_%zu, brt_tmp_%zu)", in->a, in->b);
    else
      fprintf(out, "brt_compare(brt_tmp_%zu, brt_tmp_%zu) %s 0", in->a, in->b,
              operators[op]);
  } else if (kind == SEMA_STRUCT || kind == SEMA_SYM) {
    char a[96], b[96];
    snprintf(a, sizeof(a), "brt_tmp_%zu", in->a);
    snprintf(b, sizeof(b), "brt_tmp_%zu", in->b);
    if (op == BINARY_NE)
      fputc('!', out);
    c_equal(out, in->type, a, b);
  } else if (kind >= SEMA_SHRT && kind <= SEMA_ULNG && helpers[op]) {
    fputc('(', out);
    c_type(out, in->type);
    fprintf(out, ")brt_%s_%c(brt_tmp_%zu, brt_tmp_%zu, %u)", helpers[op],
            c_signed_integer(kind) ? 's' : 'u', in->a, in->b, c_bits(kind));
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

static void string(FILE *out, const IrInstruction *in) {
  size_t id = in->result;
  fprintf(out, "  static const unsigned char brt_bytes_%zu[] = ", id);
  c_string_literal(out, in->as.string.bytes, in->as.string.length);
  fputs(";\n", out);
  fprintf(out,
          "  static const brt_string brt_literal_%zu = {{0, NULL}, %zu, "
          "brt_bytes_%zu};\n  brt_tmp_%zu = &brt_literal_%zu;\n",
          id, in->as.string.length, id, id, id);
}

static void constant(FILE *out, const IrInstruction *in) {
  fprintf(out, "  brt_tmp_%zu = ", in->result);
  if (in->type->kind == SEMA_FLT || in->type->kind == SEMA_DUB) {
    if (isinf(in->as.constant.real))
      fputs(signbit(in->as.constant.real) ? "-INFINITY" : "INFINITY", out);
    else
      fprintf(out, "%a%s", in->as.constant.real,
              in->type->kind == SEMA_FLT ? "f" : "");
  } else if (in->as.constant.negative && in->as.constant.integer)
    fprintf(out, "(-INT64_C(%" PRIu64 ") - INT64_C(1))",
            in->as.constant.integer - 1);
  else
    fprintf(out, "UINT64_C(%" PRIu64 ")", in->as.constant.integer);
  fputs(";\n", out);
}

static void call(FILE *out, const IrProgram *p, const IrInstruction *in) {
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
            k == SEMA_BOOL        ? "bool"
            : k == SEMA_FLT       ? "flt"
            : k == SEMA_DUB       ? "dub"
            : c_signed_integer(k) ? "s"
                                  : "u");
  } else
    c_function_name(out, &p->functions[in->as.call.function]);
  fputc('(', out);
  int comma = 0;
  for (size_t k = 0; k < in->as.call.count; k++)
    if (in->as.call.args[k] != IR_NO_VALUE) {
      fprintf(out, "%sbrt_tmp_%zu", comma ? ", " : "", in->as.call.args[k]);
      comma = 1;
    }
  fputs(");\n", out);
}

static void convert(FILE *out, const IrProgram *p, const IrInstruction *in) {
  const SemaType *target = in->as.conversion;
  if (!in->tag) {
    fprintf(out, "  brt_tmp_%zu = (", in->result);
    c_type(out, target);
    fprintf(out, ")brt_tmp_%zu;\n", in->a);
    return;
  }
  const char *source = in->type->kind == SEMA_STR ? "text"
                       : !sema_integer(in->type)  ? "f"
                       : in->type->is_signed      ? "s"
                                                  : "u";
  const char *kind = target->kind == SEMA_FLT   ? "BRT_FLOAT"
                     : target->kind == SEMA_DUB ? "BRT_DOUBLE"
                     : target->is_signed        ? "BRT_SIGNED"
                                                : "BRT_UNSIGNED";
  const char *member = !sema_integer(target) ? "f"
                       : target->is_signed   ? "s"
                                             : "u";
  fprintf(out,
          "  { brt_locate(__FILE__, __LINE__); brt_number brt_converted = "
          "{0};\n"
          "    if (brt_convert_%s(brt_tmp_%zu, %s, %u, &brt_converted)) {\n"
          "      brt_tmp_%zu.tag = %zu; brt_tmp_%zu.payload.v%zu = (",
          source, in->a, kind, target->bits, in->result, p->ok->id, in->result,
          p->ok->id);
  c_type(out, target);
  fprintf(out,
          ")brt_converted.%s;\n    } else {\n      brt_tmp_%zu.tag = %zu;\n",
          member, in->result, in->tag->id);
  if (in->type->kind == SEMA_STR)
    fprintf(out, "      brt_tmp_%zu.payload.v%zu = brt_tmp_%zu;\n", in->result,
            in->tag->id, in->a);
  fputs("    }\n  }\n", out);
}

static void symbol(FILE *out, const IrInstruction *in) {
  fprintf(out, "  brt_tmp_%zu = (bry_%zu){.tag = %zu};\n", in->result,
          in->type->symbols->id, in->tag->id);
  const SemaVariant *v = sema_variant(in->type, in->tag);
  char base[64], field[128];
  snprintf(base, sizeof(base), "brt_tmp_%zu", in->result);
  for (size_t j = 0; j < in->as.symbol.count; j++) {
    if (in->as.symbol.args[j] == IR_NO_VALUE)
      continue;
    c_payload_name(field, sizeof(field), base, v, j);
    fprintf(out, "  %s = brt_tmp_%zu;\n", field, in->as.symbol.args[j]);
  }
}

// Repacks the payloads that both symbol sets share.
static void symbol_cast(FILE *out, const IrFunction *f,
                        const IrInstruction *in) {
  fprintf(out,
          "  brt_tmp_%zu = (bry_%zu){.tag = brt_tmp_%zu.tag};\n"
          "  switch (brt_tmp_%zu.tag) {\n",
          in->result, in->type->symbols->id, in->a, in->a);
  const SemaType *source = f->temporary_types[in->a];
  for (size_t k = 0; k < in->type->symbols->count; k++) {
    const SemaVariant *v = &in->type->symbols->variants[k];
    if (!sema_variant(source, v->tag))
      continue;
    int started = 0;
    char a[64], b[64], to[128], from[128];
    snprintf(a, sizeof(a), "brt_tmp_%zu", in->result);
    snprintf(b, sizeof(b), "brt_tmp_%zu", in->a);
    for (size_t j = 0; j < v->payload_count; j++) {
      if (v->payloads[j]->kind == SEMA_UNIT)
        continue;
      if (!started)
        fprintf(out, "  case %zu: ", v->tag->id);
      else
        fputc(' ', out);
      c_payload_name(to, sizeof(to), a, v, j);
      c_payload_name(from, sizeof(from), b, v, j);
      fprintf(out, "%s = %s;", to, from);
      started = 1;
    }
    if (started)
      fputs(" break;\n", out);
  }
  fputs("  default: break;\n  }\n", out);
}

static void symbol_payload(FILE *out, const IrFunction *f,
                           const IrInstruction *in) {
  const SemaVariant *v = sema_variant(f->temporary_types[in->a], in->tag);
  char base[64], value[128];
  snprintf(base, sizeof(base), "brt_tmp_%zu", in->a);
  c_payload_name(value, sizeof(value), base, v, in->position);
  fprintf(out, "  brt_tmp_%zu = %s;\n", in->result, value);
}

static void drop(FILE *out, const IrFunction *f, const IrInstruction *in) {
  const SemaType *type = in->op == IR_DROP_VAR     ? f->variables[in->a]
                         : in->op == IR_DROP_FIELD ? in->field->type
                                                   : f->temporary_types[in->a];
  char name[128];
  if (in->op == IR_DROP_FIELD)
    snprintf(name, sizeof(name), "brt_tmp_%zu->brt_field_%zu", in->a,
             in->field->id);
  else
    snprintf(name, sizeof(name), "brt_%s_%zu",
             in->op == IR_DROP_VAR ? "var" : "tmp", in->a);
  fputs("  ", out);
  c_ref_prefix(out, type, 0);
  fprintf(out, "%s); %s = ", name, name);
  c_zero(out, type);
  fputs(";\n", out);
}

static void unary(FILE *out, const IrInstruction *in) {
  SemaTypeKind kind = in->type->kind;
  int checked =
      in->as.unary == UNARY_NEG && kind >= SEMA_SHRT && kind <= SEMA_ULNG;
  if (checked)
    fputs("  brt_locate(__FILE__, __LINE__);", out);
  fprintf(out, "  brt_tmp_%zu = ", in->result);
  if (checked) {
    fputc('(', out);
    c_type(out, in->type);
    fprintf(out, ")brt_neg_%c(brt_tmp_%zu, %u)",
            c_signed_integer(kind) ? 's' : 'u', in->a, c_bits(kind));
  } else if (in->as.unary == UNARY_BIT_NOT && !c_signed_integer(kind)) {
    fputc('(', out);
    c_type(out, in->type);
    fprintf(out, ")brt_not_u(brt_tmp_%zu, %u)", in->a, c_bits(kind));
  } else
    fprintf(out, "%cbrt_tmp_%zu",
            in->as.unary == UNARY_NEG   ? '-'
            : in->as.unary == UNARY_NOT ? '!'
                                        : '~',
            in->a);
  fputs(";\n", out);
}

static void instruction(FILE *out, const IrProgram *p, const IrFunction *f,
                        const IrInstruction *in) {
  switch (in->op) {
  case IR_STRING:
    string(out, in);
    break;
  case IR_CONSTANT:
    constant(out, in);
    break;
  case IR_CALL:
  case IR_NEW:
    call(out, p, in);
    break;
  case IR_CONVERT:
    convert(out, p, in);
    break;
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
    fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu->brt_field_%zu;\n", in->result,
            in->a, in->field->id);
    break;
  case IR_FIELD_STORE:
    fprintf(out, "  brt_tmp_%zu->brt_field_%zu = brt_tmp_%zu;\n", in->a,
            in->field->id, in->b);
    break;
  case IR_SYMBOL:
    symbol(out, in);
    break;
  case IR_SYMBOL_CAST:
    symbol_cast(out, f, in);
    break;
  case IR_SYMBOL_TAG:
    fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu.tag;\n", in->result, in->a);
    break;
  case IR_SYMBOL_PAYLOAD:
    symbol_payload(out, f, in);
    break;
  case IR_RETAIN:
    fputs("  brt_locate(__FILE__, __LINE__); ", out);
    c_ref_prefix(out, f->temporary_types[in->a], 1);
    fprintf(out, "brt_tmp_%zu);\n", in->a);
    break;
  case IR_DROP_TEMP:
  case IR_DROP_VAR:
  case IR_DROP_FIELD:
    drop(out, f, in);
    break;
  case IR_BINARY:
    binary(out, f, in);
    break;
  case IR_UNARY:
    unary(out, in);
    break;
  case IR_LABEL:
    fprintf(out, "brt_label_%zu: ;\n", in->label);
    break;
  case IR_JUMP:
    fprintf(out, "  goto brt_label_%zu;\n", in->label);
    break;
  case IR_BRANCH_FALSE:
    fprintf(out, "  if (!brt_tmp_%zu) goto brt_label_%zu;\n", in->a, in->label);
    break;
  case IR_PANIC_SYMBOL:
    fprintf(out,
            "  { brt_locate(__FILE__, __LINE__); size_t brt_format_budget = "
            "1024;\n"
            "  brt_panic_begin();\n"
            "  brt_sym_%zu_write(stderr, brt_tmp_%zu, 0, &brt_format_budget);\n"
            "  fputc('\\n', stderr); brt_panic_end(); }\n",
            in->type->symbols->id, in->a);
    break;
  case IR_UNREACHABLE:
    fputs("  brt_locate(__FILE__, __LINE__); brt_panic(\"unreachable "
          "control flow\");\n",
          out);
    break;
  }
}

void c_function(FILE *out, const IrProgram *p, const IrFunction *f) {
  c_line(out, f->span);
  c_signature(out, f);
  fputs(" {\n", out);
  for (size_t j = 0; j < f->variable_count; j++) {
    if (f->variables[j]->kind == SEMA_UNIT ||
        f->variables[j]->kind == SEMA_NEVER)
      continue;
    if (j >= f->parameter_count) {
      fputs("  ", out);
      c_type(out, f->variables[j]);
      fprintf(out, " brt_var_%zu = {0};\n", j);
    }
    fprintf(out, "  (void)brt_var_%zu;\n", j);
  }
  for (size_t j = 0; j < f->temporaries; j++) {
    fputs("  ", out);
    c_type(out, f->temporary_types[j]);
    fprintf(out, " brt_tmp_%zu = {0};\n  (void)brt_tmp_%zu;\n", j, j);
  }
  for (size_t j = 0; j < f->count; j++) {
    const IrInstruction *in = &f->instructions[j];
    c_line(out, in->span);
    instruction(out, p, f, in);
  }
  fputs("}\n\n", out);
}
