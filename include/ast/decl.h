#ifndef AST_DECL_H
#define AST_DECL_H

typedef struct Decl Decl;

// Visibility of functions, fields and bindings. Types are inherently public;
// their Decl.visibility only records the absence of a written pub modifier.
typedef enum {
  VISIBILITY_PRIV,
  VISIBILITY_PUB,
} Visibility;

// How a parameter borrows its argument.
typedef enum {
  BORROW_IMMUT, // user: User
  BORROW_MUT,   // user: &User
} BorrowKind;

// Receiver form: written syntax in the AST, inferred/bound form in sema.
typedef enum {
  RECEIVER_NONE,
  RECEIVER_SELF,     // Readonly; the written plain-self form is trait-only.
  RECEIVER_MUT_SELF, // &self in a struct or trait.
} ReceiverKind;

typedef enum {
  BIND_LET,   // immutable binding, mutable data
  BIND_VAR,   // mutable binding, mutable data
  BIND_CONST, // immutable binding, immutable data
} BindingKind;

typedef enum {
  DECL_BINDING,
  DECL_FN,
  DECL_STRUCT,
  DECL_TYPE, // Error = :!bad[str] | :!missing
  DECL_MODULE,
  DECL_TRAIT, // Fieldless declaration; native support remains unavailable.
  DECL_IMPORT,
} DeclKind;

#endif
