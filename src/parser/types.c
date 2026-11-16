#include "parser/internal.h"

int parse_type_arguments(Parser *p, List *args) {
  take(p); // [
  do {
    Type *arg = parse_type(p);
    if (!arg)
      return 0;
    PUSH(p, *args, arg);
    if (!match(p, TOK_COMMA))
      break;
  } while (!at(p, TOK_RBRACKET) && !at(p, TOK_EOF));
  return expect(p, TOK_RBRACKET, "Expected ']' after type arguments.");
}

// Accepted unions are flat lists; nested payload types stay separate.
static void union_entries(Parser *p, List *entries, Type *t) {
  if (t->kind == TYPE_UNION) {
    for (size_t i = 0; i < t->as.union_type.count; i++) {
      UnionEntry entry = t->as.union_type.items[i];
      PUSH(p, *entries, entry);
    }
  } else {
    UnionEntry entry = {.span = t->span, .type = t};
    PUSH(p, *entries, entry);
  }
}

static Type *type_atom(Parser *p) {
  Token start = peek(p);
  Type *t;
  if (match(p, TOK_LPAREN)) {
    if (!expect(p, TOK_RPAREN,
                "Expected ')' in the empty type; named tuples are not in the "
                "first version."))
      return NULL;
    t = new_type(p, TYPE_UNIT, through(start.span, p->previous.span));
  } else if (parser_at_symbol(p)) {
    SymbolHead head;
    if (!parser_symbol_head(p, &head, "Expected a symbol tag after ':'."))
      return NULL;
    UnionEntry entry = {.module = head.module,
                        .module_span = head.module_span,
                        .kind = head.kind,
                        .tag = head.tag,
                        .tag_span = head.tag_span};
    if (match(p, TOK_LBRACKET)) {
      List payloads = {0};
      if (!at(p, TOK_RBRACKET))
        do {
          Type *payload = parse_type(p);
          if (!payload ||
              parser_typed_binding_elsewhere(p, payload->kind == TYPE_NAME))
            return NULL;
          PUSH(p, payloads, payload);
          if (!match(p, TOK_COMMA) || at(p, TOK_RBRACKET))
            break;
        } while (!at(p, TOK_EOF));
      if (!expect(p, TOK_RBRACKET, "Expected ']' after symbol payload types."))
        return NULL;
      entry.payloads = payloads.data;
      entry.count = payloads.count;
    }
    parser_check_symbol_head(p, &head);
    entry.span = through(start.span, p->previous.span);
    t = new_type(p, TYPE_UNION, entry.span);
    t->as.union_type.items = arena_alloc(p->arena, sizeof(entry));
    t->as.union_type.items[0] = entry;
    t->as.union_type.count = 1;
  } else if (match(p, TOK_WILDCARD)) {
    t = new_type(p, TYPE_INFER, start.span);
  } else if (parser_type_name_token(start.type)) {
    const char *text = parser_path(p, 0);
    if (!text)
      return NULL;
    t = new_type(p, TYPE_NAME, through(start.span, p->previous.span));
    t->as.name = text;
    if (at(p, TOK_LBRACKET)) {
      List args = {0};
      if (!parse_type_arguments(p, &args))
        return NULL;
      Type *applied =
          new_type(p, TYPE_APPLY, through(start.span, p->previous.span));
      applied->as.apply.base = t;
      applied->as.apply.args = args.data;
      applied->as.apply.count = args.count;
      t = applied;
    }
  } else {
    error(p, "Expected a type.");
    return NULL;
  }
  return parser_checked_type(p, t);
}

static Type *type_inner(Parser *p) {
  Type *left = type_atom(p);
  if (!left || !match(p, TOK_BIT_OR))
    return left;
  List entries = {0};
  union_entries(p, &entries, left);
  do {
    Type *right = type_atom(p);
    if (!right)
      return NULL;
    union_entries(p, &entries, right);
  } while (match(p, TOK_BIT_OR));
  Type *t = new_type(p, TYPE_UNION, through(left->span, p->previous.span));
  t->as.union_type.items = entries.data;
  t->as.union_type.count = entries.count;
  return parser_checked_type(p, t);
}

Type *parse_type(Parser *p) {
  if (!parser_enter(p))
    return NULL;
  Type *result = type_inner(p);
  p->depth--;
  return result;
}
