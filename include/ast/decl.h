#ifndef AST_DECL_H
#define AST_DECL_H

typedef struct Decl Decl;

typedef enum {
  VISIBILITY_DEFAULT,
  VISIBILITY_PUB,
  VISIBILITY_PRIV,
} Visibility;

typedef enum {
  ACCESS_DEFAULT,
  ACCESS_MUT,
  ACCESS_SHR,
} AccessKind;

typedef enum {
  BIND_LET,
  BIND_VAR,
} BindingKind;

typedef enum {
  DECL_BINDING,
  DECL_FN,
  DECL_STRUCT,
  DECL_ENUM,
  DECL_MODULE,
} DeclKind;

#endif
