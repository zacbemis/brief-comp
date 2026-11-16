#ifndef BRIEF_CODEGEN_C_INTERNAL_H
#define BRIEF_CODEGEN_C_INTERNAL_H

#include "codegen/c.h"

// spelling.c: C spellings of Brief types, names and values.

void c_string_literal(FILE *out, const unsigned char *bytes, size_t length);
void c_line(FILE *out, Span span);
void c_type(FILE *out, const SemaType *type);
// Opens a retain/release call for a reference type; the caller closes it.
void c_ref_prefix(FILE *out, const SemaType *type, int retain);
void c_zero(FILE *out, const SemaType *type);
void c_equal(FILE *out, const SemaType *type, const char *a, const char *b);
void c_payload_name(char *out, size_t size, const char *value,
                    const SemaVariant *variant, size_t position);
void c_function_name(FILE *out, const IrFunction *f);
unsigned c_bits(SemaTypeKind kind);
int c_signed_integer(SemaTypeKind kind);
void c_signature(FILE *out, const IrFunction *f);
void c_write_value(FILE *out, const SemaType *t, const char *value);

// types.c: struct and symbol-set types with their runtime helpers.
void c_structures(FILE *out, const IrProgram *p);

// function.c: one function definition.
void c_function(FILE *out, const IrProgram *p, const IrFunction *f);

#endif
