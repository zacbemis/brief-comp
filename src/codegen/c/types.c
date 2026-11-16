#include "codegen/c/internal.h"

static void symbol_declarations(FILE *out, const IrProgram *p) {
  for (size_t set = 0; set < p->symbol_set_count; set++) {
    const SemaSymSet *s = p->symbol_sets[set];
    fprintf(
        out,
        "typedef struct {\n  uint32_t tag;\n  union {\n    uint8_t empty;\n");
    for (size_t i = 0; i < s->count; i++) {
      const SemaVariant *v = &s->variants[i];
      if (v->payload_count <= 1) {
        const SemaType *payload = sema_single_payload(v);
        if (!payload || payload->kind == SEMA_UNIT)
          continue;
        fputs("    ", out);
        c_type(out, payload);
        fprintf(out, " v%zu;\n", v->tag->id);
      } else {
        fputs("    struct {\n", out);
        size_t fields = 0;
        for (size_t j = 0; j < v->payload_count; j++) {
          if (v->payloads[j]->kind == SEMA_UNIT)
            continue;
          fputs("      ", out);
          c_type(out, v->payloads[j]);
          fprintf(out, " p%zu;\n", j);
          fields++;
        }
        if (!fields)
          fputs("      uint8_t empty;\n", out);
        fprintf(out, "    } v%zu;\n", v->tag->id);
      }
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
  for (size_t set = 0; set < p->symbol_set_count; set++) {
    const SemaSymSet *s = p->symbol_sets[set];
    for (int retain = 0; retain <= 1; retain++) {
      fprintf(out,
              "void brt_sym_%zu_%s(bry_%zu value) {\n  switch (value.tag) {\n",
              s->id, retain ? "retain" : "release", s->id);
      for (size_t i = 0; i < s->count; i++) {
        const SemaVariant *v = &s->variants[i];
        int started = 0;
        for (size_t j = 0; j < v->payload_count; j++) {
          const SemaType *payload = v->payloads[j];
          if (!sema_reference(payload))
            continue;
          if (!started)
            fprintf(out, "  case %zu: ", v->tag->id);
          else
            fputc(' ', out);
          char value[128];
          c_payload_name(value, sizeof(value), "value", v, j);
          c_ref_prefix(out, payload, retain);
          fprintf(out, "%s);", value);
          started = 1;
        }
        if (started)
          fputs(" break;\n", out);
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
      size_t values = 0;
      for (size_t j = 0; j < v->payload_count; j++) {
        if (v->payloads[j]->kind == SEMA_UNIT)
          continue;
        if (values++)
          fputs(" && ", out);
        char a[128], b[128];
        c_payload_name(a, sizeof(a), "a", v, j);
        c_payload_name(b, sizeof(b), "b", v, j);
        c_equal(out, v->payloads[j], a, b);
      }
      if (!values)
        fputs("1", out);
      fputs(";\n", out);
    }
    fputs("  default: return 1;\n  }\n}\n", out);
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
              "  --*budget; fputs(\"%s%s = \", out); ",
              j ? ", " : " ", j ? ", " : " ", f->name);
      char value[96];
      snprintf(value, sizeof(value), "value->brt_field_%zu", j);
      c_write_value(out, f->type, value);
      fputs(";\n", out);
    }
    fprintf(out, "  fputs(\"%s}\", out);\n}\n", s->count ? " " : "");
  }
  for (size_t set = 0; set < p->symbol_set_count; set++) {
    const SemaSymSet *s = p->symbol_sets[set];
    fprintf(out,
            "void brt_sym_%zu_write(FILE *out, bry_%zu value, unsigned depth, "
            "size_t *budget) {\n"
            "  if (depth >= 32 || !*budget) { fputs(\"...\", out); return; }\n"
            "  --*budget;\n"
            "  switch (value.tag) {\n",
            s->id, s->id);
    for (size_t i = 0; i < s->count; i++) {
      const SemaVariant *v = &s->variants[i];
      fprintf(out, "  case %zu: fputs(\":%s%s\", out);", v->tag->id,
              v->tag->kind == SEMA_TAG_ERROR ? "!" : "", v->tag->name);
      if (v->payload_count) {
        fputs(" fputc('[', out); ", out);
        for (size_t j = 0; j < v->payload_count; j++) {
          if (j)
            fputs("; fputs(\", \", out); ", out);
          char value[128];
          c_payload_name(value, sizeof(value), "value", v, j);
          c_write_value(out, v->payloads[j], value);
        }
        fputs("; fputc(']', out);", out);
      }
      fputs(" break;\n", out);
    }
    fputs("  default: fputs(\"<invalid symbol>\", out); break;\n  }\n}\n", out);
  }
}

static void struct_declaration(FILE *out, const SemaStruct *s) {
  fprintf(out, "struct brs_%s {\n  brt_object brt_header;\n", s->type.name);
  for (size_t j = 0; j < s->count; j++) {
    if (s->fields[j].type->kind == SEMA_UNIT)
      continue;
    fputs("  ", out);
    c_type(out, s->fields[j].type);
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

// Destructor, constructor and (when equatable) structural equality.
static void struct_helpers(FILE *out, const SemaStruct *s) {
  const char *name = s->type.name;
  fprintf(out,
          "void brt_struct_%s_destroy(brt_object *object) {\n  brs_%s *value "
          "= (brs_%s *)object;\n  (void)value;\n",
          name, name, name);
  for (size_t j = 0; j < s->count; j++)
    if (sema_reference(s->fields[j].type)) {
      fputs("  ", out);
      c_ref_prefix(out, s->fields[j].type, 0);
      fprintf(out, "value->brt_field_%zu);\n", j);
    }
  fprintf(out, "}\nbrs_%s *brt_struct_%s_new(", name, name);
  int comma = 0;
  for (size_t j = 0; j < s->count; j++) {
    if (s->fields[j].type->kind == SEMA_UNIT)
      continue;
    if (comma)
      fputs(", ", out);
    c_type(out, s->fields[j].type);
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
      c_ref_prefix(out, s->fields[j].type, 1);
      fprintf(out, "f%zu);\n", j);
    }
    fprintf(out, "  value->brt_field_%zu = f%zu;\n", j, j);
  }
  fputs("  return value;\n}\n", out);
  if (!s->equatable)
    return;
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
    c_equal(out, t, a, b);
  }
  fputs(";\n}\n", out);
}

void c_structures(FILE *out, const IrProgram *p) {
  for (size_t i = 0; i < p->struct_count; i++)
    fprintf(out, "typedef struct brs_%s brs_%s;\n", p->structs[i].type.name,
            p->structs[i].type.name);
  symbol_declarations(out, p);
  for (size_t i = 0; i < p->struct_count; i++)
    struct_declaration(out, &p->structs[i]);
  symbol_helpers(out, p);
  formatters(out, p);
  for (size_t i = 0; i < p->struct_count; i++)
    struct_helpers(out, &p->structs[i]);
}
