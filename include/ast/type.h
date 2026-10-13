#ifndef AST_TYPE_H
#define AST_TYPE_H

typedef struct Type Type;

typedef enum {
  TYPE_NAME,
  TYPE_APPLY, // Vec<User>
  TYPE_ARRAY, // int[5]
  TYPE_SYM,   // Sym<:ok(User), :err(str)>
  TYPE_SELF,  // Self
} TypeKind;

#endif
