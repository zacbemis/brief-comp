#include "codegen/c.h"
#include "test_utils.h"

int main(void) {
  Arena a;
  arena_init(&a);
  TEST("the independent RC pass pins reads, retains stores first, and "
       "transfers returns") {
    SemaType string = {.kind = SEMA_STR, .name = "str"};
    const SemaType *variables[] = {&string, &string},
                   *temporaries[] = {&string};
    IrInstruction instructions[] = {{.op = IR_LOAD, .result = 0, .a = 0},
                                    {.op = IR_STORE, .a = 1, .b = 0},
                                    {.op = IR_DROP_VAR, .a = 1},
                                    {.op = IR_RETURN, .a = 0}};
    IrFunction f = {.variables = variables,
                    .variable_count = 2,
                    .parameter_count = 1,
                    .temporary_types = temporaries,
                    .temporaries = 1,
                    .instructions = instructions,
                    .count = 4};
    IrProgram p = {.functions = &f, .count = 1};
    ir_insert_rc(&a, &p);
    ASSERT_TRUE(p.ownership_inserted);
    ASSERT_EQ_INT(8, f.count);
    ASSERT_EQ_INT(IR_DROP_TEMP, f.instructions[0].op); // Old loop-slot value.
    ASSERT_EQ_INT(IR_LOAD, f.instructions[1].op);
    ASSERT_EQ_INT(IR_RETAIN, f.instructions[2].op); // Pin the borrowed read.
    ASSERT_EQ_INT(IR_RETAIN, f.instructions[3].op); // Retain before overwrite.
    ASSERT_EQ_INT(IR_DROP_VAR, f.instructions[4].op);
    ASSERT_EQ_INT(1, f.instructions[4].a);
    ASSERT_EQ_INT(IR_STORE, f.instructions[5].op);
    ASSERT_EQ_INT(IR_DROP_VAR, f.instructions[6].op);
    ASSERT_EQ_INT(IR_RETURN, f.instructions[7].op);
    ASSERT_EQ_INT(0, f.instructions[7].a);
    // The borrowed parameter was not released; the owned result wasn't dropped.
    IrInstruction *once = f.instructions;
    ir_insert_rc(&a, &p);
    ASSERT_TRUE(f.instructions == once);
    ASSERT_EQ_INT(8, f.count);
  }
  TEST("field replacement retains its source before releasing the old child") {
    SemaType string = {.kind = SEMA_STR}, object = {.kind = SEMA_STRUCT};
    SemaField field = {.id = 3, .type = &string};
    const SemaType *temporaries[] = {&object, &string};
    IrInstruction instructions[] = {
        {.op = IR_FIELD_STORE, .a = 0, .b = 1, .field = &field}};
    IrFunction f = {.temporary_types = temporaries,
                    .temporaries = 2,
                    .instructions = instructions,
                    .count = 1};
    IrProgram p = {.functions = &f, .count = 1};
    ir_insert_rc(&a, &p);
    ASSERT_EQ_INT(3, f.count);
    ASSERT_EQ_INT(IR_RETAIN, f.instructions[0].op);
    ASSERT_EQ_INT(1, f.instructions[0].a);
    ASSERT_EQ_INT(IR_DROP_FIELD, f.instructions[1].op);
    ASSERT_TRUE(f.instructions[1].field == &field);
    ASSERT_EQ_INT(IR_FIELD_STORE, f.instructions[2].op);
  }
  TEST("reference self-copies are no-ops, not drop-before-retain") {
    SemaType string = {.kind = SEMA_STR};
    const SemaType *temporaries[] = {&string};
    IrInstruction in = {.op = IR_COPY, .a = 0, .result = 0};
    IrFunction f = {.temporary_types = temporaries,
                    .temporaries = 1,
                    .instructions = &in,
                    .count = 1};
    IrProgram p = {.functions = &f, .count = 1};
    ir_insert_rc(&a, &p);
    ASSERT_EQ_INT(0, f.count);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
