#ifndef AST_TYPE_H
#define AST_TYPE_H

typedef struct Type Type;

typedef enum {
  TYPE_NAME,
  TYPE_APPLY, // Vec<User>
  TYPE_ARRAY, // int[5]
  TYPE_SYM,   // Sym<:ok(User), :err(str)>
  TYPE_FN,    // Fn<(User): bool>
  TYPE_TUPLE, // (name: str, age: int)
  TYPE_SELF,  // Self
  TYPE_UNIT,  // ()
  TYPE_INFER, // _ in a partial Sym return type
} TypeKind;

#endif
