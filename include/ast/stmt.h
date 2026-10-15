#ifndef AST_STMT_H
#define AST_STMT_H

typedef struct Stmt Stmt;

typedef enum {
  STMT_EXPR,
  STMT_ASSIGN,
  STMT_RET,
  STMT_BREAK,
  STMT_CONTINUE,
} StmtKind;

#endif
