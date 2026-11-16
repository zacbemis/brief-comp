#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("empty file and walking-skeleton function") {
    program_dump(&arena, "", "(program)\n");
    program_dump(&arena, "main() { print(\"Hello\") }\n",
                 "(program (fn main (params) _ (block (call (name print) (str "
                 "\"Hello\")))))\n");
  }
  TEST("classified names and wildcard tokens preserve current AST shapes") {
    expression_dump(&arena, "User.new(value).age",
                    "(field (call (field (name User) new) (name value)) age)");
    expression_dump(&arena, "value |> { _ => value }",
                    "(|> (name value) (match (arm (wildcard) (name value))))");
    Parsed *v = parse(&arena, "f(x: Vec[Other.User]) -> :ok[int] | _ {\n"
                              "x |> { _ => 1 }\n}\n");
    if (v->diagnostics.count)
      diagnostics_print(&v->diagnostics, stderr);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->count);
    if (v->program->count) {
      const Decl *f = v->program->items[0];
      ASSERT_EQ_INT(TYPE_APPLY, f->as.function.params[0].type->kind);
      ASSERT_EQ_INT(
          TYPE_INFER,
          f->as.function.return_type->as.union_type.items[1].type->kind);
      const Expr *pipeline = f->as.function.body->as.block.items[0].as.expr;
      ASSERT_EQ_INT(PAT_WILDCARD,
                    pipeline->as.pipe.stage->as.match.arms[0].pattern->kind);
    }
    size_t wildcards = 0;
    for (size_t i = 0; i < v->raw.count; i++)
      if (v->raw.items[i].type == TOK_WILDCARD) {
        wildcards++;
        ASSERT_TRUE(!token_is_name(v->raw.items[i].type));
        ASSERT_EQ_INT(1, v->raw.items[i].length);
      }
    ASSERT_EQ_INT(2, wildcards);
  }
  TEST("all literal forms and implicit/inferred return syntax") {
    expression_dump(&arena, "()", "(unit)");
    expression_dump(&arena, "42", "(int 42)");
    expression_dump(&arena, "0.5", "(float 0.5)");
    expression_dump(&arena, "true", "(bool true)");
    expression_dump(&arena, "'\\n'", "(chr '\\n')");
    expression_dump(&arena, "\"x\ry\"", "(str \"x\\ry\")");
    expression_dump(&arena, "\"x\ty\"", "(str \"x\\ty\")");
    expression_dump(&arena, "'\x01'", "(chr '\\x01')");
    expression_dump(&arena, ":ok[42]", "(symbol ok (int 42))");
    expression_dump(&arena, ":none", "(symbol none)");
    program_dump(&arena, "pub add(x: int, y: int) -> int { x + y }",
                 "(program (fn pub add (params (x int) (y int)) int (block (+ "
                 "(name x) (name y)))))\n");
  }
  TEST("pub is recorded on fields/functions but rejected on struct types") {
    program_dump(
        &arena,
        "P { pub value: int\nhidden: int }\npub read(p: P) -> int { p.hidden "
        "}\n",
        "(program (struct P (field pub value int) (field hidden int)) "
        "(fn pub read (params (p P)) int (block (field (name p) hidden))))\n");
    const char *cases[] = {
        "pub P { pub value: int }\n", "pub\t P { value: int }\n",
        "pub/* keep */P { value: int }\n", "\tpub\nP { pub\nvalue: int }\n",
        "pub P { unit: () }\n"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char text[1024];
        size_t count = 0;
        for (size_t j = 0; cases[i][j]; j++) {
          if (ending && cases[i][j] == '\n')
            text[count++] = '\r';
          text[count++] = cases[i][j];
        }
        strcpy(text + count, "after() {}\n");
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        if (v->diagnostics.count != 1)
          continue;
        const Diagnostic *d = v->diagnostics.items[0];
        size_t start = (size_t)(strstr(text, "pub") - text);
        ASSERT_EQ_INT(
            0, strcmp("Struct types cannot be marked 'pub'.", d->message));
        ASSERT_TRUE(d->span.file == v->source);
        ASSERT_EQ_INT(start, d->span.start);
        ASSERT_EQ_INT(start + 3, d->span.end);
        ASSERT_TRUE(d->fixes && d->fixes == d->last_fix);
        if (d->fixes) {
          const DiagnosticFix *fix = d->fixes;
          size_t end = start + 3;
          while (text[end] == ' ' || text[end] == '\t')
            end++;
          // A modifier alone on its line also removes the line break.
          size_t first = start;
          while (first > 0 &&
                 (text[first - 1] == ' ' || text[first - 1] == '\t'))
            first--;
          if (first == 0 || text[first - 1] == '\n')
            end += text[end] == '\r' ? 2 : text[end] == '\n';
          ASSERT_TRUE(fix->span.file == v->source && !fix->next);
          ASSERT_EQ_INT(start, fix->span.start);
          ASSERT_EQ_INT(end, fix->span.end);
          ASSERT_EQ_INT(0, strcmp("", fix->replacement));
          char rewritten[1024];
          snprintf(rewritten, sizeof(rewritten), "%.*s%s", (int)start, text,
                   text + end);
          Parsed *fixed = parse(&arena, rewritten);
          ASSERT_EQ_INT(0, fixed->diagnostics.count);
          char *before = dump(&arena, v->program),
               *after = dump(&arena, fixed->program);
          ASSERT_TRUE(before && after);
          if (before && after)
            ASSERT_EQ_INT(0, strcmp(before, after));
          if (v->program->count) {
            ASSERT_EQ_INT(VISIBILITY_PRIV, v->program->items[0]->visibility);
            for (size_t j = 0; j < v->raw.count; j++)
              if (v->raw.items[j].span.start == start)
                ASSERT_EQ_INT(TOK_PUB, v->raw.items[j].type);
          }
        }
      }
    }
    invalid(&arena, "pub f(pub value: int) {}", "Expected a parameter name.");
    invalid(&arena, "main() { P { pub value = 1 } }",
            "Expected a struct literal's field name.");
  }
  TEST("keyword-free aliases reject pub and preserve recovery") {
    const struct {
      const char *code;
      size_t errors;
    } cases[] = {{"pub A = :a\n", 1}, {"pub/* keep */A = :a\n", 1}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[512];
        size_t length = 0;
        for (size_t j = 0; cases[i].code[j]; j++) {
          if (crlf && cases[i].code[j] == '\n')
            text[length++] = '\r';
          text[length++] = cases[i].code[j];
        }
        strcpy(text + length, "main() {}\n");
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != cases[i].errors)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(cases[i].errors, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        char rewritten[1024];
        size_t out = 0;
        for (size_t at = 0; text[at];) {
          const DiagnosticFix *found = NULL;
          for (size_t j = 0; j < v->diagnostics.count; j++) {
            const Diagnostic *d = v->diagnostics.items[j];
            ASSERT_TRUE(d->fixes && d->fixes == d->last_fix);
            if (d->fixes && d->fixes->span.start == at)
              found = d->fixes;
          }
          if (found) {
            ASSERT_TRUE(found->span.file == v->source && !found->next);
            size_t size = strlen(found->replacement);
            memcpy(rewritten + out, found->replacement, size);
            out += size;
            at = found->span.end;
          } else
            rewritten[out++] = text[at++];
        }
        rewritten[out] = 0;
        Parsed *fixed = parse(&arena, rewritten);
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        char *before = dump(&arena, v->program),
             *after = dump(&arena, fixed->program);
        ASSERT_TRUE(before && after);
        if (before && after)
          ASSERT_EQ_INT(0, strcmp(before, after));
      }
    }
    invalid(&arena, "A\n= int", "Statements are only allowed");
    program_dump(
        &arena, "A =\nVec[Vec[int]]\nB = ()\n",
        "(program (type A (apply Vec (apply Vec int))) (type B ()))\n");
    program_dump(
        &arena, "type() {}\nmain() { let type = 1\ntype }\n",
        "(program (fn type (params) _ (block)) (fn main (params) _ (block "
        "(let type _ (int 1)) (name type))))\n");
  }
  TEST("imports retain complete interned paths and precise declaration spans") {
    const char *text = "import\nmodels.\nUser\nimport std.Json\nimport "
                       "models.User\nmain() {}\n";
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(4, v->program->count);
    if (v->program->count == 4) {
      for (size_t i = 0; i < 3; i++) {
        Decl *d = v->program->items[i];
        ASSERT_EQ_INT(DECL_IMPORT, d->kind);
        ASSERT_EQ_INT(VISIBILITY_PRIV, d->visibility);
        ASSERT_TRUE(d->span.file == v->source);
      }
      ASSERT_EQ_INT(0, v->program->items[0]->span.start);
      ASSERT_EQ_INT((size_t)(strstr(text, "User") - text) + 4,
                    v->program->items[0]->span.end);
      ASSERT_TRUE(v->program->items[0]->name == v->program->items[2]->name);
      ASSERT_EQ_INT(0, strcmp("models.User", v->program->items[0]->name));
    }
    program_dump(&arena, "import models.User\nimport std.Json\nmain() {}",
                 "(program (import models.User) (import std.Json) (fn main "
                 "(params) _ (block)))\n");
    TokenArray filtered = newline_filter(&arena, v->raw);
    size_t bytes = filtered.count * sizeof(Token);
    Token *saved = arena_alloc(&arena, bytes);
    memcpy(saved, filtered.items, bytes);
    Diagnostics again;
    diagnostics_init(&again, &arena);
    parse_program(&arena, &v->names, filtered, &again);
    ASSERT_EQ_INT(0, again.count);
    ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
  }
  TEST("imports diagnose visibility and module-name casing without changing "
       "paths") {
    const char *cases[] = {"import models.user\n", "pub import models.User\n",
                           "pub/* keep */import models.user\n",
                           "pub\nimport models.User\n"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[256];
        size_t n = 0;
        for (size_t j = 0; cases[i][j]; j++) {
          if (crlf && cases[i][j] == '\n')
            text[n++] = '\r';
          text[n++] = cases[i][j];
        }
        strcpy(text + n, "after() {}\n");
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(i == 2 ? 2 : 1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        for (size_t j = 0; j < v->diagnostics.count; j++) {
          Diagnostic *d = v->diagnostics.items[j];
          ASSERT_TRUE(d->span.file == v->source && d->fixes != NULL);
          int pub = strncmp(text + d->span.start, "pub", 3) == 0;
          ASSERT_EQ_INT(pub ? 3 : 4, d->span.end - d->span.start);
          ASSERT_EQ_INT(
              0,
              strcmp(pub ? "Imports cannot be marked 'pub'."
                         : "Module names must start with an uppercase letter.",
                     d->message));
        }
        Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        if (i == 2)
          ASSERT_TRUE(strstr(fixed->source->text, "/* keep */") != NULL);
        if (i == 3)
          ASSERT_TRUE(strncmp(fixed->source->text, "import", 6) == 0);
        if (v->program->count)
          ASSERT_EQ_INT(0, strcmp(i % 2 ? "models.User" : "models.user",
                                  v->program->items[0]->name));
      }
    }
    Parsed *v = parse(&arena, "import models.fn\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    if (v->diagnostics.count)
      ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
  }
  TEST("malformed and nested imports retain primary errors and recover") {
    const char *eof[] = {"import", "import models.", "import models.*"};
    for (size_t i = 0; i < sizeof(eof) / sizeof(*eof); i++) {
      Parsed *v = parse(&arena, eof[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
    }
    struct {
      const char *code, *message;
    } cases[] = {
        {"import _", "Expected a module path after 'import'."},
        {"import \"models.User\"", "Expected a module path after 'import'."},
        {"import models._", "Expected a name after '.' in an import path."},
        {"import models.@", "Unexpected character."},
        {"import models.User as Other", "Expected a newline between items."},
        {"main() { import models.User }",
         "Imports are only allowed at the top level."},
        {"P { value: int\nf() { import models.User } }",
         "Imports are only allowed at the top level."},
        {"P { import models.User }",
         "Imports are only allowed at the top level."},
        {"P { pub import models.User }",
         "Imports are only allowed at the top level."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[256];
      snprintf(text, sizeof(text), "%s\nafter() {}\n", cases[i].code);
      Parsed *v = parse(&arena, text);
      ASSERT_TRUE(v->diagnostics.count > 0);
      if (v->diagnostics.count) {
        ASSERT_EQ_INT(
            0, strcmp(cases[i].message, v->diagnostics.items[0]->message));
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
      }
      ASSERT_TRUE(v->program->count > 0);
      if (v->program->count)
        ASSERT_EQ_INT(
            0, strcmp("after", v->program->items[v->program->count - 1]->name));
    }
  }
  TEST("trait and ovr are names, not declaration or override modifiers") {
    const char *text =
        "trait(ovr: int) -> int { ovr }\novr(trait: int) -> int { trait }\n"
        "P { trait: int\novr: int\n trait() { self.trait }\novr() { self.ovr "
        "} }\n"
        "main() { let trait = P { trait = 1, ovr = 2 }\nvar ovr = "
        "trait.trait()\n"
        "for trait in 0..2 { ovr += trait }\n:trait |> { :trait => :ovr } }";
    Parsed *v = parse(&arena, text);
    if (v->diagnostics.count)
      diagnostics_print(&v->diagnostics, stderr);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(4, v->program->count);
    if (v->program->count == 4) {
      ASSERT_EQ_INT(0, strcmp("trait", v->program->items[0]->name));
      ASSERT_EQ_INT(0, strcmp("ovr", v->program->items[1]->name));
      ASSERT_EQ_INT(2, v->program->items[2]->as.structure.count);
      ASSERT_EQ_INT(2, v->program->items[2]->as.structure.function_count);
    }
    const char *fixed[] = {"Trait() {}", "Ovr() {}", "main() { :Trait }",
                           "main() { :Ovr }"};
    for (size_t i = 0; i < sizeof(fixed) / sizeof(*fixed); i++) {
      v = parse(&arena, fixed[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_TRUE(v->diagnostics.items[0]->fixes != NULL);
      Parsed *again = parse(&arena, apply_fixes(&arena, v));
      ASSERT_EQ_INT(0, again->diagnostics.count);
    }
  }
  TEST("bindings, typed fields, methods, written and partial return types") {
    const char *text =
        "Error = :!bad[str] | :!missing\n"
        "User {\nname: str\nresult: :ok[User] | Error\ncount: int\n\n\n"
        "pub new(name: str) -> :ok[User] | _ {\n"
        "var count: shrt = 0\ncount += 1\n:ok[User { name = name, count = "
        "count }]\n}\n"
        "setAge(user: &User, age: int) -> () { user.count = age }\n}\n";
    Parsed *v = parse(&arena, text);
    if (v->diagnostics.count)
      diagnostics_print(&v->diagnostics, stderr);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(2, v->program->count);
    Type *failures = v->program->items[0]->as.alias;
    ASSERT_EQ_INT(TYPE_UNION, failures->kind);
    ASSERT_EQ_INT(2, failures->as.union_type.count);
    ASSERT_EQ_INT(SYMBOL_ERROR, failures->as.union_type.items[0].kind);
    ASSERT_EQ_INT(SYMBOL_ERROR, failures->as.union_type.items[1].kind);
    Decl *structure = v->program->items[1];
    ASSERT_EQ_INT(3, structure->as.structure.count);
    ASSERT_EQ_INT(2, structure->as.structure.function_count);
    ASSERT_EQ_INT(
        BORROW_MUT,
        structure->as.structure.functions[1]->as.function.params[0].borrow);
    ASSERT_EQ_INT(TYPE_INFER,
                  structure->as.structure.functions[0]
                      ->as.function.return_type->as.union_type.items[1]
                      .type->kind);
    ASSERT_TRUE(structure->name == intern(&v->names, "User"));
    program_dump(
        &arena,
        "const max: int = 4\nmain() { let x = max\nvar y: lng = 1\ny = 2 }",
        "(program (const max int (int 4)) (fn main (params) _ (block (let x _ "
        "(name max)) (var y lng (int 1)) (= (name y) (int 2)))))\n");
  }
  TEST("spans, source slices, name interning and bracketed types survive "
       "parsing") {
    const char *text =
        "Nested = Vec[Vec[:x]]\nmain(x: Nested) {\nlet x = x\nx\n}\n";
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(0, v->program->span.start);
    ASSERT_EQ_INT(strlen(text), v->program->span.end);
    Type *outer = v->program->items[0]->as.alias;
    Type *inner = outer->as.apply.args[0];
    ASSERT_EQ_INT(inner->span.end + 1, outer->span.end);
    ASSERT_EQ_INT(']', v->source->text[inner->span.end - 1]);
    ASSERT_EQ_INT(']', v->source->text[outer->span.end - 1]);
    Decl *fn = v->program->items[1];
    Expr *body = fn->as.function.body;
    Decl *local = body->as.block.items[0].as.decl;
    Expr *use = body->as.block.items[1].as.expr;
    ASSERT_TRUE(local->name == fn->as.function.params[0].name);
    ASSERT_TRUE(local->name == local->as.binding.value->as.name);
    ASSERT_TRUE(local->name == use->as.name);
    ASSERT_EQ_INT(4, source_position(v->source, use->span.start).line);
    size_t closes = 0;
    for (size_t i = 0; i < v->raw.count; i++) {
      ASSERT_TRUE(v->raw.items[i].type != TOK_RSHIFT);
      if (v->raw.items[i].type == TOK_RBRACKET)
        closes++;
    }
    ASSERT_EQ_INT(2, closes);
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
