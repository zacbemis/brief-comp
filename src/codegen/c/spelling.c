#include "codegen/c/internal.h"

void c_string_literal(FILE *out, const unsigned char *bytes, size_t length) {
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

void c_line(FILE *out, Span span) {
  if (!span.file)
    return;
  fprintf(out, "#line %zu ", source_position(span.file, span.start).line);
  const unsigned char *path = (const unsigned char *)span.file->path;
  // Source paths use the same fixed-width, trigraph-safe escaping as strings.
  size_t length = 0;
  while (path[length])
    length++;
  c_string_literal(out, path, length);
  fputc('\n', out);
}

void c_type(FILE *out, const SemaType *type) {
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

void c_ref_prefix(FILE *out, const SemaType *type, int retain) {
  if (type->kind == SEMA_SYM)
    fprintf(out, "brt_sym_%zu_%s(", type->symbols->id,
            retain ? "retain" : "release");
  else
    fputs(retain ? "brt_retain(" : "brt_release(", out);
}

void c_zero(FILE *out, const SemaType *type) {
  if (type->kind == SEMA_SYM) {
    fputc('(', out);
    c_type(out, type);
    fputs("){0}", out);
  } else
    fputs("NULL", out);
}

void c_equal(FILE *out, const SemaType *type, const char *a, const char *b) {
  if (type->kind == SEMA_STR)
    fprintf(out, "brt_compare(%s, %s) == 0", a, b);
  else if (type->kind == SEMA_STRUCT)
    fprintf(out, "brt_struct_%s_equal(%s, %s)", type->name, a, b);
  else if (type->kind == SEMA_SYM)
    fprintf(out, "brt_sym_%zu_equal(%s, %s)", type->symbols->id, a, b);
  else
    fprintf(out, "%s == %s", a, b);
}

void c_payload_name(char *out, size_t size, const char *value,
                    const SemaVariant *variant, size_t position) {
  if (variant->payload_count == 1)
    snprintf(out, size, "%s.payload.v%zu", value, variant->tag->id);
  else
    snprintf(out, size, "%s.payload.v%zu.p%zu", value, variant->tag->id,
             position);
}

void c_function_name(FILE *out, const IrFunction *f) {
  fprintf(out, "%s%s", f->method ? "brm_" : "brf_", f->name);
}

unsigned c_bits(SemaTypeKind kind) {
  return kind <= SEMA_USHRT ? 16 : kind <= SEMA_UINT ? 32 : 64;
}

int c_signed_integer(SemaTypeKind kind) {
  return kind == SEMA_SHRT || kind == SEMA_INT || kind == SEMA_LNG;
}

void c_signature(FILE *out, const IrFunction *f) {
  c_type(out, f->return_type);
  fputc(' ', out);
  c_function_name(out, f);
  fputc('(', out);
  int comma = 0;
  for (size_t j = 0; j < f->parameter_count; j++) {
    if (f->variables[j]->kind == SEMA_UNIT)
      continue;
    if (comma)
      fputs(", ", out);
    c_type(out, f->variables[j]);
    fprintf(out, " brt_var_%zu", j);
    comma = 1;
  }
  if (!comma)
    fputs("void", out);
  fputc(')', out);
}

void c_write_value(FILE *out, const SemaType *t, const char *value) {
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
                         : t->kind == SEMA_DBL ? "dbl"
                         : t->is_signed        ? "s"
                                               : "u";
    fprintf(out, "brt_write_number(out, brt_str_%s(%s))", suffix, value);
  }
}
