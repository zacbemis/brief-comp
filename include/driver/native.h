#ifndef BRIEF_DRIVER_NATIVE_H
#define BRIEF_DRIVER_NATIVE_H

#include "ir/ir.h"

// POSIX host driver. CC is a single executable name/path, never shell text.
// build publishes atomically in output's directory; run uses TMPDIR and
// propagates the program's status (128 + signal when terminated by a signal).
int native_build(Arena *arena, const IrProgram *program, const char *output,
                 int debug, int run);

#endif
