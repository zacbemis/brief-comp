#ifndef AST_TYPE_H
#define AST_TYPE_H

typedef struct Type Type;

typedef enum {
  TYPE_NAME,
  TYPE_APPLY, // Vec[User]
  TYPE_UNION, // :ok[User] | :!err[str]
  TYPE_FN,    // Fn<(User): bool>
  TYPE_TUPLE, // (name: str, age: int)
  TYPE_UNIT,  // ()
  TYPE_INFER, // _ in a partial union return type
} TypeKind;

#endif
