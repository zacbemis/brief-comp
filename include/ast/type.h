#ifndef AST_TYPE_H
#define AST_TYPE_H

typedef struct Type Type;

typedef enum {
  TYPE_NAME,
  TYPE_APPLY,
  TYPE_ARRAY,
  TYPE_UNIT,
} TypeKind;

#endif
