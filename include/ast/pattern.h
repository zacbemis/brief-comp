#ifndef AST_PATTERN_H
#define AST_PATTERN_H

typedef struct Pattern Pattern;

typedef enum {
  PAT_BIND,
  PAT_WILDCARD,
  PAT_LITERAL,
  PAT_SYMBOL,  // :ok(user)
  PAT_RANGE,   // 18..<100
  PAT_COMPARE, // < 18
} PatternKind;

#endif
