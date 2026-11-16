#include "parser/internal.h"

static int parameter(Parser *p, Parameter *param, int field) {
  *param = (Parameter){.span = peek(p).span};
  if (field && match(p, TOK_PUB))
    param->visibility = VISIBILITY_PUB;
  if (field && at(p, TOK_IMPORT)) {
    error(p, "Imports are only allowed at the top level.");
    return 0;
  }
  if (!parser_expect_identifier(p, field ? "Expected a field name."
                                         : "Expected a parameter name."))
    return 0;
  Token declared = p->previous;
  size_t mark = p->diagnostics->count;
  param->name = name(p, declared);
  if (!expect(p, TOK_COLON, "Expected ':' before type."))
    return 0;
  if (!field && match(p, TOK_BIT_AND))
    param->borrow = BORROW_MUT;
  param->type = parse_type(p);
  if (!param->type)
    return 0;
  param->span = through(param->span, param->type->span);
  parser_check_name_case(
      p, declared, mark, TOK_LOWER_NAME,
      field ? "Field names must start with a lowercase letter."
            : "Parameter names must start with a lowercase letter.");
  return 1;
}

static const char *top_level_statement_message =
    "Statements are only allowed inside a function.";

// Only diagnose statement-shaped input. Typed parameters, a following body or
// return annotation still select declaration parsing, including its specific
// malformed-header errors. This lookahead never consumes or splits tokens.
static int top_level_statement(Parser *p) {
  TokenType first = peek(p).type;
  if (first >= TOK_INT_LIT && first <= TOK_BOOL_LIT)
    return 1;
  int type_keyword = first >= TOK_BOOL && first <= TOK_STR;
  switch (first) {
  case TOK_LET:
  case TOK_VAR:
  case TOK_RET:
  case TOK_BREAK:
  case TOK_CONTINUE:
  case TOK_IF:
  case TOK_WHILE:
  case TOK_FOR:
  case TOK_LPAREN:
  case TOK_COLON:
  case TOK_MINUS:
  case TOK_NOT:
  case TOK_TILDE:
    return 1;
  case TOK_UPPER_NAME:
  case TOK_LOWER_NAME:
  case TOK_WILDCARD:
    break;
  default:
    if (!type_keyword)
      return 0;
    break;
  }
  TokenType next = p->tokens.items[p->position + 1].type;
  if (next != TOK_LPAREN)
    return !type_keyword &&
           (next == TOK_NEWLINE || next == TOK_EOF || next == TOK_PERIOD ||
            next == TOK_LBRACKET || next == TOK_DOT_LPAREN ||
            next == TOK_QUESTION || next == TOK_PIPE ||
            (next >= TOK_EQUAL && next <= TOK_RSHIFT));
  size_t parens = 0, braces = 0, brackets = 0;
  for (size_t i = p->position + 1; i < p->tokens.count; i++) {
    TokenType t = p->tokens.items[i].type;
    if (t == TOK_EOF || t == TOK_ERR)
      return 0;
    if (t == TOK_COLON && parens == 1 && !braces && !brackets &&
        token_is_identifier(p->tokens.items[i - 1].type))
      return 0;
    if (t == TOK_LPAREN || t == TOK_DOT_LPAREN)
      parens++;
    else if (t == TOK_RPAREN) {
      if (!--parens) {
        if (braces || brackets)
          return 0;
        // A body/annotation on the wrong line is still a declaration error.
        do {
          next = p->tokens.items[++i].type;
        } while (next == TOK_NEWLINE);
        return next != TOK_LBRACE && next != TOK_ARROW;
      }
    } else if (t == TOK_LBRACE)
      braces++;
    else if (t == TOK_RBRACE) {
      if (!braces)
        return 0;
      braces--;
    } else if (t == TOK_LBRACKET)
      brackets++;
    else if (t == TOK_RBRACKET) {
      if (!brackets)
        return 0;
      brackets--;
    }
  }
  return 0;
}

// Remove a rejected declaration modifier without swallowing comments/newlines.
static void remove_modifier(Parser *p, Diagnostic *d, Token token) {
  Span span = token.span;
  if (!d || !span.file)
    return;
  const char *text = span.file->text;
  while (span.end < span.file->length &&
         (text[span.end] == ' ' || text[span.end] == '\t'))
    span.end++;
  // A modifier alone on its line takes the line break too, so the fix (and its
  // preview) joins the declaration instead of leaving an empty line.
  size_t before = span.start;
  while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t'))
    before--;
  if (before == 0 || text[before - 1] == '\n') {
    if (span.end + 1 < span.file->length && text[span.end] == '\r' &&
        text[span.end + 1] == '\n')
      span.end += 2;
    else if (span.end < span.file->length && text[span.end] == '\n')
      span.end++;
  }
  diagnostic_fix(p->diagnostics, d, span, "");
}
static Decl *declaration(Parser *p, int in_member);

static Decl *import_declaration(Parser *p, Token start, Visibility visibility,
                                int in_member) {
  Token keyword = p->previous;
  if (in_member) {
    error_at(p, keyword.span, "Imports are only allowed at the top level.");
    return NULL;
  }
  if (!token_is_name(peek(p).type)) {
    error(p, "Expected a module path after 'import'.");
    return NULL;
  }
  Token leaf = take(p);
  const char *selected = name(p, leaf);
  while (match(p, TOK_PERIOD)) {
    if (!token_is_name(peek(p).type)) {
      error(p, "Expected a name after '.' in an import path.");
      return NULL;
    }
    leaf = take(p);
    selected = parser_join_path(p, selected, name(p, leaf));
  }
  Decl *d =
      new_decl(p, DECL_IMPORT, through(start.span, leaf.span), VISIBILITY_PRIV);
  d->name = selected;
  // Let the declaration separator own malformed continuations before offering
  // visibility/casing edits for an otherwise complete import.
  if (!at(p, TOK_NEWLINE) && !at(p, TOK_EOF))
    return d;
  if (visibility == VISIBILITY_PUB)
    remove_modifier(
        p, error_at(p, start.span, "Imports cannot be marked 'pub'."), start);
  parser_check_name_case(p, leaf, p->diagnostics->count, TOK_UPPER_NAME,
                         "Module names must start with an uppercase letter.");
  return d;
}

static Decl *alias_declaration(Parser *p, Token start, Visibility visibility) {
  Decl *d = new_decl(p, DECL_TYPE, start.span, VISIBILITY_PRIV);
  d->name = name(p, take(p));
  take(p); // =
  d->as.alias = parse_type(p);
  if (!d->as.alias)
    return NULL;
  if (visibility == VISIBILITY_PUB)
    remove_modifier(
        p, error_at(p, start.span, "Type aliases cannot be marked 'pub'."),
        start);
  d->span = through(start.span, d->as.alias->span);
  return d;
}

static int conformances(Parser *p, Decl *d) {
  Token colon = p->previous;
  List traits = {0};
  do {
    if (!token_is_name(peek(p).type)) {
      error(p, "Expected a named trait in conformance list.");
      return 0;
    }
    size_t before = p->diagnostics->count;
    Type *trait = parse_type(p);
    if (!trait)
      return 0;
    if (trait->kind != TYPE_NAME && trait->kind != TYPE_APPLY) {
      if (p->diagnostics->count == before)
        error_at(p, trait->span, "Expected a named trait in conformance list.");
      return 0;
    }
    PUSH(p, traits, trait);
  } while (match(p, TOK_AND));
  d->as.structure.conformances = traits.data;
  d->as.structure.conformance_count = traits.count;
  d->as.structure.conformance_span = through(colon.span, p->previous.span);
  return 1;
}

// Members distinguish a field from a function by the token after its name.
static int member_function_head(Parser *p) {
  size_t i = p->position;
  if (p->tokens.items[i].type == TOK_PUB)
    i++;
  if (i + 1 >= p->tokens.count)
    return 0;
  Token name = p->tokens.items[i];
  return (token_is_identifier(name.type) ||
          parser_primitive_member_name(name.type)) &&
         p->tokens.items[i + 1].type == TOK_LPAREN;
}

// Returns whether every member parsed; failed members are skipped.
static int members(Parser *p, Decl *d) {
  List fields = {0}, functions = {0};
  int valid_body = 1;
  newlines(p);
  while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    size_t checkpoint = p->position;
    int valid;
    if (member_function_head(p)) {
      Decl *function = declaration(p, 1);
      valid = function != NULL;
      if (function)
        PUSH(p, functions, function);
    } else {
      Parameter field;
      valid = parameter(p, &field, 1);
      if (valid)
        PUSH(p, fields, field);
    }
    if (!valid || !parser_separator(p, TOK_RBRACE)) {
      valid_body = 0;
      parser_recover(p, checkpoint, 1);
    }
    newlines(p);
  }
  d->as.structure.fields = fields.data;
  d->as.structure.count = fields.count;
  d->as.structure.functions = functions.data;
  d->as.structure.function_count = functions.count;
  return valid_body;
}

// A body with fields is a struct; a body of only functions is a trait.
static void classify_members(Parser *p, Decl *d, int valid_body) {
  Decl **functions = d->as.structure.functions;
  size_t count = d->as.structure.function_count;
  if (d->as.structure.count) {
    for (size_t i = 0; i < count; i++) {
      if (!functions[i]->as.function.body)
        error_at(p, functions[i]->span, "Struct functions require a body.");
      if (functions[i]->as.function.receiver == RECEIVER_SELF)
        error_at(p, functions[i]->as.function.receiver_span,
                 "Plain 'self' parameters are only allowed in traits.");
    }
    return;
  }
  if (!valid_body)
    return;
  for (size_t i = 0; i < count; i++)
    if (!functions[i]->as.function.body &&
        !functions[i]->as.function.return_type) {
      error_at(p, functions[i]->span,
               "Trait signatures require a return type.");
      valid_body = 0;
    }
  // Classify only complete, valid bodies. A failed member keeps its primary
  // syntax diagnostic and a recovery struct, never a guessed trait.
  if (valid_body)
    d->kind = DECL_TRAIT;
}

static Decl *struct_declaration(Parser *p, Token start, Visibility visibility,
                                Token declared, size_t mark) {
  Decl *d = new_decl(p, DECL_STRUCT, start.span, VISIBILITY_PRIV);
  d->name = name(p, declared);
  if (match(p, TOK_COLON) && !conformances(p, d))
    return NULL;
  if (!expect(p, TOK_LBRACE, "Expected '{' after conformance list."))
    return NULL;
  int valid_body = members(p, d);
  if (!expect(p, TOK_RBRACE, "Expected '}' after struct fields."))
    return NULL;
  classify_members(p, d, valid_body);
  parser_check_name_case(p, declared, mark, TOK_UPPER_NAME,
                         "Type names must start with an uppercase letter.");
  if (visibility == VISIBILITY_PUB)
    remove_modifier(p,
                    error_at(p, start.span,
                             d->kind == DECL_TRAIT
                                 ? "Trait types cannot be marked 'pub'."
                                 : "Struct types cannot be marked 'pub'."),
                    start);
  d->span = through(start.span, p->previous.span);
  return d;
}

static int receiver(Parser *p, Decl *d, int in_member, size_t params) {
  Token first = take(p);
  int mutable = first.type == TOK_BIT_AND;
  if (mutable)
    take(p); // self, recognized without consuming an ordinary name.
  Span span = through(first.span, p->previous.span);
  if (!in_member) {
    error_at(p, span,
             "Receiver parameters are only allowed in struct or trait "
             "functions.");
    return 0;
  }
  if (params || d->as.function.receiver != RECEIVER_NONE) {
    error_at(p, span,
             "A receiver must be the first and only receiver parameter.");
    return 0;
  }
  if (at(p, TOK_COLON)) {
    error_at(p, span, "Receiver parameters have no written type.");
    return 0;
  }
  d->as.function.receiver = mutable ? RECEIVER_MUT_SELF : RECEIVER_SELF;
  d->as.function.receiver_span = span;
  return 1;
}

static int parameters(Parser *p, Decl *d, int in_member) {
  List params = {0};
  if (!at(p, TOK_RPAREN)) {
    do {
      if (at(p, TOK_SELF) ||
          (at(p, TOK_BIT_AND) &&
           p->tokens.items[p->position + 1].type == TOK_SELF)) {
        if (!receiver(p, d, in_member, params.count))
          return 0;
      } else {
        Parameter param;
        if (!parameter(p, &param, 0))
          return 0;
        PUSH(p, params, param);
      }
      if (!match(p, TOK_COMMA))
        break;
      if (at(p, TOK_RPAREN))
        break;
    } while (!at(p, TOK_EOF));
  }
  d->as.function.params = params.data;
  d->as.function.count = params.count;
  return 1;
}

static Decl *function_declaration(Parser *p, Token start, Visibility visibility,
                                  int in_member, Token declared, size_t mark) {
  if (!expect(p, TOK_LPAREN,
              "Expected '(' after function name (or '{' for a struct)."))
    return NULL;
  Decl *d = new_decl(p, DECL_FN, start.span, visibility);
  d->name = name(p, declared);
  if (!parameters(p, d, in_member))
    return NULL;
  if (!expect(p, TOK_RPAREN, "Expected ')' after parameters."))
    return NULL;
  if (match(p, TOK_ARROW)) {
    d->as.function.return_type = parse_type(p);
    if (!d->as.function.return_type)
      return NULL;
  }
  if (!in_member || at(p, TOK_LBRACE)) {
    d->as.function.body = parse_block(p);
    if (!d->as.function.body)
      return NULL;
  } else if (!at(p, TOK_NEWLINE) && !at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
    error(p, "Expected a function body or a trait signature separator.");
    return NULL;
  }
  d->span = through(start.span, p->previous.span);
  parser_check_function_name(p, declared, mark, in_member);
  return d;
}

static Decl *declaration(Parser *p, int in_member) {
  Token start = peek(p);
  Visibility visibility = match(p, TOK_PUB) ? VISIBILITY_PUB : VISIBILITY_PRIV;
  if (match(p, TOK_IMPORT))
    return import_declaration(p, start, visibility, in_member);
  if (at(p, TOK_CONST)) {
    Decl *d = parse_binding(p, visibility);
    if (d)
      d->span = through(start.span, d->span);
    return d;
  }
  if (!in_member && at(p, TOK_UPPER_NAME) &&
      p->tokens.items[p->position + 1].type == TOK_EQUAL)
    return alias_declaration(p, start, visibility);
  if (!in_member && visibility == VISIBILITY_PRIV && top_level_statement(p)) {
    error(p, top_level_statement_message);
    return NULL;
  }
  if (in_member) {
    if (!parser_expect_member_name(p, "Expected a function declaration."))
      return NULL;
  } else if (!parser_expect_identifier(
                 p,
                 "Expected a declaration (function, struct, or named type)."))
    return NULL;
  Token declared = p->previous;
  size_t mark = p->diagnostics->count;
  if (!in_member && (at(p, TOK_LBRACE) ||
                     (declared.type == TOK_UPPER_NAME && at(p, TOK_COLON))))
    return struct_declaration(p, start, visibility, declared, mark);
  return function_declaration(p, start, visibility, in_member, declared, mark);
}

Program *parse_program(Arena *arena, InternTable *names, TokenArray tokens,
                       Diagnostics *diagnostics) {
  Program *program = arena_calloc(arena, 1, sizeof(*program));
  if (!tokens.count || tokens.items[tokens.count - 1].type != TOK_EOF) {
    diagnostics_error(diagnostics, span_make(NULL, 0, 0),
                      "Parser input must end in EOF.");
    return program;
  }
  Parser p = {.arena = arena,
              .names = names,
              .tokens = tokens,
              .diagnostics = diagnostics};
  program->span =
      through(tokens.items[0].span, tokens.items[tokens.count - 1].span);
  List declarations = {0};
  newlines(&p);
  while (!at(&p, TOK_EOF)) {
    size_t checkpoint = p.position;
    Decl *d = match(&p, TOK_ERR) ? NULL : declaration(&p, 0);
    if (d)
      PUSH(&p, declarations, d);
    if (!d || !parser_separator(&p, TOK_EOF))
      parser_recover(&p, checkpoint, 0);
    newlines(&p);
  }
  program->items = declarations.data;
  program->count = declarations.count;
  return program;
}
