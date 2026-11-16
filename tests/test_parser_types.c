#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST(
      "type members retain functions, fields and trait signatures separately") {
    program_dump(
        &arena,
        "P { pub make(n: int) -> P { P { n = n } }\npub n: int\nread(p: P) { "
        "p.n } }\n"
        "T { required(n: int) -> int\ndefault(n: int) -> int { missing(n) } "
        "}\n",
        "(program (struct P (field pub n int) (fn pub make (params (n int)) P "
        "(block (struct-value (name P) (n (name n))))) (fn read (params (p P)) "
        "_ "
        "(block (field (name p) n)))) (trait T (fn required (params (n int)) "
        "int none) "
        "(fn default (params (n int)) int (block (call (name missing) (name "
        "n))))))\n");
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      const char *lf =
          "P {\npub\nread(p: &P) -> int { p.n }\nn: int\n}\nafter() {}\n";
      char text[512];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      ASSERT_EQ_INT(2, v->program->count);
      Decl *p = v->program->items[0];
      ASSERT_EQ_INT(1, p->as.structure.count);
      ASSERT_EQ_INT(1, p->as.structure.function_count);
      Decl *f = p->as.structure.functions[0];
      ASSERT_EQ_INT(VISIBILITY_PUB, f->visibility);
      ASSERT_EQ_INT(BORROW_MUT, f->as.function.params[0].borrow);
      ASSERT_EQ_INT((size_t)(strstr(text, "pub") - text), f->span.start);
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_EQ_INT(0, strcmp(dump(&arena, v->program), dump(&arena, next)));
      ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
    }
    invalid(&arena, "P { n: int\nread(p: P) -> int\n}",
            "Struct functions require a body.");
    invalid(&arena, "f() -> int\nmain() {}", "Expected '{' to start a block.");
    invalid(&arena, "T { required() }",
            "Trait signatures require a return type.");
    Parsed *bad_member = parse(&arena, "T { bad(x) {} }");
    ASSERT_EQ_INT(1, bad_member->diagnostics.count);
    ASSERT_EQ_INT(DECL_STRUCT, bad_member->program->items[0]->kind);
    char wide[32768];
    size_t width = (size_t)sprintf(wide, "P { n: int\n");
    for (unsigned i = 0; i < 512; i++)
      width += (size_t)sprintf(wide + width, "f%u() -> int { 1 }\n", i);
    sprintf(wide + width, "}\n");
    Parsed *many = parse(&arena, wide);
    ASSERT_EQ_INT(0, many->diagnostics.count);
    ASSERT_EQ_INT(512, many->program->items[0]->as.structure.function_count);
    Parsed *partial =
        parse(&arena, "P { n: int\nbad(x) {}\ngood() { 1 } }\nafter() {}\n");
    ASSERT_EQ_INT(1, partial->diagnostics.count);
    ASSERT_EQ_INT(1, partial->program->items[0]->as.structure.function_count);
    Parsed *pub_eof = parse(&arena, "P { pub");
    ASSERT_TRUE(pub_eof->diagnostics.count > 0);
    program_dump(
        &arena, "impl(impl: int) { impl }\n",
        "(program (fn impl (params (impl int)) _ (block (name impl))))\n");
  }
  TEST("primitive keywords name only function members and expression members") {
    const char *names[] = {"bool", "int", "uint", "shrt", "ushrt", "lng",
                           "ulng", "flt", "dbl",  "chr",  "str"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
      char text[512];
      snprintf(text, sizeof(text),
               "P { unit: ()\npub %s(value: %s) -> %s { value } }\nmain() { "
               "P.%s(1) }\n",
               names[i], names[i], names[i], names[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      Decl *member = v->program->items[0]->as.structure.functions[0];
      ASSERT_TRUE(member->name == intern(&v->names, names[i]));
      ASSERT_EQ_INT(VISIBILITY_PUB, member->visibility);
      Expr *call =
          v->program->items[1]->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(EXPR_FIELD, call->as.call.callee->kind);
      ASSERT_TRUE(call->as.call.callee->as.field.name == member->name);
      ASSERT_EQ_INT(TYPE_NAME, member->as.function.params[0].type->kind);
      ASSERT_TRUE(member->as.function.params[0].type->as.name == member->name);
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_TRUE(!memcmp(saved, filtered.items, bytes));
      ASSERT_EQ_INT(0, strcmp(dump(&arena, next), dump(&arena, v->program)));
      snprintf(text, sizeof(text), "%s() {}", names[i]);
      invalid(&arena, text, "Expected a declaration");
      snprintf(text, sizeof(text), "P { %s: int }", names[i]);
      invalid(&arena, text, "Expected a field name");
      snprintf(text, sizeof(text), "main() { let %s = 1 }", names[i]);
      invalid(&arena, text, "Expected a binding name");
    }
    expression_dump(&arena, "make().str(1).int[int](2)",
                    "(call (field (call (field (call (name make)) str) (int "
                    "1)) int) (types int) (int 2))");
    program_dump(&arena, "T { str(n: int) -> str\nint(n: int) -> int { n } }",
                 "(program (trait T (fn str (params (n int)) str none) (fn int "
                 "(params (n int)) int (block (name n)))))\n");
    const char *rejected[] = {"P { ret() {} }",     "P { Fn() {} }",
                              "main() { x.ret() }", "A = P.int",
                              "import P.int",       "E = P.int.:tag",
                              "main() { :int }",    "f(int: int) {}"};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); i++) {
      Parsed *v = parse(&arena, rejected[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
    }
    Parsed *colon = parse(&arena, "impl: int");
    ASSERT_EQ_INT(1, colon->diagnostics.count);
    ASSERT_EQ_INT(
        0, strcmp("Expected '(' after function name (or '{' for a struct).",
                  colon->diagnostics.items[0]->message));
    ASSERT_TRUE(!colon->diagnostics.items[0]->fixes);
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      const char *eol = crlf ? "\r\n" : "\n";
      char text[512];
      snprintf(text, sizeof(text),
               "P {%sunit: ()%spub INT/* keep */(n: int) -> int { n }%s}%s",
               eol, eol, eol, eol);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      char *fixed = apply_fixes(&arena, v);
      ASSERT_TRUE(strstr(fixed, "pub int/* keep */(n: int) -> int") != NULL);
      ASSERT_TRUE(strstr(fixed, crlf ? "{\r\nunit" : "{\nunit") != NULL);
      Parsed *next = parse(&arena, fixed);
      ASSERT_EQ_INT(0, next->diagnostics.count);
      ASSERT_TRUE(!strcmp(
          "INT", v->program->items[0]->as.structure.functions[0]->name));
      const char *bad[] = {"Str() {}", "P { Int: int }",
                           "main() { let Bool = true }", "P { Int() -> {} }"};
      for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        Parsed *b = parse(&arena, bad[i]);
        ASSERT_EQ_INT(1, b->diagnostics.count);
        ASSERT_TRUE(!b->diagnostics.items[0]->fixes);
      }
    }
  }
  TEST("conformance headers retain named traits without resolving them") {
    program_dump(&arena,
                 "P: Traits.Reader[int] and Writer { pub n: int\nread(p: P) -> "
                 "int { p.n } }\nEmpty: Marker {}\n",
                 "(program (struct P (conforms (apply Traits.Reader int) "
                 "Writer) (field pub n int) "
                 "(fn read (params (p P)) int (block (field (name p) n)))) "
                 "(trait Empty (conforms Marker)))\n");
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      const char *lf =
          "P: models.Query[Map[str, User]] and\n/* keep */ Printable "
          "{\nread(p: P) { p.n }\nn: int\n}\nafter() {}\n";
      char text[1024];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      ASSERT_EQ_INT(2, v->program->count);
      Decl *p = v->program->items[0];
      ASSERT_EQ_INT(DECL_STRUCT, p->kind);
      ASSERT_EQ_INT(2, p->as.structure.conformance_count);
      ASSERT_EQ_INT(1, p->as.structure.count);
      ASSERT_EQ_INT(1, p->as.structure.function_count);
      Span span = p->as.structure.conformance_span;
      ASSERT_TRUE(span.file == v->source);
      ASSERT_EQ_INT(1, span.start);
      ASSERT_EQ_INT(
          (size_t)(strstr(text, "Printable") - text + strlen("Printable")),
          span.end);
      Type *q = p->as.structure.conformances[0];
      ASSERT_EQ_INT(TYPE_APPLY, q->kind);
      ASSERT_TRUE(q->as.apply.base->as.name ==
                  intern(&v->names, "models.Query"));
      ASSERT_EQ_INT(TYPE_APPLY, q->as.apply.args[0]->kind);
      ASSERT_EQ_INT(2, q->as.apply.args[0]->as.apply.count);
      ASSERT_TRUE(p->as.structure.conformances[1]->as.name ==
                  intern(&v->names, "Printable"));
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_TRUE(!memcmp(saved, filtered.items, bytes));
      ASSERT_EQ_INT(0, strcmp(dump(&arena, next), dump(&arena, v->program)));
    }
    char wide[32768];
    size_t n = (size_t)sprintf(wide, "P: ");
    for (unsigned i = 0; i < 512; i++)
      n += (size_t)sprintf(wide + n, "%sT%u", i ? " and " : "", i);
    sprintf(wide + n, " { n: int }");
    Parsed *many = parse(&arena, wide);
    ASSERT_EQ_INT(0, many->diagnostics.count);
    ASSERT_EQ_INT(512, many->program->items[0]->as.structure.conformance_count);
    const char *bad[] = {"P: {}",    "P: T and {}", "P: int {}",   "P: () {}",
                         "P: _ {}",  "P: :tag {}",  "P: T | U {}", "P: T, U {}",
                         "P: T. {}", "P: T[U {} ]"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[512];
      snprintf(text, sizeof(text), "%s\nafter() {}\n", bad[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_TRUE(v->diagnostics.count > 0);
      ASSERT_TRUE(v->program->count > 0);
      if (v->program->count)
        ASSERT_TRUE(
            !strcmp("after", v->program->items[v->program->count - 1]->name));
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(!v->diagnostics.items[j]->fixes);
    }
    char deep[4096];
    n = (size_t)sprintf(deep, "P: ");
    for (unsigned i = 0; i < 140; i++)
      n += (size_t)sprintf(deep + n, "Query[");
    n += (size_t)sprintf(deep + n, "User");
    for (unsigned i = 0; i < 140; i++)
      deep[n++] = ']';
    sprintf(deep + n, " { n: int }");
    invalid(&arena, deep, "Syntax nesting limit exceeded");
    invalid(&arena, "impl: int", "Expected '(' after function name");
    invalid(&arena, "main() { P: Trait {} }",
            "Expected a newline between items");
  }
  TEST(
      "self expressions and explicit receivers have distinct syntax metadata") {
    program_dump(
        &arena,
        "T { read(self) -> int\nset(&self, n: int) { self.n = n } }\n"
        "P { n: int\nread() { self.n }\nbump(&self) { self.n++ } }\n",
        "(program (trait T (fn read (params (self)) int none) (fn set (params "
        "(&self) (n int)) _ (block (= (field (self) n) (name n))))) "
        "(struct P (field n int) (fn read (params) _ (block (field (self) n))) "
        "(fn bump (params (&self)) _ (block (inc (field (self) n))))))\n");
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      const char *lf = "P {\npub read(& /* keep */ self, n: Vec[Other]) -> "
                       "Self { (( /* group */ self )) }\nunit: ()\n}\n";
      char text[1024];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      Decl *f = v->program->items[0]->as.structure.functions[0];
      ASSERT_EQ_INT(RECEIVER_MUT_SELF, f->as.function.receiver);
      ASSERT_EQ_INT(1, f->as.function.count);
      ASSERT_EQ_INT(VISIBILITY_PUB, f->visibility);
      ASSERT_EQ_INT(TYPE_NAME, f->as.function.return_type->kind);
      ASSERT_TRUE(f->as.function.return_type->as.name ==
                  intern(&v->names, "Self"));
      Span receiver = f->as.function.receiver_span;
      ASSERT_TRUE(receiver.file == v->source);
      ASSERT_EQ_INT((size_t)(strstr(text, "& /* keep */") - text),
                    receiver.start);
      ASSERT_EQ_INT(strlen("& /* keep */ self"), receiver.end - receiver.start);
      Expr *value = f->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(EXPR_SELF, value->kind);
      ASSERT_EQ_INT((size_t)(strstr(text, "/* group */ self") - text +
                             strlen("/* group */ ")),
                    value->name_span.start);
      ASSERT_EQ_INT(4, value->name_span.end - value->name_span.start);
      ASSERT_TRUE(value->span.start < value->name_span.start);
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_TRUE(!memcmp(saved, filtered.items, bytes));
      ASSERT_EQ_INT(0, strcmp(dump(&arena, next), dump(&arena, v->program)));
    }
    expression_dump(&arena, "value |> self.read()",
                    "(|> (name value) (call (field (self) read)))");
    expression_dump(&arena, "self\n.read().(0)",
                    "(index (call (field (self) read)) (int 0))");
    const char *bad[] = {"f(self) {}",
                         "f(&self) {}",
                         "P { n: int\nf(self) { self.n } }",
                         "P { n: int\nf(n: int, &self) {} }",
                         "P { n: int\nf(&self, &self) {} }",
                         "T { f(self, self) -> int }",
                         "P { n: int\nf(&self: P) {} }",
                         "T { f(self: T) -> int }",
                         "main() { let self = 1 }",
                         "P { self: int }",
                         "f(n: self) {}",
                         "A = self",
                         "main() { value.self }",
                         "main() { :self }",
                         "main() { for self in 0..2 {} }",
                         "Self() {}",
                         "P { Self() {} }",
                         "main() { :Self }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(!v->diagnostics.items[j]->fixes);
    }
    char wide[32768];
    size_t n = (size_t)sprintf(wide, "P { n: int\nf(&self,");
    for (unsigned i = 0; i < 512; i++)
      n += (size_t)sprintf(wide + n, "%sp%u: int", i ? "," : "", i);
    sprintf(wide + n, ") { self } }");
    Parsed *many = parse(&arena, wide);
    ASSERT_EQ_INT(0, many->diagnostics.count);
    ASSERT_EQ_INT(
        512,
        many->program->items[0]->as.structure.functions[0]->as.function.count);
  }
  TEST("empty type bodies are traits; unit fields still make structs") {
    program_dump(&arena,
                 "Empty {}\nCommented { /* keep */ }\nData { unit: () }\n",
                 "(program (trait Empty) (trait Commented) (struct Data (field "
                 "unit ())))\n");
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      const char *lf = "Empty {\n // keep\n}\nData { unit: () }\nafter() {}\n";
      char text[512];
      size_t n = 0;
      for (size_t i = 0; lf[i]; i++) {
        if (crlf && lf[i] == '\n')
          text[n++] = '\r';
        text[n++] = lf[i];
      }
      text[n] = 0;
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
      ASSERT_EQ_INT(3, v->program->count);
      Decl *trait = v->program->items[0];
      ASSERT_EQ_INT(DECL_TRAIT, trait->kind);
      ASSERT_EQ_INT(VISIBILITY_PRIV, trait->visibility);
      ASSERT_EQ_INT(0, trait->as.structure.count);
      ASSERT_EQ_INT(0, trait->span.start);
      ASSERT_EQ_INT((size_t)(strchr(text, '}') - text) + 1, trait->span.end);
      ASSERT_EQ_INT(DECL_STRUCT, v->program->items[1]->kind);
      TokenArray filtered = newline_filter(&arena, v->raw);
      size_t bytes = filtered.count * sizeof(Token);
      Token *saved = arena_alloc(&arena, bytes);
      memcpy(saved, filtered.items, bytes);
      Diagnostics again;
      diagnostics_init(&again, &arena);
      Program *next = parse_program(&arena, &v->names, filtered, &again);
      ASSERT_EQ_INT(0, again.count);
      ASSERT_EQ_INT(0, strcmp(dump(&arena, v->program), dump(&arena, next)));
      ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
    }
    Parsed *bad = parse(&arena, "Broken { field\n}\nAfter {}\n");
    ASSERT_EQ_INT(1, bad->diagnostics.count);
    ASSERT_EQ_INT(DECL_STRUCT, bad->program->items[0]->kind);
    ASSERT_EQ_INT(DECL_TRAIT, bad->program->items[1]->kind);
    Parsed *lexical = parse(&arena, "Broken { @ }\nAfter {}\n");
    ASSERT_EQ_INT(1, lexical->diagnostics.count);
    ASSERT_EQ_INT(DECL_STRUCT, lexical->program->items[0]->kind);
    Parsed *pub = parse(&arena, "pub\r\n/* keep */ Empty {}\r\n");
    ASSERT_EQ_INT(1, pub->diagnostics.count);
    if (pub->diagnostics.count == 1) {
      ASSERT_EQ_INT(0, strcmp("Trait types cannot be marked 'pub'.",
                              pub->diagnostics.items[0]->message));
      Parsed *fixed = parse(&arena, apply_fixes(&arena, pub));
      ASSERT_EQ_INT(0, fixed->diagnostics.count);
      ASSERT_EQ_INT(DECL_TRAIT, fixed->program->items[0]->kind);
      ASSERT_TRUE(strstr(fixed->source->text, "/* keep */ Empty {}\r\n") !=
                  NULL);
    }
    Parsed *lower = parse(&arena, "empty {}\n");
    ASSERT_EQ_INT(1, lower->diagnostics.count);
    Parsed *fixed = parse(&arena, apply_fixes(&arena, lower));
    ASSERT_EQ_INT(0, fixed->diagnostics.count);
    ASSERT_EQ_INT(DECL_TRAIT, fixed->program->items[0]->kind);
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
