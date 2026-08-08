#ifndef AST_PATTERN_H
#define AST_PATTERN_H

typedef struct Pattern Pattern;

typedef enum {
  PAT_BIND,
  PAT_WILDCARD,
  PAT_LITERAL,
  PAT_VARIANT,
  PAT_TUPLE,
} PatternKind;

#endif
