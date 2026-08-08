#ifndef AST_H
#define AST_H

#include <stddef.h>

#include "ast/decl.h"
#include "ast/expr.h"
#include "ast/pattern.h"
#include "ast/stmt.h"
#include "ast/type.h"

typedef struct Program Program;
typedef struct BlockItem BlockItem;

typedef enum {
  BLOCK_ITEM_DECL,
  BLOCK_ITEM_STMT,
  BLOCK_ITEM_EXPR,
} BlockItemKind;

struct Program {
  Decl **items;
  size_t count;
  size_t capacity;
};

#endif
