#include "ir/ir.h"

void ir_insert_rc(Arena *arena, IrProgram *p) {
  if (p->ownership_inserted)
    return;
  for (size_t i = 0; i < p->count; i++) {
    IrFunction *f = &p->functions[i];
    // At most retain + drop + original + retain for each input instruction.
    // Let arena_calloc check the byte-size product, without pre-multiplying
    // the instruction count (which could wrap before reaching the arena).
    IrInstruction *out = arena_calloc(arena, f->count, 4 * sizeof(*out));
    size_t count = 0;
    for (size_t j = 0; j < f->count; j++) {
      IrInstruction in = f->instructions[j];
      if (in.op == IR_COPY && in.result == in.a)
        continue;
      int store = (in.op == IR_STORE && sema_reference(f->variables[in.a])) ||
                  (in.op == IR_FIELD_STORE && sema_reference(in.field->type));
      if (store) {
        out[count++] =
            (IrInstruction){.op = IR_RETAIN, .span = in.span, .a = in.b};
        out[count++] = (IrInstruction){.op = in.op == IR_STORE ? IR_DROP_VAR
                                                               : IR_DROP_FIELD,
                                       .span = in.span,
                                       .a = in.a,
                                       .field = in.field};
      }
      int defines = in.op == IR_LOAD || in.op == IR_COPY ||
                    in.op == IR_FIELD_LOAD || in.op == IR_STRING ||
                    in.op == IR_CALL || in.op == IR_NEW || in.op == IR_BINARY ||
                    in.op == IR_UNARY || in.op == IR_CONSTANT ||
                    in.op == IR_SYMBOL || in.op == IR_SYMBOL_CAST ||
                    in.op == IR_SYMBOL_TAG || in.op == IR_SYMBOL_PAYLOAD;
      int reference = defines && in.result != IR_NO_VALUE &&
                      sema_reference(f->temporary_types[in.result]);
      // Every temporary slot is owned and may be reused on a loop backedge.
      // Sources live in separate owned slots, so dropping the old result here
      // cannot invalidate a read, a call argument, or a constructor field.
      if (reference)
        out[count++] = (IrInstruction){
            .op = IR_DROP_TEMP, .span = in.span, .a = in.result};
      out[count++] = in;
      if (reference && (in.op == IR_LOAD || in.op == IR_COPY ||
                        in.op == IR_FIELD_LOAD || in.op == IR_SYMBOL ||
                        in.op == IR_SYMBOL_CAST || in.op == IR_SYMBOL_PAYLOAD))
        out[count++] =
            (IrInstruction){.op = IR_RETAIN, .span = in.span, .a = in.result};
    }
    f->instructions = out;
    f->count = count;
    f->capacity = f->count;
  }
  p->ownership_inserted = 1;
}
