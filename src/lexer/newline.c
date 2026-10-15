#include "lexer/newline.h"

static int name_token(TokenType t) {
  return t == TOK_IDENT || (t >= TOK_BOOL && t <= TOK_SELF);
}

// Distinguish a type argument's closing > from a comparison/shift. Without
// this, `field: Sym<:ok(int)>\nnext: int` would lose its field separator.
// This is only lexical context, not generic-call disambiguation (the parser
// does that with a speculative type parse).
static size_t previous_token(TokenArray raw, size_t before) {
  while (before && raw.items[before - 1].type == TOK_NEWLINE)
    before--;
  return before;
}

static int type_angle(TokenArray raw, size_t i) {
  size_t before = previous_token(raw, i);
  if (!before)
    return 0;
  TokenType base = raw.items[before - 1].type;
  if (base == TOK_SYM || base == TOK_FN)
    return 1;
  if (!name_token(base))
    return 0;
  before = previous_token(raw, before - 1);
  while (before && raw.items[before - 1].type == TOK_PERIOD) {
    before = previous_token(raw, before - 1);
    if (!before || !name_token(raw.items[before - 1].type))
      return 0;
    before = previous_token(raw, before - 1);
  }
  if (before && raw.items[before - 1].type == TOK_BIT_AND)
    before = previous_token(raw, before - 1);
  if (!before)
    return 0;
  TokenType context = raw.items[before - 1].type;
  if (context == TOK_IMPL)
    return 1;
  if (context == TOK_COLON) {
    // A symbol's prefix colon is not a type annotation.
    before = previous_token(raw, before - 1);
    return before && (raw.items[before - 1].type == TOK_IDENT ||
                      raw.items[before - 1].type == TOK_RPAREN);
  }
  if (context != TOK_EQUAL)
    return 0;
  before = previous_token(raw, before - 1); // alias name
  if (!before || raw.items[before - 1].type != TOK_IDENT)
    return 0;
  before = previous_token(raw, before - 1);
  return before && raw.items[before - 1].type == TOK_TYPE;
}

static int continues(TokenType t, int type_close, int prefix_bang) {
  switch (t) {
  case TOK_GREATER_THAN:
  case TOK_RSHIFT:
    return !type_close;
  case TOK_BANG:
    return prefix_bang;
  case TOK_COMMA:
  case TOK_COLON:
  case TOK_PERIOD:
  case TOK_FAT_ARROW:
  case TOK_PIPE:
  case TOK_EQUAL:
  case TOK_PLUS:
  case TOK_MINUS:
  case TOK_STAR:
  case TOK_SLASH:
  case TOK_PERCENT:
  case TOK_STAR_STAR:
  case TOK_PLUS_EQUAL:
  case TOK_MINUS_EQUAL:
  case TOK_STAR_EQUAL:
  case TOK_SLASH_EQUAL:
  case TOK_PERCENT_EQUAL:
  case TOK_EQUAL_EQUAL:
  case TOK_BANG_EQUAL:
  case TOK_LESS_THAN:
  case TOK_LESS_THAN_EQUAL:
  case TOK_GREATER_THAN_EQUAL:
  case TOK_AND:
  case TOK_OR:
  case TOK_TILDE:
  case TOK_BIT_AND:
  case TOK_BIT_OR:
  case TOK_BIT_XOR:
  case TOK_LSHIFT:
  case TOK_DOT_DOT:
  case TOK_LET:
  case TOK_VAR:
  case TOK_CONST:
  case TOK_TYPE:
  case TOK_IMPL:
  case TOK_PUB:
  case TOK_IF:
  case TOK_ELSE:
  case TOK_FOR:
  case TOK_IN:
  case TOK_WHILE:
    return 1;
  default:
    return 0;
  }
}

static int can_end(TokenType t) {
  return name_token(t) || (t >= TOK_INT_LIT && t <= TOK_BOOL_LIT) ||
         t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE ||
         t == TOK_QUESTION || t == TOK_BANG || t == TOK_PLUS_PLUS ||
         t == TOK_MINUS_MINUS;
}

TokenArray newline_filter(Arena *arena, TokenArray raw) {
  TokenArray out = {.items = arena_calloc(arena, raw.count, sizeof(Token))};
  TokenType *brackets = arena_calloc(arena, raw.count, sizeof(*brackets));
  int *angles = arena_calloc(arena, raw.count, sizeof(*angles));
  size_t depth = 0, angle_depth = 0;
  int closed_type = 0, prefix_bang = 0;
  for (size_t i = 0; i < raw.count;) {
    Token token = raw.items[i];
    TokenType t = token.type;
    if (t == TOK_NEWLINE) {
      size_t end = i + 1;
      while (end < raw.count && raw.items[end].type == TOK_NEWLINE)
        end++;
      TokenType previous = out.count ? out.items[out.count - 1].type : TOK_EOF;
      TokenType next = end < raw.count ? raw.items[end].type : TOK_EOF;
      int enclosed = depth && brackets[depth - 1] != TOK_LBRACE;
      if (!enclosed && !continues(previous, closed_type, prefix_bang) &&
          next != TOK_PIPE && next != TOK_PERIOD) {
        for (; i < end; i++)
          out.items[out.count++] = raw.items[i];
        angle_depth = 0;
      }
      i = end;
      continue;
    }
    TokenType previous = out.count ? out.items[out.count - 1].type : TOK_EOF;
    prefix_bang = t == TOK_BANG && (!out.count || !can_end(previous) ||
                                    (previous == TOK_BANG && prefix_bang));
    closed_type = 0;
    // Logical operators cannot occur in a type argument list: a speculative
    // angle in a struct field's value was a comparison, not a generic type.
    if (t == TOK_AND || t == TOK_OR)
      for (size_t a = 0; a < angle_depth; a++)
        angles[a] = 0;
    if (t == TOK_LESS_THAN) {
      int known =
          (angle_depth && angles[angle_depth - 1]) || type_angle(raw, i);
      angles[angle_depth++] = known;
    } else if (t == TOK_GREATER_THAN || t == TOK_RSHIFT) {
      int n = t == TOK_RSHIFT ? 2 : 1;
      while (n-- && angle_depth)
        closed_type |= angles[--angle_depth];
    }
    if (t == TOK_LPAREN || t == TOK_LBRACKET || t == TOK_LBRACE)
      brackets[depth++] = t;
    else if (t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE) {
      TokenType wanted = t == TOK_RPAREN     ? TOK_LPAREN
                         : t == TOK_RBRACKET ? TOK_LBRACKET
                                             : TOK_LBRACE;
      size_t match = depth;
      while (match && brackets[match - 1] != wanted)
        match--;
      if (match)
        depth = match - 1;
    }
    out.items[out.count++] = token;
    i++;
  }
  return out;
}
