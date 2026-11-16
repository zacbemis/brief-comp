#include "lexer/lexer.h"
#include "parser/internal.h"

#include <string.h>

// Recovery accepts both name classes. Declaration roles validate the consumed
// token once their syntax is complete; use sites and patterns keep their rules.
int parser_expect_identifier(Parser *p, const char *message) {
  if (token_is_identifier(peek(p).type)) {
    take(p);
    return 1;
  }
  error(p, message);
  return 0;
}
// Primitive keywords remain reserved except as function member names or
// expression members after '.'. Do not widen ordinary names or type paths.
int parser_primitive_member_name(TokenType type) {
  return type >= TOK_BOOL && type <= TOK_STR;
}
int parser_expect_member_name(Parser *p, const char *message) {
  if (parser_primitive_member_name(peek(p).type)) {
    take(p);
    return 1;
  }
  return parser_expect_identifier(p, message);
}
// Checks a declared name or tag once its construct's syntax is complete, so
// broken syntax keeps its primary error alone. `mark` is the diagnostic count
// when the name was read: the error is placed there, before any errors from
// later source. No use-site lookup or reference rewriting happens here.
static const char *check_name_case_inner(Parser *p, Token token, size_t mark,
                                         TokenType wanted, const char *message,
                                         int primitive_member) {
  if (token.type == wanted ||
      (primitive_member && parser_primitive_member_name(token.type)))
    return NULL;
  Diagnostic *d = error_at(p, token.span, message);
  diagnostics_move_last(p->diagnostics, mark);
  if (!token_is_name(token.type))
    return NULL; // '_' has no certain replacement.
  char *fixed = arena_alloc(p->arena, token.length + 1);
  size_t count = 0;
  if (wanted == TOK_UPPER_NAME) {
    memcpy(fixed, token.start, token.length);
    fixed[0] = (char)(fixed[0] - 'a' + 'A');
    count = token.length;
  } else {
    int all_caps = 1;
    for (size_t i = 0; i < token.length; i++)
      if (token.start[i] >= 'a' && token.start[i] <= 'z')
        all_caps = 0;
    int capitalize = 0;
    for (size_t i = 0; i < token.length; i++) {
      char c = token.start[i];
      if (all_caps && c == '_' && i + 1 < token.length) {
        capitalize = 1;
        continue;
      }
      if ((all_caps || !i) && c >= 'A' && c <= 'Z' && !capitalize)
        c = (char)(c - 'A' + 'a');
      fixed[count++] = c;
      capitalize = 0;
    }
  }
  fixed[count] = 0;
  // Ask the lexer instead of maintaining a second keyword list. Ordinary
  // names must not become keywords; function members may name primitives.
  SourceFile *candidate = source_create(p->arena, "<name fix>", fixed, count);
  Lexer lexer;
  lexer_init(&lexer, candidate, NULL);
  Token checked = lexer_next_token(&lexer);
  if ((checked.type != wanted &&
       !(primitive_member && parser_primitive_member_name(checked.type))) ||
      checked.length != count)
    return NULL;
  diagnostic_fix(p->diagnostics, d, token.span, fixed);
  return fixed;
}
const char *parser_check_name_case(Parser *p, Token token, size_t mark,
                                   TokenType wanted, const char *message) {
  return check_name_case_inner(p, token, mark, wanted, message, 0);
}
void parser_check_function_name(Parser *p, Token token, size_t mark,
                                int in_member) {
  check_name_case_inner(p, token, mark, TOK_LOWER_NAME,
                        "Function names must start with a lowercase letter.",
                        in_member);
}
// ':' and '!' stay separate tokens; only an adjacent pair spells :!.
// Recovery moves the bang before trivia, preserving comments and line endings.
static int symbol_name(Parser *p, Token colon, SymbolKind *kind, Span *span,
                       const char *message) {
  Token bang = {0};
  *kind = SYMBOL_NEUTRAL;
  if (match(p, TOK_BANG)) {
    bang = p->previous;
    *kind = SYMBOL_ERROR;
  }
  if (!parser_expect_identifier(p, message))
    return 0;
  *span = through(colon.span, p->previous.span);
  if (*kind == SYMBOL_ERROR && (bang.span.file != colon.span.file ||
                                bang.span.start != colon.span.end)) {
    Diagnostic *d = error_at(p, through(colon.span, bang.span),
                             "The ':!' error-symbol prefix must be adjacent.");
    int blank = bang.span.file == colon.span.file;
    for (size_t i = colon.span.end; blank && i < bang.span.start; i++)
      blank = colon.span.file->text[i] &&
              strchr(" \t\r\n", colon.span.file->text[i]);
    if (blank) { // Close a whitespace-only gap: `: !bad` becomes `:!bad`.
      diagnostic_fix(p->diagnostics, d,
                     span_make(colon.span.file, colon.span.end, bang.span.end),
                     "!");
    } else { // Move the bang before comments, keeping them.
      diagnostic_fix(p->diagnostics, d, colon.span, ":!");
      diagnostic_fix(p->diagnostics, d, bang.span, "");
    }
  }
  return 1;
}
int parser_type_name_token(TokenType t) {
  return token_is_identifier(t) || (t >= TOK_BOOL && t <= TOK_STR);
}
const char *parser_join_path(Parser *p, const char *left, const char *right) {
  size_t a = strlen(left), b = strlen(right);
  char *joined = arena_alloc(p->arena, a + b + 2);
  memcpy(joined, left, a);
  joined[a] = '.';
  memcpy(joined + a + 1, right, b + 1);
  return intern(p->names, joined);
}

const char *parser_path(Parser *p, int qualifier) {
  if (!parser_type_name_token(peek(p).type)) {
    error(p, "Expected a name.");
    return NULL;
  }
  const char *result = name(p, take(p));
  while (at(p, TOK_PERIOD) &&
         !(qualifier && p->tokens.items[p->position + 1].type == TOK_COLON)) {
    take(p);
    if (!parser_expect_identifier(p, "Expected a name after '.'."))
      return NULL;
    result = parser_join_path(p, result, name(p, p->previous));
  }
  return result;
}

// Token-only recognition: a simple name path followed by '.:' is a symbol
// qualifier, never a receiver expression. Accepted parsing does not rewind.
static int qualified_symbol_head(Parser *p) {
  size_t i = p->position;
  if (!token_is_name(p->tokens.items[i].type))
    return 0;
  while (i + 2 < p->tokens.count && p->tokens.items[i + 1].type == TOK_PERIOD) {
    if (p->tokens.items[i + 2].type == TOK_COLON)
      return 1;
    if (!token_is_name(p->tokens.items[i + 2].type))
      return 0;
    i += 2;
  }
  return 0;
}
static Token symbol_prefix(Parser *p, SymbolHead *head) {
  if (!at(p, TOK_COLON)) {
    Token start = peek(p);
    head->module = parser_path(p, 1);
    head->leaf = p->previous;
    head->module_span = through(start.span, head->leaf.span);
    take(p); // The qualifier's final dot, recognized above.
  }
  return take(p); // ':'
}

int parser_at_symbol(Parser *p) {
  return at(p, TOK_COLON) || qualified_symbol_head(p);
}

int parser_symbol_head(Parser *p, SymbolHead *head, const char *message) {
  *head = (SymbolHead){.module_mark = p->diagnostics->count};
  Token colon = symbol_prefix(p, head);
  if (!symbol_name(p, colon, &head->kind, &head->tag_span, message))
    return 0;
  head->tag_token = p->previous;
  head->tag_mark = p->diagnostics->count;
  head->tag = name(p, head->tag_token);
  return 1;
}

void parser_check_symbol_head(Parser *p, const SymbolHead *head) {
  parser_check_name_case(p, head->tag_token, head->tag_mark, TOK_LOWER_NAME,
                         "Symbol tags must start with a lowercase letter.");
  if (!head->module)
    return;
  parser_check_name_case(p, head->leaf, head->module_mark, TOK_UPPER_NAME,
                         "Module names must start with an uppercase letter.");
  if (!strcmp(head->tag, "ok") || !strcmp(head->tag, "none"))
    error_at(p, head->module_span,
             "':ok' and ':none' cannot have a module qualifier.");
}

// `name: Type` binds only in a payload pattern. In a value or type entry, say
// so rather than reporting a generic missing ']' at the colon.
int parser_typed_binding_elsewhere(Parser *p, int name) {
  if (!name || !at(p, TOK_COLON))
    return 0;
  error_at(p, peek(p).span,
           "Typed bindings are only allowed in symbol payload patterns.");
  return 1;
}
