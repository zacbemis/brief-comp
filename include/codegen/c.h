#ifndef BRIEF_CODEGEN_C_H
#define BRIEF_CODEGEN_C_H

#include "ir/ir.h"
#include <stdio.h>

// Emits a self-contained C11 translation unit. Returns 0 on an output error.
int emit_c(FILE *output, const IrProgram *program);

#endif
