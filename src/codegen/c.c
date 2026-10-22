#include "codegen/c.h"
#include "runtime_text.h"
#include <inttypes.h>
#include <math.h>

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
  else if (type->kind == SEMA_SYM)
    fprintf(out, "bry_%zu", type->symbols->id);
  else
    fputs(names[type->kind], out);
}
static void ref_prefix(FILE *out, const SemaType *type, int retain) {
  if (type->kind == SEMA_SYM)
    fprintf(out, "brt_sym_%zu_%s(", type->symbols->id,
            retain ? "retain" : "release");
  else
    fputs(retain ? "brt_retain(" : "brt_release(", out);
}
static void zero(FILE *out, const SemaType *type) {
  if (type->kind == SEMA_SYM) {
    fputc('(', out);
    ctype(out, type);
    fputs("){0}", out);
  } else
    fputs("NULL", out);
}
static void equal(FILE *out, const SemaType *type, const char *a,
                  const char *b) {
  if (type->kind == SEMA_STR)
    fprintf(out, "brt_compare(%s, %s) == 0", a, b);
  else if (type->kind == SEMA_STRUCT)
    fprintf(out, "brt_struct_%s_equal(%s, %s)", type->name, a, b);
  else if (type->kind == SEMA_SYM)
    fprintf(out, "brt_sym_%zu_equal(%s, %s)", type->symbols->id, a, b);
  else
    fprintf(out, "%s == %s", a, b);
}
static void symbol_declarations(FILE *out, const IrProgram *p) {
  for (const SemaSymSet *s = p->symbol_sets; s; s = s->next) {
    fprintf(
        out,
        "typedef struct {\n  uint32_t tag;\n  union {\n    uint8_t empty;\n");
    for (size_t i = 0; i < s->count; i++) {
      const SemaVariant *v = &s->variants[i];
      if (!v->payload || v->payload->kind == SEMA_UNIT)
        continue;
      fputs("    ", out);
      ctype(out, v->payload);
      fprintf(out, " v%zu;\n", v->tag->id);
    }
    fprintf(out, "  } payload;\n} bry_%zu;\n", s->id);
    fprintf(out, "void brt_sym_%zu_retain(bry_%zu);\n", s->id, s->id);
    fprintf(out, "void brt_sym_%zu_release(bry_%zu);\n", s->id, s->id);
    fprintf(out,
            "void brt_sym_%zu_write(FILE *, bry_%zu, unsigned, size_t *);\n",
            s->id, s->id);
    if (s->equatable)
      fprintf(out, "_Bool brt_sym_%zu_equal(bry_%zu, bry_%zu);\n", s->id, s->id,
              s->id);
  }
}
static void symbol_helpers(FILE *out, const IrProgram *p) {
  for (const SemaSymSet *s = p->symbol_sets; s; s = s->next) {
    for (int retain = 0; retain <= 1; retain++) {
      fprintf(out,
              "void brt_sym_%zu_%s(bry_%zu value) {\n  switch (value.tag) {\n",
              s->id, retain ? "retain" : "release", s->id);
      for (size_t i = 0; i < s->count; i++) {
        const SemaVariant *v = &s->variants[i];
        if (!sema_reference(v->payload))
          continue;
        fprintf(out, "  case %zu: ", v->tag->id);
        ref_prefix(out, v->payload, retain);
        fprintf(out, "value.payload.v%zu); break;\n", v->tag->id);
      }
      fputs("  default: break;\n  }\n}\n", out);
    }
    if (!s->equatable)
      continue;
    fprintf(out,
            "_Bool brt_sym_%zu_equal(bry_%zu a, bry_%zu b) {\n"
            "  if (a.tag != b.tag) return 0;\n  switch (a.tag) {\n",
            s->id, s->id, s->id);
    for (size_t i = 0; i < s->count; i++) {
      const SemaVariant *v = &s->variants[i];
      fprintf(out, "  case %zu: return ", v->tag->id);
      if (!v->payload || v->payload->kind == SEMA_UNIT)
        fputs("1", out);
      else {
        char a[96], b[96];
        snprintf(a, sizeof(a), "a.payload.v%zu", v->tag->id);
        snprintf(b, sizeof(b), "b.payload.v%zu", v->tag->id);
        equal(out, v->payload, a, b);
      }
      fputs(";\n", out);
    }
    fputs("  default: return 1;\n  }\n}\n", out);
  }
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
static void write_value(FILE *out, const SemaType *t, const char *value) {
  if (t->kind == SEMA_UNIT)
    fputs("fputs(\"()\", out)", out);
  else if (t->kind == SEMA_SYM)
    fprintf(out, "brt_sym_%zu_write(out, %s, depth + 1, budget)",
            t->symbols->id, value);
  else if (t->kind == SEMA_STRUCT)
    fprintf(out, "brt_struct_%s_write(out, %s, depth + 1, budget)", t->name,
            value);
  else if (t->kind == SEMA_STR)
    fprintf(out, "brt_write_quoted(out, %s->bytes, %s->length, '\"')", value,
            value);
  else if (t->kind == SEMA_CHR)
    fprintf(out, "brt_write_quoted(out, &%s, 1, 39)", value);
  else {
    const char *suffix = t->kind == SEMA_BOOL  ? "bool"
                         : t->kind == SEMA_FLT ? "flt"
                         : t->kind == SEMA_DUB ? "dub"
                         : t->is_signed        ? "s"
                                               : "u";
    fprintf(out, "brt_write_number(out, brt_str_%s(%s))", suffix, value);
  }
}
static void formatters(FILE *out, const IrProgram *p) {
  for (size_t i = 0; i < p->struct_count; i++) {
    const SemaStruct *s = &p->structs[i];
    fprintf(out,
            "void brt_struct_%s_write(FILE *out, brs_%s *value, unsigned "
            "depth, size_t *budget) {\n"
            "  (void)value; if (depth >= 32 || !*budget) { fputs(\"...\", "
            "out); return; }\n"
            "  --*budget;\n"
            "  fputs(\"%s {\", out);\n",
            s->type.name, s->type.name, s->type.name);
    for (size_t j = 0; j < s->count; j++) {
      const SemaField *f = &s->fields[j];
      fprintf(out,
              "  if (!*budget) { fputs(\"%s... }\", out); return; }\n"
              "  --*budget; fputs(\"%s%s: \", out); ",
              j ? ", " : " ", j ? ", " : " ", f->name);
      char value[96];
      snprintf(value, sizeof(value), "value->brt_field_%zu", j);
      write_value(out, f->type, value);
      fputs(";\n", out);
    }
    fprintf(out, "  fputs(\"%s}\", out);\n}\n", s->count ? " " : "");
  }
  for (const SemaSymSet *s = p->symbol_sets; s; s = s->next) {
    fprintf(out,
            "void brt_sym_%zu_write(FILE *out, bry_%zu value, unsigned depth, "
            "size_t *budget) {\n"
            "  if (depth >= 32 || !*budget) { fputs(\"...\", out); return; }\n"
            "  --*budget;\n"
            "  switch (value.tag) {\n",
            s->id, s->id);
    for (size_t i = 0; i < s->count; i++) {
      const SemaVariant *v = &s->variants[i];
      fprintf(out, "  case %zu: fputs(\":%s\", out);", v->tag->id,
              v->tag->name);
      if (v->payload) {
        fputs(" fputc('(', out); ", out);
        char value[96];
        snprintf(value, sizeof(value), "value.payload.v%zu", v->tag->id);
        write_value(out, v->payload, value);
        fputs("; fputc(')', out);", out);
      }
      fputs(" break;\n", out);
    }
    fputs("  default: fputs(\"<invalid symbol>\", out); break;\n  }\n}\n", out);
  }
}
static void structures(FILE *out, const IrProgram *p) {
  for (size_t i = 0; i < p->struct_count; i++)
    fprintf(out, "typedef struct brs_%s brs_%s;\n", p->structs[i].type.name,
            p->structs[i].type.name);
  symbol_declarations(out, p);
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
    fprintf(out,
            "void brt_struct_%s_write(FILE *, brs_%s *, unsigned, size_t *);\n",
            s->type.name, s->type.name);
    if (s->equatable)
      fprintf(out, "_Bool brt_struct_%s_equal(brs_%s *, brs_%s *);\n",
              s->type.name, s->type.name, s->type.name);
  }
  symbol_helpers(out, p);
  formatters(out, p);
  for (size_t i = 0; i < p->struct_count; i++) {
    const SemaStruct *s = &p->structs[i];
    const char *name = s->type.name;
    fprintf(out,
            "void brt_struct_%s_destroy(brt_object *object) {\n  brs_%s *value "
            "= (brs_%s *)object;\n  (void)value;\n",
            name, name, name);
    for (size_t j = 0; j < s->count; j++)
      if (sema_reference(s->fields[j].type)) {
        fputs("  ", out);
        ref_prefix(out, s->fields[j].type, 0);
        fprintf(out, "value->brt_field_%zu);\n", j);
      }
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
      if (sema_reference(s->fields[j].type)) {
        fputs("  ", out);
        ref_prefix(out, s->fields[j].type, 1);
        fprintf(out, "f%zu);\n", j);
      }
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
      char a[96], b[96];
      snprintf(a, sizeof(a), "a->brt_field_%zu", j);
      snprintf(b, sizeof(b), "b->brt_field_%zu", j);
      fputs("\n    && ", out);
      equal(out, t, a, b);
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
  } else if (kind == SEMA_STRUCT || kind == SEMA_SYM) {
    char a[96], b[96];
    snprintf(a, sizeof(a), "brt_tmp_%zu", in->a);
    snprintf(b, sizeof(b), "brt_tmp_%zu", in->b);
    if (op == BINARY_NE)
      fputc('!', out);
    equal(out, in->type, a, b);
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
        if (in->type->kind == SEMA_FLT || in->type->kind == SEMA_DUB) {
          if (isinf(in->as.constant.real))
            fputs(signbit(in->as.constant.real) ? "-INFINITY" : "INFINITY",
                  out);
          else
            fprintf(out, "%a%s", in->as.constant.real,
                    in->type->kind == SEMA_FLT ? "f" : "");
        } else if (in->as.constant.negative && in->as.constant.integer)
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
      case IR_CONVERT: {
        const SemaType *target = in->as.conversion;
        if (!in->tag) {
          fprintf(out, "  brt_tmp_%zu = (", in->result);
          ctype(out, target);
          fprintf(out, ")brt_tmp_%zu;\n", in->a);
          break;
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
        fprintf(
            out,
            "  { brt_locate(__FILE__, __LINE__); brt_number brt_converted = "
            "{0};\n"
            "    if (brt_convert_%s(brt_tmp_%zu, %s, %u, &brt_converted)) {\n"
            "      brt_tmp_%zu.tag = %zu; brt_tmp_%zu.payload.v%zu = (",
            source, in->a, kind, target->bits, in->result, p->ok->id,
            in->result, p->ok->id);
        ctype(out, target);
        fprintf(
            out,
            ")brt_converted.%s;\n    } else {\n      brt_tmp_%zu.tag = %zu;\n",
            member, in->result, in->tag->id);
        if (in->type->kind == SEMA_STR)
          fprintf(out, "      brt_tmp_%zu.payload.v%zu = brt_tmp_%zu;\n",
                  in->result, in->tag->id, in->a);
        fputs("    }\n  }\n", out);
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
      case IR_SYMBOL:
        fprintf(out, "  brt_tmp_%zu = (bry_%zu){.tag = %zu};\n", in->result,
                in->type->symbols->id, in->tag->id);
        if (in->a != IR_NO_VALUE)
          fprintf(out, "  brt_tmp_%zu.payload.v%zu = brt_tmp_%zu;\n",
                  in->result, in->tag->id, in->a);
        break;
      case IR_SYMBOL_CAST: {
        fprintf(out,
                "  brt_tmp_%zu = (bry_%zu){.tag = brt_tmp_%zu.tag};\n"
                "  switch (brt_tmp_%zu.tag) {\n",
                in->result, in->type->symbols->id, in->a, in->a);
        const SemaType *source = f->temporary_types[in->a];
        for (size_t k = 0; k < in->type->symbols->count; k++) {
          const SemaVariant *v = &in->type->symbols->variants[k];
          if (v->payload && v->payload->kind != SEMA_UNIT &&
              sema_variant(source, v->tag))
            fprintf(out,
                    "  case %zu: brt_tmp_%zu.payload.v%zu = "
                    "brt_tmp_%zu.payload.v%zu; break;\n",
                    v->tag->id, in->result, v->tag->id, in->a, v->tag->id);
        }
        fputs("  default: break;\n  }\n", out);
        break;
      }
      case IR_SYMBOL_TAG:
        fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu.tag;\n", in->result, in->a);
        break;
      case IR_SYMBOL_PAYLOAD:
        fprintf(out, "  brt_tmp_%zu = brt_tmp_%zu.payload.v%zu;\n", in->result,
                in->a, in->tag->id);
        break;
      case IR_RETAIN:
        fputs("  brt_locate(__FILE__, __LINE__); ", out);
        ref_prefix(out, f->temporary_types[in->a], 1);
        fprintf(out, "brt_tmp_%zu);\n", in->a);
        break;
      case IR_DROP_TEMP:
      case IR_DROP_VAR:
      case IR_DROP_FIELD: {
        const SemaType *type = in->op == IR_DROP_VAR ? f->variables[in->a]
                               : in->op == IR_DROP_FIELD
                                   ? in->field->type
                                   : f->temporary_types[in->a];
        char name[128];
        if (in->op == IR_DROP_FIELD)
          snprintf(name, sizeof(name), "brt_tmp_%zu->brt_field_%zu", in->a,
                   in->field->id);
        else
          snprintf(name, sizeof(name), "brt_%s_%zu",
                   in->op == IR_DROP_VAR ? "var" : "tmp", in->a);
        fputs("  ", out);
        ref_prefix(out, type, 0);
        fprintf(out, "%s); %s = ", name, name);
        zero(out, type);
        fputs(";\n", out);
        break;
      }
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
      case IR_PANIC_SYMBOL:
        fprintf(
            out,
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
    fputs("}\n\n", out);
  }
  const SemaType *entry = p->functions[p->entry].return_type;
  int symbol = entry->kind == SEMA_SYM;
  fputs("#line 1 \"<brief entry>\"\nint main(void) {\n  int status = 0;\n  ",
        out);
  if (symbol) {
    ctype(out, entry);
    fputs(" result = ", out);
  } else if (sema_reference(entry))
    ref_prefix(out, entry, 0);
  function_name(out, &p->functions[p->entry]);
  fputs("()", out);
  if (!symbol && sema_reference(entry))
    fputc(')', out);
  fputs(";\n", out);
  if (symbol) {
    const SemaVariant *ok = sema_ok(p->entry_outcome);
    // Data-only sets are ordinary results; only a fallible outcome is an error.
    if (ok && p->entry_outcome->symbols->count > 1) {
      fprintf(
          out,
          "  if (result.tag != %zu) {\n    size_t brt_format_budget = 1024;\n"
          "    (void)fflush(stdout); fputs(\"error: \", stderr);\n"
          "    brt_sym_%zu_write(stderr, result, 0, &brt_format_budget);\n"
          "    fputc('\\n', stderr); status = 1;\n  }\n",
          p->ok->id, entry->symbols->id);
    }
    fputs("  ", out);
    ref_prefix(out, entry, 0);
    fputs("result);\n", out);
  }
  fputs("  brt_finish();\n  if (fflush(stdout) == EOF) {\n    "
        "fputs(\"error: cannot flush standard output\\n\", stderr);\n    "
        "return 1;\n  }\n  return status;\n}\n",
        out);
  return !ferror(out);
}
