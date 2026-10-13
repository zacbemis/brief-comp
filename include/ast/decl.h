#ifndef AST_DECL_H
#define AST_DECL_H

typedef struct Decl Decl;

// Private to the enclosing module unless marked pub.
typedef enum {
  VISIBILITY_PRIV,
  VISIBILITY_PUB,
} Visibility;

// How a parameter borrows its argument.
typedef enum {
  BORROW_IMMUT, // user: User
  BORROW_MUT,   // user: &User
} BorrowKind;

typedef enum {
  BIND_LET,   // immutable binding, mutable data
  BIND_VAR,   // mutable binding, mutable data
  BIND_CONST, // immutable binding, immutable data
} BindingKind;

typedef enum {
  DECL_BINDING,
  DECL_FN,
  DECL_STRUCT,
  DECL_ENUM,
  DECL_MODULE,
  DECL_TRAIT,
  DECL_IMPL,
  DECL_IMPORT,
} DeclKind;

#endif
