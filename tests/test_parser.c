#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

#include <stdint.h>

typedef struct {
  SourceFile *source;
  Diagnostics diagnostics;
  InternTable names;
  Program *program;
  TokenArray raw;
} Parsed;
static Parsed *parse(Arena *arena, const char *text) {
  Parsed *v = arena_calloc(arena, 1, sizeof(*v));
  v->source = source_create(arena, "test.brf", text, strlen(text));
  diagnostics_init(&v->diagnostics, arena);
  intern_init(&v->names, arena);
  v->raw = lexer_tokenize(arena, v->source, &v->diagnostics);
  v->program = parse_program(arena, &v->names, newline_filter(arena, v->raw),
                             &v->diagnostics);
  return v;
}
static char *dump(Arena *arena, const Program *program) {
  FILE *out = tmpfile();
  if (!out)
    return NULL;
  ast_print(out, program);
  fflush(out);
  long length = ftell(out);
  if (length < 0 || fseek(out, 0, SEEK_SET)) {
    fclose(out);
    return NULL;
  }
  char *text = arena_calloc(arena, (size_t)length + 1, 1);
  size_t count = fread(text, 1, (size_t)length, out);
  text[count] = '\0';
  fclose(out);
  return text;
}
static void program_dump(Arena *arena, const char *text, const char *expected) {
  Parsed *v = parse(arena, text);
  if (v->diagnostics.count)
    diagnostics_print(&v->diagnostics, stderr);
  ASSERT_EQ_INT(0, v->diagnostics.count);
  char *actual = dump(arena, v->program);
  ASSERT_TRUE(actual != NULL);
  if (actual)
    ASSERT_EQ_STRN(expected, actual, strlen(actual));
}
static void expression_dump(Arena *arena, const char *text,
                            const char *expected) {
  char *source = arena_alloc(arena, strlen(text) + 32);
  sprintf(source, "main() {\n%s\n}\n", text);
  char *tree = arena_alloc(arena, strlen(expected) + 64);
  sprintf(tree, "(program (fn main (params) _ (block %s)))\n", expected);
  program_dump(arena, source, tree);
}
static void invalid(Arena *arena, const char *source, const char *message) {
  Parsed *v = parse(arena, source);
  ASSERT_TRUE(v->diagnostics.count > 0);
  int found = 0;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    if (strstr(v->diagnostics.items[i]->message, message))
      found = 1;
  if (!found)
    diagnostics_print(&v->diagnostics, stderr);
  ASSERT_TRUE(found);
}
// Test-only application of diagnostic groups, ordered in original byte space.
static char *apply_fixes(Arena *arena, Parsed *v) {
  size_t count = 0, size = v->source->length + 1;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    for (DiagnosticFix *f = v->diagnostics.items[i]->fixes; f; f = f->next) {
      count++;
      size += strlen(f->replacement);
    }
  const DiagnosticFix **fixes = arena_alloc(arena, count * sizeof(*fixes));
  size_t n = 0;
  for (size_t i = 0; i < v->diagnostics.count; i++)
    for (DiagnosticFix *f = v->diagnostics.items[i]->fixes; f; f = f->next) {
      size_t at = n++;
      while (at && fixes[at - 1]->span.start > f->span.start) {
        fixes[at] = fixes[at - 1];
        at--;
      }
      fixes[at] = f;
    }
  char *result = arena_alloc(arena, size);
  size_t source = 0, out = 0;
  for (size_t i = 0; i < count; i++) {
    const DiagnosticFix *f = fixes[i];
    ASSERT_TRUE(f->span.file == v->source && f->span.start >= source);
    if (f->span.start < source)
      continue;
    size_t prefix = f->span.start - source, length = strlen(f->replacement);
    memcpy(result + out, v->source->text + source, prefix);
    out += prefix;
    memcpy(result + out, f->replacement, length);
    out += length;
    source = f->span.end;
  }
  strcpy(result + out, v->source->text + source);
  return result;
}
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
    Parsed *v = parse(&arena, "f(x: Vec[Other.User]) -> Sym<:ok(int), _> {\n"
                              "x |> { _ => 1 }\n}\n");
    if (v->diagnostics.count)
      diagnostics_print(&v->diagnostics, stderr);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->count);
    if (v->program->count) {
      const Decl *f = v->program->items[0];
      ASSERT_EQ_INT(TYPE_APPLY, f->as.function.params[0].type->kind);
      ASSERT_EQ_INT(TYPE_INFER,
                    f->as.function.return_type->as.sym.items[1].type->kind);
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
  TEST(
      "invalid underscore names suppress dependent parser errors and recover") {
    const char *cases[] = {"let _binding = 1",
                           "let value: _Type = 1",
                           "item._field",
                           ":_tag",
                           "for _item in 0..2 {}",
                           "f(_argument)",
                           "_call()",
                           "let value = _object"};
    const char *message =
        "Names must start with a letter; '_' alone is a wildcard.";
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *nl = ending ? "\r\n" : "\n";
        char text[512];
        snprintf(text, sizeof(text),
                 "main() {%s%s%slet kept = 1%s}%safter() {}%s", nl, cases[i],
                 nl, nl, nl, nl);
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count) {
          const Diagnostic *d = v->diagnostics.items[0];
          size_t start = (size_t)(strchr(text, '_') - text), end = start;
          while (text[end] == '_' || (text[end] >= 'a' && text[end] <= 'z') ||
                 (text[end] >= 'A' && text[end] <= 'Z'))
            end++;
          ASSERT_EQ_STRN(message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(end, d->span.end);
          ASSERT_TRUE(d->fixes == NULL);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          const Expr *body = v->program->items[0]->as.function.body;
          ASSERT_TRUE(body->as.block.count > 0);
          if (body->as.block.count) {
            BlockItem last = body->as.block.items[body->as.block.count - 1];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, last.kind);
            if (last.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", last.as.decl->name,
                             strlen(last.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
      }
    }
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
    expression_dump(&arena, ":ok(42)", "(symbol ok (int 42))");
    expression_dump(&arena, ":none", "(symbol none)");
    program_dump(&arena, "pub add(x: int, y: int) -> int { x + y }",
                 "(program (fn pub add (params (x int) (y int)) int (block (+ "
                 "(name x) (name y)))))\n");
  }
  TEST("source-exponent lexer errors suppress parser cascades and recover") {
    const char *message =
        "Exponent notation is not supported in source number literals.";
    struct {
      const char *code, *lexeme;
    } cases[] = {{"let value = 1e2", "1e2"},
                 {"let value = 0.5E-2", "0.5E-2"},
                 {"let value = -1e-2", "1e-2"},
                 {"f(1E+2)", "1E+2"},
                 {"ret -1.25e2", "1.25e2"},
                 {"if 1e2 > 0 {}", "1e2"},
                 {"while 1E2 > 0 {}", "1E2"},
                 {"for i in 0..1e2 {}", "1e2"},
                 {"P { n = 1E2 }", "1E2"},
                 {"1 |> {\n1e2 => 0\n_ => 1\n}", "1e2"},
                 {"1 |> {\n-1E2 => 0\n_ => 1\n}", "1E2"},
                 {"let value: Arr[1e2] = ()", "1e2"},
                 {"let value = 1e", "1e"},
                 {"let value = 1E+", "1E+"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[512], crlf[1024];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n", cases[i].code);
      size_t n = 0;
      for (size_t j = 0; text[j]; j++) {
        if (text[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = text[j];
      }
      crlf[n] = '\0';
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *source = ending ? crlf : text;
        Parsed *v = parse(&arena, source);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count) {
          size_t start = (size_t)(strstr(source, cases[i].lexeme) - source);
          ASSERT_EQ_STRN(message, v->diagnostics.items[0]->message,
                         strlen(v->diagnostics.items[0]->message));
          ASSERT_EQ_INT(start, v->diagnostics.items[0]->span.start);
          ASSERT_EQ_INT(start + strlen(cases[i].lexeme),
                        v->diagnostics.items[0]->span.end);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          Expr *body = v->program->items[0]->as.function.body;
          ASSERT_TRUE(body->as.block.count > 0);
          if (body->as.block.count) {
            BlockItem last = body->as.block.items[body->as.block.count - 1];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, last.kind);
            if (last.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", last.as.decl->name,
                             strlen(last.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
      }
    }
    Parsed *v = parse(&arena, "main() { let value = 1e2 + 2E3\n"
                              "let = 1\nlet kept = 1\n}\nafter() {}\n");
    ASSERT_EQ_INT(3,
                  v->diagnostics.count); // Two lexemes and one binding error.
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strcmp(message, v->diagnostics.items[i]->message) == 0 ||
                  strcmp("Expected a binding name.",
                         v->diagnostics.items[i]->message) == 0);
    expression_dump(&arena, "dbl(\"1E+2\")!",
                    "(panic (call (name dbl) (str \"1E+2\")))");
    expression_dump(&arena, "0.25 + e2", "(+ (float 0.25) (name e2))");
    expression_dump(&arena, "1 // gap\n", "(int 1)");
  }
  TEST("old dub types/converters have exact fixes, including bracketed types") {
    const char *cases[] = {
        "Box { value: dub }\nmain() {}\n",
        "Answer = Sym<:ok(dub), :none>\nmain() {}\n",
        "f(x: dub) -> dub { x }\nmain() {}\n",
        "main() { let n: dub = 1.5\nn }\n",
        "main() { dub(1) }\n",
        "main() { ( /* before */ dub /* after */ )(\"1e2\")! }\n",
        "main() { 1 |> dub() }\n",
        "main() { let n: Vec[dub] = value }\n",
        "main() { f[dub]() }\n",
        "main() { obj.f[Vec[dub]]() }\n",
        "main() { f[Sym<:ok(dub), :none>]() }\n",
        "main() { f[dub]()\nf[dub]() }\n",
        "main() { dub(dub(1)) }\n",
        "main() { a < dub(1) }\n"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char text[1024];
        size_t length = 0;
        for (size_t j = 0; cases[i][j]; j++) {
          if (ending && cases[i][j] == '\n')
            text[length++] = '\r';
          text[length++] = cases[i][j];
        }
        text[length] = 0;
        Parsed *v = parse(&arena, text);
        size_t old_count = 0;
        for (size_t j = 0; j < v->raw.count; j++) {
          const Token *token = &v->raw.items[j];
          if (token->type == TOK_OLD_DUB) {
            old_count++;
            ASSERT_EQ_INT(3, token->length);
            ASSERT_TRUE(token->start == v->source->text + token->span.start);
            ASSERT_EQ_STRN("dub", token->start, 3);
          }
        }
        ASSERT_TRUE(old_count > 0);
        if (v->diagnostics.count != old_count)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(old_count, v->diagnostics.count);
        char rewritten[1024];
        memcpy(rewritten, text, length + 1);
        for (size_t j = 0; j < v->diagnostics.count; j++) {
          const Diagnostic *d = v->diagnostics.items[j];
          ASSERT_EQ_STRN("Double precision is written 'dbl', not 'dub'.",
                         d->message, strlen(d->message));
          ASSERT_EQ_INT(3, d->span.end - d->span.start);
          ASSERT_EQ_STRN("dub", text + d->span.start, 3);
          ASSERT_TRUE(d->span.file == v->source);
          ASSERT_TRUE(d->fixes && d->fixes == d->last_fix);
          if (d->fixes) {
            const DiagnosticFix *fix = d->fixes;
            ASSERT_TRUE(fix->next == NULL && fix->span.file == v->source);
            ASSERT_EQ_INT(d->span.start, fix->span.start);
            ASSERT_EQ_INT(d->span.end, fix->span.end);
            ASSERT_EQ_INT(0, strcmp("dbl", fix->replacement));
            memcpy(rewritten + fix->span.start, fix->replacement, 3);
          }
        }
        Parsed *fixed = parse(&arena, rewritten);
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        char *before = dump(&arena, v->program);
        char *after = dump(&arena, fixed->program);
        ASSERT_TRUE(before && after);
        if (before && after)
          ASSERT_EQ_STRN(before, after, strlen(after));
      }
    }
    // Names with either prefix, and literal/comment text, are not rewrites.
    program_dump(&arena,
                 "main() { let dubx: dbl = 1\nlet dblx = dubx\n"
                 "print(\"dub dbl\") /* dub */ }\n",
                 "(program (fn main (params) _ (block (let dubx dbl (int 1)) "
                 "(let dblx _ (name dubx)) (call (name print) "
                 "(str \"dub dbl\")))))\n");
  }
  TEST("pub is recorded on fields/functions but rejected on struct types") {
    program_dump(
        &arena,
        "P { pub value: int\nhidden: int }\npub read(p: P) -> int { p.hidden "
        "}\n",
        "(program (struct P (field pub value int) (field hidden int)) "
        "(fn pub read (params (p P)) int (block (field (name p) hidden))))\n");
    const char *cases[] = {"pub P { pub value: int }\n",
                           "pub\t P { value: int }\n",
                           "pub/* keep */P { value: int }\n",
                           "\tpub\nP { pub\nvalue: int }\n", "pub P {}\n"};
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
  TEST("keyword-free aliases reject legacy modifiers and preserve recovery") {
    const struct {
      const char *code;
      size_t errors;
    } cases[] = {
        {"type A = Sym<:a>\n", 1},          {"type\t A = Sym<:a>\n", 1},
        {"type/* keep */A = Sym<:a>\n", 1}, {"type\nA = Sym<:a>\n", 1},
        {"pub A = Sym<:a>\n", 1},           {"pub/* keep */A = Sym<:a>\n", 1},
        {"type lower = Sym<:a>\n", 2},      {"pub type lower = Sym<:a>\n", 3}};
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
    const char *reserved[] = {"sym", "self", "fn"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(*reserved); i++) {
      char text[128];
      snprintf(text, sizeof(text), "type %s = Sym<:a>\nmain() {}\n",
               reserved[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(2, v->diagnostics.count);
      if (v->diagnostics.count == 2) {
        ASSERT_TRUE(v->diagnostics.items[0]->fixes != NULL);
        ASSERT_TRUE(v->diagnostics.items[1]->fixes == NULL);
      }
    }
    Parsed *bad = parse(&arena, "type Broken = {}\nmain() {}\n");
    ASSERT_EQ_INT(1, bad->diagnostics.count);
    if (bad->diagnostics.count)
      ASSERT_TRUE(!bad->diagnostics.items[0]->fixes);
    invalid(&arena, "A\n= int", "Statements are only allowed");
    program_dump(
        &arena, "A =\nVec[Vec[int]]\nB = ()\n",
        "(program (type A (apply Vec (apply Vec int))) (type B ()))\n");
    program_dump(
        &arena, "type() {}\nmain() { let type = 1\ntype }\n",
        "(program (fn type (params) _ (block)) (fn main (params) _ (block "
        "(let type _ (int 1)) (name type))))\n");
  }
  TEST("struct literal colons have exact fixes without touching annotations") {
    const struct {
      const char *body;
      size_t count;
    } cases[] = {{"P { x:1 }", 1},
                 {"P { x: 1 }", 1},
                 {"P { x :1 }", 1},
                 {"P { x : 1 }", 1},
                 {"P { x/* before */:/* after */1 }", 1},
                 {"P { x:\n// value\n1 }", 1},
                 {"P { x::none }", 1},
                 {"P { x: Q { value: 1 }, y: 2 }", 3},
                 {":outer(P { x: 1 })", 1},
                 {"let x: P = P { x: 1 }", 1}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char lf[1024], text[2048];
        snprintf(lf, sizeof(lf),
                 "P { x: int }\nmain() {\n%s\nlet kept = 1\n}\nafter() {}\n",
                 cases[i].body);
        size_t length = 0;
        for (size_t j = 0; lf[j]; j++) {
          if (ending && lf[j] == '\n')
            text[length++] = '\r';
          text[length++] = lf[j];
        }
        text[length] = 0;
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != cases[i].count)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(cases[i].count, v->diagnostics.count);
        ASSERT_EQ_INT(3, v->program->count);
        char rewritten[4096];
        size_t out = 0;
        for (size_t at = 0; text[at];) {
          const DiagnosticFix *found = NULL;
          for (size_t j = 0; j < v->diagnostics.count; j++) {
            const Diagnostic *d = v->diagnostics.items[j];
            ASSERT_EQ_INT(0, strcmp("Struct literal fields use '=', not ':'.",
                                    d->message));
            ASSERT_EQ_INT(1, d->span.end - d->span.start);
            ASSERT_TRUE(d->fixes && d->fixes == d->last_fix);
            if (d->fixes && d->fixes->span.start == at) {
              found = d->fixes;
              ASSERT_TRUE(found->span.file == v->source && !found->next);
              ASSERT_EQ_INT(d->span.start, found->span.start);
              ASSERT_EQ_INT(d->span.end, found->span.end);
              ASSERT_EQ_INT(':', text[at]);
              size_t expected_length = 1;
              int before = text[at - 1] != ' ' && text[at - 1] != '\t' &&
                           text[at - 1] != '\n' && text[at - 1] != '\r';
              int after = text[at + 1] != ' ' && text[at + 1] != '\t' &&
                          text[at + 1] != '\n' && text[at + 1] != '\r';
              expected_length += before + after;
              ASSERT_EQ_INT(expected_length, strlen(found->replacement));
              ASSERT_EQ_INT('=', found->replacement[before]);
            }
          }
          if (found) {
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
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(cases[i].count, again.count);
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
      }
    }
    Parsed *missing = parse(&arena, "main() { P { x: } }\nafter() {}\n");
    ASSERT_EQ_INT(1, missing->diagnostics.count);
    if (missing->diagnostics.count) {
      ASSERT_EQ_INT(0, strcmp("Expected an expression.",
                              missing->diagnostics.items[0]->message));
      ASSERT_TRUE(!missing->diagnostics.items[0]->fixes);
    }
    ASSERT_EQ_INT(2, missing->program->count);
    invalid(&arena, "main() { P { x 1 } }", "Expected '=' after field name.");
    const char *bodies[] = {
        "if ready { field = value }", "while ready { field = value }",
        "for i in 0..2 { field = value }",
        "if ready { field = P { x = 1 } } else { field = P { x = 2 } }",
        ("let p = if ready { field = 1\nP { x = 1 } } else { P { x = 2 } "
         "}\np.x")};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
      char text[512];
      snprintf(text, sizeof(text), "main() { %s }", bodies[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(0, v->diagnostics.count);
    }
    // Old unwrapped headers retain their primary parentheses advice.
    invalid(&arena, "main() { if P { x: 1 }.ok {} }", "Wrap struct literals");
  }
  TEST("return arrows reject old colons with local, layout-preserving fixes") {
    const struct {
      const char *code, *replacement;
    } cases[] = {{"f():int { 1 }\n", " -> "},
                 {"f(): int { 1 }\n", " ->"},
                 {"f() :int { 1 }\n", "-> "},
                 {"f() : int { 1 }\n", "->"},
                 {"f()/* before */:/* after */int { 1 }\n", " -> "},
                 {"f():\n// continuation\nint { 1 }\n", " ->"},
                 {"pub f(x: Vec[Vec[int]]): Vec[Vec[int]] { x }\n", " ->"},
                 {"f(): Sym<:ok(int), _> { 1 }\n", " ->"},
                 {"P { x: int }\nimpl P { f(p: Self): int { p.x } }\n", " ->"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char text[1024];
        size_t length = 0;
        for (size_t j = 0; cases[i].code[j]; j++) {
          if (ending && cases[i].code[j] == '\n')
            text[length++] = '\r';
          text[length++] = cases[i].code[j];
        }
        strcpy(text + length, "after() {}\n");
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count != 1)
          continue;
        const Diagnostic *d = v->diagnostics.items[0];
        ASSERT_EQ_INT(0,
                      strcmp("Return types follow '->', not ':'.", d->message));
        ASSERT_TRUE(d->span.file == v->source);
        ASSERT_EQ_INT(1, d->span.end - d->span.start);
        ASSERT_EQ_INT(':', text[d->span.start]);
        ASSERT_TRUE(d->fixes && d->fixes == d->last_fix);
        if (d->fixes) {
          const DiagnosticFix *fix = d->fixes;
          ASSERT_TRUE(!fix->next && fix->span.file == v->source);
          ASSERT_EQ_INT(d->span.start, fix->span.start);
          ASSERT_EQ_INT(d->span.end, fix->span.end);
          ASSERT_EQ_INT(0, strcmp(cases[i].replacement, fix->replacement));
          char rewritten[1200];
          snprintf(rewritten, sizeof(rewritten), "%.*s%s%s",
                   (int)fix->span.start, text, fix->replacement,
                   text + fix->span.end);
          Parsed *fixed = parse(&arena, rewritten);
          ASSERT_EQ_INT(0, fixed->diagnostics.count);
          char *before = dump(&arena, v->program),
               *after = dump(&arena, fixed->program);
          ASSERT_TRUE(before && after);
          if (before && after)
            ASSERT_EQ_INT(0, strcmp(before, after));
        }
      }
    }
    Parsed *multiple =
        parse(&arena, "f(): int { 1 }\ng(): () {}\nafter() {}\n");
    ASSERT_EQ_INT(2, multiple->diagnostics.count);
    ASSERT_EQ_INT(3, multiple->program->count);
    const char *missing[] = {"f(): {}\nafter() {}\n",
                             "f() -> {}\nafter() {}\n"};
    for (size_t i = 0; i < sizeof(missing) / sizeof(*missing); i++) {
      Parsed *v = parse(&arena, missing[i]);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        ASSERT_EQ_INT(
            0, strcmp("Expected a type.", v->diagnostics.items[0]->message));
        ASSERT_TRUE(!v->diagnostics.items[0]->fixes);
      }
      ASSERT_EQ_INT(1, v->program->count);
    }
    program_dump(&arena, "f()->\nint { 1 }\n",
                 "(program (fn f (params) int (block (int 1))))\n");
    invalid(&arena, "f() -> Vec[int]\n{}", "Expected '{' to start a block.");
    invalid(&arena, "f()\n-> int {}", "Expected '{' to start a block.");
  }
  TEST("not has prefix precedence below power and postfix, above comparisons") {
    expression_dump(&arena, "not not flag", "(not (not (name flag)))");
    expression_dump(&arena, "not (a and b)", "(not (and (name a) (name b)))");
    expression_dump(&arena, "not a == b", "(== (not (name a)) (name b))");
    expression_dump(&arena, "not a ** b", "(not (** (name a) (name b)))");
    expression_dump(&arena, "not f()!", "(not (panic (call (name f))))");
    expression_dump(&arena, "not(a)", "(not (name a))");
    expression_dump(&arena, "not\n// continued\nflag", "(not (name flag))");
  }
  TEST("old prefix bang gets a precise fix and reparses without changing its "
       "AST") {
    const struct {
      const char *code, *replacement;
    } cases[] = {{"!flag", "not "},         {"! flag", "not"},
                 {"!\tflag", "not"},        {"!\nflag", "not"},
                 {"!(a and b)", "not "},    {"!f()!", "not "},
                 {"if!flag {}", " not "},   {"ret!flag", " not "},
                 {"not!flag", " not "},     {"(true and!flag)", " not "},
                 {"!/* gap */flag", "not "}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      for (unsigned ending = 0; ending < 2; ending++) {
        char lf[512], crlf[1024];
        snprintf(lf, sizeof(lf), "main() {\n%s\nlet kept = 1\n}\nafter() {}\n",
                 cases[i].code);
        size_t count = 0;
        for (size_t j = 0; lf[j]; j++) {
          if (lf[j] == '\n')
            crlf[count++] = '\r';
          crlf[count++] = lf[j];
        }
        crlf[count] = 0;
        const char *text = ending ? crlf : lf;
        Parsed *v = parse(&arena, text);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        if (v->diagnostics.count == 1) {
          const Diagnostic *d = v->diagnostics.items[0];
          size_t start = (size_t)(strchr(text, '!') - text);
          ASSERT_EQ_STRN("Logical not is written 'not'.", d->message,
                         strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(start + 1, d->span.end);
          ASSERT_TRUE(d->fixes != NULL && d->fixes == d->last_fix);
          if (d->fixes) {
            const DiagnosticFix *fix = d->fixes;
            ASSERT_EQ_INT(start, fix->span.start);
            ASSERT_EQ_INT(start + 1, fix->span.end);
            ASSERT_TRUE(fix->span.file == v->source && fix->next == NULL);
            ASSERT_EQ_STRN(cases[i].replacement, fix->replacement,
                           strlen(fix->replacement));
            char rewritten[1200];
            snprintf(rewritten, sizeof(rewritten), "%.*s%s%s", (int)start, text,
                     fix->replacement, text + start + 1);
            Parsed *fixed = parse(&arena, rewritten);
            ASSERT_EQ_INT(0, fixed->diagnostics.count);
            char *before = dump(&arena, v->program);
            char *after = dump(&arena, fixed->program);
            ASSERT_TRUE(before != NULL && after != NULL);
            if (before && after)
              ASSERT_EQ_STRN(before, after, strlen(after));
          }
        }
      }
    }
    Parsed *nested = parse(&arena, "main() { !!flag\nlet kept = 1 }\n");
    ASSERT_EQ_INT(2, nested->diagnostics.count);
    if (nested->diagnostics.count == 2) {
      const Diagnostic *a = nested->diagnostics.items[0];
      const Diagnostic *b = nested->diagnostics.items[1];
      ASSERT_TRUE(a->fixes && b->fixes);
      ASSERT_TRUE(a->span.start != b->span.start);
    }
    // A missing operand has its own primary error, not a guessed spelling fix.
    Parsed *missing = parse(&arena, "main() { ! }\n");
    ASSERT_EQ_INT(1, missing->diagnostics.count);
    if (missing->diagnostics.count) {
      ASSERT_EQ_STRN("Expected an expression.",
                     missing->diagnostics.items[0]->message,
                     strlen(missing->diagnostics.items[0]->message));
      ASSERT_TRUE(missing->diagnostics.items[0]->fixes == NULL);
    }
  }
  TEST("arithmetic grouping, right-associative power, and prefix binding") {
    expression_dump(&arena, "a + b * c", "(+ (name a) (* (name b) (name c)))");
    expression_dump(&arena, "a - b - c", "(- (- (name a) (name b)) (name c))");
    expression_dump(&arena, "2 ** 3 ** 2", "(** (int 2) (** (int 3) (int 2)))");
    expression_dump(&arena, "-2 ** 2", "(- (** (int 2) (int 2)))");
    expression_dump(&arena, "2 ** -2", "(** (int 2) (- (int 2)))");
    expression_dump(&arena, "(-2) ** 2", "(** (- (int 2)) (int 2))");
    expression_dump(&arena, "not user.isActive()",
                    "(not (call (field (name user) isActive)))");
  }
  TEST("bitwise, range, pipeline, comparisons and logical precedence") {
    expression_dump(&arena, "flags & MASK == 0",
                    "(== (& (name flags) (name MASK)) (int 0))");
    expression_dump(&arena, "a | b ^ c & d",
                    "(| (name a) (^ (name b) (& (name c) (name d))))");
    expression_dump(&arena, "a + b << c",
                    "(<< (+ (name a) (name b)) (name c))");
    expression_dump(&arena, "~x >> 1", "(>> (~ (name x)) (int 1))");
    expression_dump(&arena, "0..count + 1",
                    "(.. (int 0) (+ (name count) (int 1)))");
    expression_dump(&arena, "0..10 |> sum()",
                    "(|> (.. (int 0) (int 10)) (call (name sum)))");
    expression_dump(
        &arena, "x + 1 |> double() > 10",
        "(> (|> (+ (name x) (int 1)) (call (name double))) (int 10))");
    expression_dump(&arena, "a or b and c",
                    "(or (name a) (and (name b) (name c)))");
    expression_dump(&arena, "a < b and b < c",
                    "(and (< (name a) (name b)) (< (name b) (name c)))");
  }
  TEST("assignments are standalone, non-chaining statements") {
    expression_dump(&arena, "a = b", "(= (name a) (name b))");
    expression_dump(&arena, "a += b or c",
                    "(+= (name a) (or (name b) (name c)))");
    const char *ops[] = {"-=", "*=", "/=", "%="};
    for (size_t i = 0; i < 4; i++) {
      char input[32], expected[64];
      snprintf(input, sizeof(input), "a %s 1", ops[i]);
      snprintf(expected, sizeof(expected), "(%s (name a) (int 1))", ops[i]);
      expression_dump(&arena, input, expected);
    }
    const char *bad[] = {
        "main() { a = b = c }",   "main() { a += b = c }",
        "main() { if a = b {} }", "main() { while a += 1 {} }",
        "main() { f(a = 1) }",    "main() { let x = (a = 1) }",
        "main() { ret a = 1 }",   "main() { :ok(a = 1) }",
        "main() { a = (b = c) }", "main() { for x in a = b {} }",
        "main() { a + b = c }",   "main() { a + b += c }",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      invalid(
          &arena, bad[i],
          "Assignment is only allowed as a standalone, non-chaining statement, "
          "with a name, field, or index on the left.");
    invalid(&arena, "main() { 1 = 2 }", "assignment target");
    Parsed *v = parse(&arena, "main() {\nuser.age = 1\nitems.(0) += 2\n}");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    Expr *body = v->program->items[0]->as.function.body;
    ASSERT_EQ_INT(2, body->as.block.count);
    ASSERT_EQ_INT(BLOCK_ITEM_STMT, body->as.block.items[0].kind);
    ASSERT_EQ_INT(STMT_ASSIGN, body->as.block.items[0].as.stmt->kind);
    ASSERT_EQ_INT(EXPR_FIELD,
                  body->as.block.items[0].as.stmt->as.assign.target->kind);
    ASSERT_EQ_INT(EXPR_INDEX,
                  body->as.block.items[1].as.stmt->as.assign.target->kind);
    ASSERT_EQ_INT(ASSIGN_ADD, body->as.block.items[1].as.stmt->as.assign.op);
    expression_dump(&arena, "x |> { _ => count += 1 }",
                    "(|> (name x) (match (arm (wildcard) (block (+= (name "
                    "count) (int 1))))))");
  }
  TEST("postfix indices compose with calls, fields, operators and control "
       "flow") {
    expression_dump(&arena, "rows.(i + 1).(f(j)).name?",
                    "(? (field (index (index (name rows) (+ (name i) (int 1))) "
                    "(call (name f) (name j))) name))");
    expression_dump(&arena, "not f().(0)! != other.(1)",
                    "(!= (not (panic (index (call (name f)) (int 0)))) (index "
                    "(name other) (int 1)))");
    expression_dump(&arena, "rows\n .(\n0\n)", "(index (name rows) (int 0))");
    expression_dump(&arena, "rows .(0)", "(index (name rows) (int 0))");
    expression_dump(
        &arena, "rows.(P { x = 0\ny = 1 }.x)",
        "(index (name rows) (field (struct-value (name P) (x (int 0)) "
        "(y (int 1))) x))");
    expression_dump(&arena, "if rows.(0) { items.(1) }",
                    "(if (index (name rows) (int 0)) (block (index (name "
                    "items) (int 1))) none)");
    expression_dump(&arena, "for x in rows.(0) { x }",
                    "(for x (index (name rows) (int 0)) (block (name x)))");
    invalid(&arena, "main() { if P { x = 1 }.(0) {} }", "Wrap struct literals");
  }
  TEST(
      "old index delimiters carry composable one-byte fixes and stable trees") {
    struct {
      const char *body;
      size_t count;
    } cases[] = {{"items[0]", 1},
                 {"rows()[cols()[0]][next()[1]]", 4},
                 {"f()[i].items[1].name", 2},
                 {"items /* before */ [ /* inside */ 1 +\ni /* tail */ ]", 1},
                 {"items()[\nP { x = 0\ny = 1 }.x\n]", 1},
                 {"items[if flag { 0 } else { 1 }]", 1},
                 {"a()[values |> { :a => 0\n_ => 1 }]", 1},
                 {"not rows[0] != other[1]", 2}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char lf[2048], text[4096], rewritten[8192];
        snprintf(lf, sizeof(lf), "main() {\n%s\nlet kept = 1\n}\nafter() {}\n",
                 cases[i].body);
        size_t length = 0;
        for (size_t j = 0; lf[j]; j++) {
          if (crlf && lf[j] == '\n')
            text[length++] = '\r';
          text[length++] = lf[j];
        }
        text[length] = 0;
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(cases[i].count, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        const char *edits[4096] = {0};
        for (size_t j = 0; j < v->diagnostics.count; j++) {
          const Diagnostic *d = v->diagnostics.items[j];
          ASSERT_EQ_INT(0, strcmp("Indexing is written '.(...)', not '[...]'.",
                                  d->message));
          ASSERT_TRUE(d->span.file == v->source);
          ASSERT_EQ_INT(d->span.start + 1, d->span.end);
          ASSERT_TRUE(d->fixes && d->fixes->next && !d->fixes->next->next);
          size_t k = 0;
          for (const DiagnosticFix *fix = d->fixes; fix; fix = fix->next, k++) {
            ASSERT_TRUE(fix->span.file == v->source);
            ASSERT_TRUE(fix->span.start < length);
            ASSERT_EQ_INT(fix->span.start + 1, fix->span.end);
            if (fix->span.start >= length)
              continue;
            ASSERT_EQ_INT(k ? ']' : '[', text[fix->span.start]);
            ASSERT_EQ_INT(0, strcmp(k ? ")" : ".(", fix->replacement));
            if (!k)
              ASSERT_EQ_INT(d->span.start, fix->span.start);
            else
              ASSERT_TRUE(fix->span.start > d->span.start);
            ASSERT_TRUE(edits[fix->span.start] == NULL);
            edits[fix->span.start] = fix->replacement;
          }
        }
        size_t count = 0;
        for (size_t j = 0; j < length; j++) {
          if (edits[j]) {
            size_t n = strlen(edits[j]);
            memcpy(rewritten + count, edits[j], n);
            count += n;
          } else
            rewritten[count++] = text[j];
        }
        rewritten[count] = 0;
        Parsed *fixed = parse(&arena, rewritten);
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        char *before = dump(&arena, v->program),
             *after = dump(&arena, fixed->program);
        ASSERT_TRUE(before && after);
        if (before && after)
          ASSERT_EQ_INT(0, strcmp(before, after));
        TokenArray filtered = newline_filter(&arena, v->raw);
        Token *raw = arena_alloc(&arena, v->raw.count * sizeof(*raw));
        Token *saved = arena_alloc(&arena, filtered.count * sizeof(*saved));
        memcpy(raw, v->raw.items, v->raw.count * sizeof(*raw));
        memcpy(saved, filtered.items, filtered.count * sizeof(*saved));
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(cases[i].count, again.count);
        ASSERT_TRUE(memcmp(raw, v->raw.items, v->raw.count * sizeof(*raw)) ==
                    0);
        ASSERT_TRUE(memcmp(saved, filtered.items,
                           filtered.count * sizeof(*saved)) == 0);
      }
    }
  }
  TEST("malformed indices preserve the primary error and later declarations") {
    struct {
      const char *body, *message;
    } cases[] = {
        {"a.()", "Expected an expression."},
        {"a.(0", "Expected ')' after index."},
        {"a.(0]", "Expected ')' after index."},
        {"a.(0, 1)", "Expected ')' after index."},
        {"a.(@)", "Unexpected character."},
        {"a[]", "Expected a type."},
        {"a[0", "Expected ']' after index."},
        {"a. (0)", "Expected a field or method name after '.'."},
        {"a. /* gap */ (0)", "Expected a field or method name after '.'."},
        {"a.\n(0)", "Expected a field or method name after '.'."}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() { %s }\nafter() {}\n",
               cases[i].body);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      ASSERT_EQ_INT(2, v->program->count);
      if (v->diagnostics.count) {
        ASSERT_EQ_INT(
            0, strcmp(cases[i].message, v->diagnostics.items[0]->message));
        ASSERT_TRUE(!v->diagnostics.items[0]->fixes);
      }
      if (v->program->count == 2)
        ASSERT_EQ_INT(0, strcmp("after", v->program->items[1]->name));
    }
  }
  TEST("increment and decrement are standalone statements, not unary values") {
    expression_dump(&arena, "x++", "(inc (name x))");
    expression_dump(&arena, "x--", "(dec (name x))");
    expression_dump(&arena, "user.age++", "(inc (field (name user) age))");
    expression_dump(&arena, "items.(0)--",
                    "(dec (index (name items) (int 0)))");
    expression_dump(
        &arena, "x |> { _ => count++ }",
        "(|> (name x) (match (arm (wildcard) (block (inc (name count))))))");
    Parsed *v = parse(&arena, "main() { x++\ny-- }");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    Expr *body = v->program->items[0]->as.function.body;
    ASSERT_EQ_INT(2, body->as.block.count);
    ASSERT_EQ_INT(BLOCK_ITEM_STMT, body->as.block.items[0].kind);
    ASSERT_EQ_INT(STMT_INC, body->as.block.items[0].as.stmt->kind);
    ASSERT_EQ_INT(STMT_DEC, body->as.block.items[1].as.stmt->kind);
    const char *bad[] = {"main() { let y = x++ }",
                         "main() { ret x-- }",
                         "main() { f(x++) }",
                         "main() { if x++ {} }",
                         "main() { while count++ < 3 {} }",
                         "main() { for i in 0..n++ {} }",
                         "main() { a.(x++) }",
                         "main() { (x++) }",
                         "main() { x++ + y }",
                         "main() { x + y++ }",
                         "main() { x++++ }",
                         "main() { x++ = y }",
                         "main() { x = y++ }",
                         "main() { x++() }",
                         "main() { x++.field }",
                         "main() { x++ |> f() }",
                         "main() { :ok(x--) }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      invalid(&arena, bad[i], "only allowed as standalone statements");
    invalid(&arena, "main() { 1++ }", "increment/decrement target");
    invalid(&arena, "main() { (x + y)-- }", "increment/decrement target");
    invalid(&arena, "main() { f()++ }", "increment/decrement target");
    invalid(&arena, "main() { ++x }", "postfix standalone statements");
    invalid(&arena, "main() { --x }", "postfix standalone statements");
    Parsed *recovered =
        parse(&arena, "main() {\nlet x = count++\nprint(\"after\")\n}");
    ASSERT_EQ_INT(1, recovered->diagnostics.count);
    ASSERT_EQ_INT(
        1, recovered->program->items[0]->as.function.body->as.block.count);
  }
  TEST("C-style logical and prefix update hints have exact spans and recover") {
    const char *prefix = "Increment and decrement must be postfix standalone "
                         "statements (x++ or x--).";
    const char *and_hint = "Use 'and' instead of '&&'.";
    const char *or_hint = "Use 'or' instead of '||'.";
    struct {
      const char *expression, *operator, *message;
    } cases[] = {{"++x", "++", prefix},
                 {"--x", "--", prefix},
                 {"++ x", "++", prefix},
                 {"-- user.age", "--", prefix},
                 {"f(++x)", "++", prefix},
                 {"let y = --x", "--", prefix},
                 {"ret ++x", "++", prefix},
                 {"if ++x {}", "++", prefix},
                 {"while --x {}", "--", prefix},
                 {"for i in 0..++x {}", "++", prefix},
                 {"x + --y", "--", prefix},
                 {"(++x)", "++", prefix},
                 {":ok(--x)", "--", prefix},
                 {"items.(++x)", "++", prefix},
                 {"x |> { _ => ++x }", "++", prefix},
                 {"true && false", "&&", and_hint},
                 {"true&&false", "&&", and_hint},
                 {"true||false", "||", or_hint},
                 {"true ||false", "||", or_hint},
                 {"true|| false", "||", or_hint},
                 {"true\t&&\tfalse", "&&", and_hint},
                 {"true/*left*/&&/*right*/false", "&&", and_hint},
                 {"(true)&&(false)", "&&", and_hint},
                 {"f(true && false)", "&&", and_hint},
                 {"let y = true || false", "||", or_hint},
                 {"ret true && false", "&&", and_hint},
                 {"if true || false {}", "||", or_hint},
                 {"while true && false {}", "&&", and_hint},
                 {"(true || false)", "||", or_hint},
                 {":ok(true && false)", "&&", and_hint},
                 {"x |> { _ => true || false }", "||", or_hint},
                 {"a < b && c > d", "&&", and_hint},
                 {"a + b||c", "||", or_hint}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[512], crlf[1024];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n",
               cases[i].expression);
      size_t n = 0;
      for (size_t j = 0; text[j]; j++) {
        if (text[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = text[j];
      }
      crlf[n] = '\0';
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *source = ending ? crlf : text;
        Parsed *v = parse(&arena, source);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        if (v->diagnostics.count) {
          const Diagnostic *d = v->diagnostics.items[0];
          size_t start = (size_t)(strstr(source, cases[i].operator) - source);
          ASSERT_EQ_STRN(cases[i].message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(start + 2, d->span.end);
          ASSERT_EQ_INT(2, source_position(v->source, d->span.start).line);
          if (cases[i].message == prefix) {
            // Moving an update is not safe in an arbitrary expression.
            ASSERT_TRUE(d->fixes == NULL);
          } else {
            ASSERT_TRUE(d->fixes != NULL && d->fixes == d->last_fix);
            if (d->fixes) {
              const DiagnosticFix *fix = d->fixes;
              ASSERT_TRUE(fix->span.file == v->source);
              ASSERT_EQ_INT(start, fix->span.start);
              ASSERT_EQ_INT(start + 2, fix->span.end);
              ASSERT_TRUE(fix->next == NULL);
              char rewritten[1100];
              snprintf(rewritten, sizeof(rewritten), "%.*s%s%s", (int)start,
                       source, fix->replacement, source + start + 2);
              Parsed *fixed = parse(&arena, rewritten);
              if (fixed->diagnostics.count)
                diagnostics_print(&fixed->diagnostics, stderr);
              ASSERT_EQ_INT(0, fixed->diagnostics.count);
            }
          }
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          Expr *body = v->program->items[0]->as.function.body;
          ASSERT_TRUE(body->as.block.count > 0);
          if (body->as.block.count) {
            BlockItem last = body->as.block.items[body->as.block.count - 1];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, last.kind);
            if (last.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", last.as.decl->name,
                             strlen(last.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
      }
    }
  }
  TEST("logical hints require adjacent bytes and preserve valid operators") {
    const char *separated[] = {"a & &b",  "a | |b",         "a &\n&b",
                               "a |\n|b", "a & // gap\n&b", "a | // gap\n|b",
                               "+ +x",    "a + +b"};
    for (size_t i = 0; i < sizeof(separated) / sizeof(separated[0]); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() { %s }", separated[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        ASSERT_EQ_STRN("Expected an expression.",
                       v->diagnostics.items[0]->message,
                       strlen(v->diagnostics.items[0]->message));
        ASSERT_TRUE(v->diagnostics.items[0]->fixes == NULL);
      }
    }
    expression_dump(&arena, "a & b | c", "(| (& (name a) (name b)) (name c))");
    expression_dump(&arena, "a & // gap\nb", "(& (name a) (name b))");
    expression_dump(&arena, "a | // gap\nb", "(| (name a) (name b))");
    expression_dump(&arena, "a or b and not c",
                    "(or (name a) (and (name b) (not (name c))))");
    expression_dump(&arena, "- -x", "(- (- (name x)))");
    program_dump(&arena, "P {}\nf(p: &P) {}",
                 "(program (struct P) (fn f (params (p & P)) _ (block)))\n");
    Parsed *v = parse(
        &arena, "main() { true && false\ntrue || false\n++x\nlet kept = 1 }\n");
    TokenArray filtered = newline_filter(&arena, v->raw);
    Token *saved = arena_alloc(&arena, filtered.count * sizeof(*saved));
    memcpy(saved, filtered.items, filtered.count * sizeof(*saved));
    size_t ampersands = 0, bars = 0;
    for (size_t i = 0; i < v->raw.count; i++)
      if (v->raw.items[i].type == TOK_BIT_AND ||
          v->raw.items[i].type == TOK_BIT_OR) {
        ASSERT_EQ_INT(1, v->raw.items[i].length);
        ampersands += v->raw.items[i].type == TOK_BIT_AND;
        bars += v->raw.items[i].type == TOK_BIT_OR;
      }
    ASSERT_EQ_INT(2, ampersands); // Lexer still emits ordinary bitwise tokens.
    ASSERT_EQ_INT(2, bars);
    for (unsigned i = 0; i < 2; i++) {
      Diagnostics d;
      diagnostics_init(&d, &arena);
      parse_program(&arena, &v->names, filtered, &d);
      ASSERT_EQ_INT(3, d.count);
      ASSERT_TRUE(
          memcmp(saved, filtered.items, filtered.count * sizeof(*saved)) == 0);
    }
    v = parse(&arena,
              "main() { let value = @\ntrue && @\n--x\nlet kept = 1 }\n");
    ASSERT_EQ_INT(4,
                  v->diagnostics.count); // Two lexer and two operator errors.
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strcmp(v->diagnostics.items[i]->message,
                         "Expected an expression.") != 0);
  }
  TEST("postfix chains distinguish panic, logical not, and !=") {
    expression_dump(
        &arena, "items.(0).get()?!",
        "(panic (? (call (field (index (name items) (int 0)) get))))");
    expression_dump(&arena, "not x != y!",
                    "(!= (not (name x)) (panic (name y)))");
    expression_dump(&arena, "json |> parse()? |> validate()!",
                    "(|> (|> (name json) (? (call (name parse)))) (panic (call "
                    "(name validate))))");
  }
  TEST("removed angle arguments and sized types have composable exact fixes") {
    struct {
      const char *text;
      size_t count;
      size_t fixes; // Of the one angle-list diagnostic, if any.
    } cases[] = {{"A = Vec<Map<int, str>>\n", 1, 4},
                 {"main() { f<int>() }\n", 1, 2},
                 {"main() { obj.read<Vec<int>>() }\n", 1, 4},
                 {"main() { f<Sym<Sym<:a>>>() }\n", 1, 2},
                 {"main() { f<Vec<dub>>() }\n", 2, 4},
                 {"A = Map<str, Vec[Vec<int>]>\n", 1, 4},
                 {"main() { f<models.Box<int>, Self>() }\n", 1, 4},
                 {"A = int[10][20]\n", 2, 0},
                 {"A = Vec<int>[2]\n", 2, 2},
                 {"A = int[ /* before */ 3 /* after */ ]\n", 1, 0},
                 {"A = int[\n// keep\n3\n]\n", 1, 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
      for (unsigned crlf = 0; crlf < 2; crlf++) {
        char text[1024];
        size_t n = 0;
        for (size_t j = 0; cases[i].text[j]; j++) {
          if (crlf && cases[i].text[j] == '\n')
            text[n++] = '\r';
          text[n++] = cases[i].text[j];
        }
        strcpy(text + n, "after() {}\n");
        Parsed *v = parse(&arena, text);
        ASSERT_EQ_INT(cases[i].count, v->diagnostics.count);
        ASSERT_EQ_INT(2, v->program->count);
        for (size_t j = 0; j < v->diagnostics.count; j++) {
          const Diagnostic *d = v->diagnostics.items[j];
          ASSERT_TRUE(d->fixes != NULL && d->span.file == v->source);
          if (strcmp(d->message, "Type arguments use '[...]', not '<...>'.") ==
              0) {
            ASSERT_EQ_INT(d->span.start + 1, d->span.end);
            ASSERT_EQ_INT('<', text[d->span.start]);
            // The outermost list carries its nested lists' fixes too.
            ASSERT_TRUE(d->fixes == NULL ||
                        d->fixes->span.start == d->span.start);
            size_t fixes = 0;
            for (const DiagnosticFix *f = d->fixes; f; f = f->next, fixes++) {
              ASSERT_EQ_INT(f->span.start + 1, f->span.end);
              int opener = text[f->span.start] == '<';
              ASSERT_TRUE(opener || text[f->span.start] == '>');
              ASSERT_EQ_INT(0, strcmp(opener ? "[" : "]", f->replacement));
            }
            ASSERT_EQ_INT(cases[i].fixes, fixes);
          }
        }
        Parsed *fixed = parse(&arena, apply_fixes(&arena, v));
        ASSERT_EQ_INT(0, fixed->diagnostics.count);
        char *before = dump(&arena, v->program),
             *after = dump(&arena, fixed->program);
        ASSERT_TRUE(before && after);
        if (before && after)
          ASSERT_EQ_INT(0, strcmp(before, after));
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(cases[i].count, again.count);
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
      }
    }
  }
  TEST("type brackets retain primary errors without inventing missing "
       "arguments") {
    const char *bad[] = {"A = Vec[]\n",      "A = Vec[int\n",
                         "main() { f[]() }", "main() { f[int }",
                         "A = Vec[1.5]\n",   "main() { f[int] }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      Parsed *v = parse(&arena, bad[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(v->diagnostics.items[j]->fixes == NULL);
    }
    program_dump(
        &arena,
        "A = Map[str, Vec[int],]\nf(a: Arr[int]) -> Vec[int] { f[int,](a) }",
        "(program (type A (apply Map str (apply Vec int))) (fn f (params (a "
        "(apply Arr int))) "
        "(apply Vec int) (block (call (name f) (types int) (name a)))))\n");
  }
  TEST("bracketed generic calls do not speculate or steal comparisons") {
    expression_dump(&arena, "Vec[User]()", "(call (name Vec) (types User))");
    expression_dump(&arena, "Vec[Vec[int]]()",
                    "(call (name Vec) (types (apply Vec int)))");
    expression_dump(&arena, "make[Sym<:ok(int), :none>]()",
                    "(call (name make) (types (Sym (:ok int) (:none))))");
    expression_dump(&arena, "check(a[b, c](d))",
                    "(call (name check) (call (name a) (types b c) (name d)))");
    expression_dump(
        &arena, "check(a < b, c > (d))",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(
        &arena, "check(a < b, c > d)",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(
        &arena, "check((a < b), (c > (d)))",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(&arena, "a < b >> c",
                    "(< (name a) (>> (name b) (name c)))");
    // Value operands never look like a removed <...>( type list.
    expression_dump(&arena, "check(a < 10, b > (a + 1))",
                    "(call (name check) (< (name a) (int 10)) (> (name b) (+ "
                    "(name a) (int 1))))");
    expression_dump(
        &arena, "check(a < (b), c > (d))",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(&arena, "check(a < Cfg.max, Cfg.min > (c))",
                    "(call (name check) (< (name a) (field (name Cfg) max)) (> "
                    "(field (name Cfg) min) (name c)))");
    expression_dump(&arena, "check(a < b.(0), c > (d))",
                    "(call (name check) (< (name a) (index (name b) (int 0))) "
                    "(> (name c) (name d)))");
  }
  TEST("bindings, typed fields, methods, written and partial return types") {
    const char *text =
        "Error = Sym<:bad(str), :missing>\n"
        "User {\nname: str\nresult: Sym<:ok(User), Error>\ncount: int\n}\n"
        "impl User {\npub new(name: str) -> Sym<:ok(User), _> {\n"
        "var count: shrt = 0\ncount += 1\n:ok(User { name = name, count = "
        "count "
        "})\n}\n"
        "setAge(user: &User, age: int) -> () { user.count = age }\n}\n";
    Parsed *v = parse(&arena, text);
    if (v->diagnostics.count)
      diagnostics_print(&v->diagnostics, stderr);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(3, v->program->count);
    Decl *structure = v->program->items[1];
    ASSERT_EQ_INT(3, structure->as.structure.count);
    Decl *impl = v->program->items[2];
    ASSERT_EQ_INT(2, impl->as.impl.count);
    ASSERT_EQ_INT(BORROW_MUT,
                  impl->as.impl.methods[1]->as.function.params[0].borrow);
    ASSERT_EQ_INT(TYPE_INFER, impl->as.impl.methods[0]
                                  ->as.function.return_type->as.sym.items[1]
                                  .type->kind);
    ASSERT_TRUE(structure->name == intern(&v->names, "User"));
    program_dump(
        &arena,
        "const MAX: int = 4\nmain() { let x = MAX\nvar y: lng = 1\ny = 2 }",
        "(program (const MAX int (int 4)) (fn main (params) _ (block (let x _ "
        "(name MAX)) (var y lng (int 1)) (= (name y) (int 2)))))\n");
  }
  TEST(
      "if as a value, newline before else, loops and control-flow statements") {
    expression_dump(
        &arena,
        "if age < 18 { \"Minor\" }\nelse if age < 100 { \"Adult\" } else { "
        "\"Senior\" }",
        "(if (< (name age) (int 18)) (block (str \"Minor\")) (if (< (name age) "
        "(int 100)) (block (str \"Adult\")) (block (str \"Senior\"))))");
    program_dump(
        &arena,
        "main() {\nfor i in 0..10 {\nif i == 3 { continue }\nif i == 8 { break "
        "}\nprint(i)\n}\nwhile true { ret }\n}",
        "(program (fn main (params) _ (block (for i (.. (int 0) (int 10)) "
        "(block (if (== (name i) (int 3)) (block (continue)) none) (if (== "
        "(name i) (int 8)) (block (break)) none) (call (name print) (name "
        "i)))) (while (bool true) (block (ret))))))\n");
    expression_dump(
        &arena, "(if x { 1 } else { 2 }) + 3",
        "(+ (if (name x) (block (int 1)) (block (int 2))) (int 3))");
  }
  TEST("bare blocks are rejected but explicit match-arm blocks remain valid") {
    const char *bad[] = {
        "main() { { print(\"a\") } }",
        "main() { print({ print(\"a\")\n\"b\" }) }",
        "main() { let x = { 1 } }",
        "main() { ret { 1 } }",
        "main() { f(({ 1 })) }",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
      invalid(&arena, bad[i], "Bare blocks are not allowed");
    expression_dump(&arena, "x |> { _ => { print(\"before\")\n42 } }",
                    "(|> (name x) (match (arm (wildcard) (block (call (name "
                    "print) (str \"before\")) (int 42)))))");
    expression_dump(&arena, "x |> { _ => {} }",
                    "(|> (name x) (match (arm (wildcard) (block))))");
    Parsed *v =
        parse(&arena, "main() {\n{ 1 }\nprint(\"kept\")\n}\nlater() {}\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
  }
  TEST(
      "struct-literal suppression resets inside grouping, calls, and indices") {
    expression_dump(&arena, "if (Guest { name = name }).isAllowed() {}",
                    "(if (call (field (struct-value (name Guest) (name (name "
                    "name))) isAllowed)) (block) none)");
    expression_dump(&arena, "if isAllowed(Guest { name = name }) {}",
                    "(if (call (name isAllowed) (struct-value (name Guest) "
                    "(name (name name)))) (block) none)");
    expression_dump(&arena, "if items.(Guest { name = name }) {}",
                    "(if (index (name items) (struct-value (name Guest) (name "
                    "(name name)))) (block) none)");
    invalid(&arena, "main() { if Guest { name = name }.isAllowed() {} }",
            "Wrap struct literals in conditions or iterables in parentheses.");
    invalid(&arena, "main() { while Guest { name = name }.ok {} }",
            "Wrap struct literals in conditions or iterables in parentheses.");
    invalid(&arena, "main() { for x in Guest { name = name }.items {} }",
            "Wrap struct literals in conditions or iterables in parentheses.");
  }
  TEST("struct header hints have exact spans and recover without token "
       "changes") {
    const char *message =
        "Wrap struct literals in conditions or iterables in parentheses.";
    const char *cases[] = {
        "if Guest { name = name }.isAllowed() {}",
        "while Guest { name = name }.ok {}",
        "for x in Guest { name = name }.items {}",
        "for i in 0..Limit { n = 2 }.n {}",
        "if ready and Guest { name = name }.ok {}",
        "if not Guest { name = name }.ok {}",
        "if module.Guest { name = name }.ok {}",
        "if (Guest) { name = name }.ok {}",
        "if Guest { /* comment */ name = name }.ok {}",
        "if Guest {\n// comment\nname = name, other = P { n = 1 }\n}.ok {}",
        "let value = if Guest { name = name }.ok {} else {}",
        "ret if Guest { name = name }.ok {} else {}",
        "sink(if Guest { name = name }.ok {})",
        "if outer(if Guest { name = name }.ok {}) {}",
        // Parenthesized: one case, intentionally split across two literals.
        ("if test[Sym<:ok(Sym<:inner(int)>)>](0) and "
         "Guest { name = name }.ok {}")};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char text[1024], crlf[2048];
      snprintf(text, sizeof(text),
               "main() {\n%s\nlet kept = 1\n}\nafter() {}\n", cases[i]);
      size_t n = 0;
      for (size_t j = 0; text[j]; j++) {
        if (text[j] == '\n')
          crlf[n++] = '\r';
        crlf[n++] = text[j];
      }
      crlf[n] = '\0';
      for (unsigned ending = 0; ending < 2; ending++) {
        const char *source = ending ? crlf : text;
        Parsed *v = parse(&arena, source);
        if (v->diagnostics.count != 1)
          diagnostics_print(&v->diagnostics, stderr);
        ASSERT_EQ_INT(1, v->diagnostics.count);
        const char *brace = strchr(strchr(source, '\n') + 1, '{');
        size_t start = (size_t)(brace - source);
        if (v->diagnostics.count) {
          Diagnostic *d = v->diagnostics.items[0];
          ASSERT_EQ_STRN(message, d->message, strlen(d->message));
          ASSERT_EQ_INT(start, d->span.start);
          ASSERT_EQ_INT(start + 1, d->span.end);
        }
        ASSERT_EQ_INT(2, v->program->count);
        if (v->program->count == 2) {
          Expr *body = v->program->items[0]->as.function.body;
          ASSERT_EQ_INT(1, body->as.block.count);
          if (body->as.block.count == 1) {
            BlockItem kept = body->as.block.items[0];
            ASSERT_EQ_INT(BLOCK_ITEM_DECL, kept.kind);
            if (kept.kind == BLOCK_ITEM_DECL)
              ASSERT_EQ_STRN("kept", kept.as.decl->name,
                             strlen(kept.as.decl->name));
          }
          ASSERT_EQ_STRN("after", v->program->items[1]->name,
                         strlen(v->program->items[1]->name));
        }
        TokenArray filtered = newline_filter(&arena, v->raw);
        size_t bytes = filtered.count * sizeof(Token);
        Token *saved = arena_alloc(&arena, bytes);
        memcpy(saved, filtered.items, bytes);
        size_t raw_bytes = v->raw.count * sizeof(Token);
        Token *raw_saved = arena_alloc(&arena, raw_bytes);
        memcpy(raw_saved, v->raw.items, raw_bytes);
        Diagnostics again;
        diagnostics_init(&again, &arena);
        Program *program = parse_program(&arena, &v->names, filtered, &again);
        ASSERT_EQ_INT(1, again.count);
        if (again.count) {
          ASSERT_EQ_STRN(message, again.items[0]->message,
                         strlen(again.items[0]->message));
          ASSERT_EQ_INT(start, again.items[0]->span.start);
          ASSERT_EQ_INT(start + 1, again.items[0]->span.end);
        }
        ASSERT_TRUE(memcmp(saved, filtered.items, bytes) == 0);
        ASSERT_TRUE(memcmp(raw_saved, v->raw.items, raw_bytes) == 0);
        char *first = dump(&arena, v->program), *second = dump(&arena, program);
        ASSERT_TRUE(first && second);
        if (first && second)
          ASSERT_EQ_STRN(first, second, strlen(second));
      }
    }
    Parsed *v =
        parse(&arena, "main() {\nif Guest { name = name }.ok {}\nlet broken 1\n"
                      "let kept = 1\n}\nafter() {}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    if (v->diagnostics.count == 2) {
      ASSERT_EQ_STRN(message, v->diagnostics.items[0]->message,
                     strlen(v->diagnostics.items[0]->message));
      ASSERT_EQ_STRN("Expected '=' in binding.",
                     v->diagnostics.items[1]->message,
                     strlen(v->diagnostics.items[1]->message));
    }
    v = parse(&arena, "main() {\nif Guest { name = @ }.ok {}\nlet kept = 1\n}");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    if (v->diagnostics.count == 2) {
      ASSERT_EQ_STRN("Unexpected character.", v->diagnostics.items[0]->message,
                     strlen(v->diagnostics.items[0]->message));
      ASSERT_EQ_STRN(message, v->diagnostics.items[1]->message,
                     strlen(v->diagnostics.items[1]->message));
    }
  }
  TEST("header hints preserve ordinary blocks and literal escape contexts") {
    expression_dump(&arena, "if ready {}.field",
                    "(field (if (name ready) (block) none) field)");
    expression_dump(&arena, "if Empty {}.ok {}",
                    "(struct-value (field (if (name Empty) (block) none) ok))");
    expression_dump(&arena, "if ready {}", "(if (name ready) (block) none)");
    expression_dump(&arena, "while ready {}", "(while (name ready) (block))");
    expression_dump(&arena, "for x in values {}",
                    "(for x (name values) (block))");
    const char *valid[] = {
        "if ready { field }",
        "if ready { :tag }",
        "if ready { let field: int = 1\nfield }",
        "if ready { Guest { name = name } }",
        "if (Guest { name = name }).ok {}",
        "while (Guest { name = name }).ok {}",
        "for i in 0..(Limit { n = 2 }).n {}",
        "if check(Guest { name = name }) {}",
        "while check(Guest { name = name }) {}",
        "for i in bounds(Limit { n = 2 }) {}",
        "if items.(Guest { name = name }) {}",
        "while items.(Guest { name = name }) {}",
        "for x in items.(Guest { name = name }) {}",
        "if check[Sym<:ok(Sym<:inner(int)>)>](Guest { name = name }) {}"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
      char source[512];
      snprintf(source, sizeof(source), "main() { %s }", valid[i]);
      Parsed *v = parse(&arena, source);
      if (v->diagnostics.count)
        diagnostics_print(&v->diagnostics, stderr);
      ASSERT_EQ_INT(0, v->diagnostics.count);
    }
    const char *other_errors[] = {"main() { if true { field: value } }",
                                  "main() { if check() { field: value } }",
                                  "main() { if ready { field() -> value } }",
                                  "main() { if ready { true: value } }",
                                  "main() { if ready",
                                  "main() { if ready {",
                                  "main() { if ready {\n",
                                  "main() { if ready { field"};
    for (size_t i = 0; i < sizeof(other_errors) / sizeof(other_errors[0]);
         i++) {
      Parsed *v = parse(&arena, other_errors[i]);
      ASSERT_TRUE(v->diagnostics.count > 0);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(strstr(v->diagnostics.items[j]->message,
                           "Wrap struct literals") == NULL);
    }
    Parsed *v = parse(&arena, "main() { if Guest { name:");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    if (v->diagnostics.count == 2) {
      ASSERT_TRUE(strstr(v->diagnostics.items[0]->message,
                         "Wrap struct literals") != NULL);
      ASSERT_EQ_STRN("Expected '}' to close block.",
                     v->diagnostics.items[1]->message,
                     strlen(v->diagnostics.items[1]->message));
    }
    v = parse(&arena, "main() { if @ {} }");
    ASSERT_EQ_INT(1, v->diagnostics.count);
  }
  TEST("match patterns include literals, ranges, symbols, sets, and "
       "catch-alls") {
    expression_dump(
        &arena,
        "age |> {\n< 18 => \"Minor\"\n18..100 => \"Adult\"\n>= 100 => "
        "\"Senior\"\n}",
        "(|> (name age) (match (arm (compare < (int 18)) (str \"Minor\")) (arm "
        "(range (int 18) (int 100)) (str \"Adult\")) (arm (compare >= (int "
        "100)) (str \"Senior\"))))");
    expression_dump(
        &arena,
        "parse(text) |> {\n:ok(value) => value\nJson.Error => ret "
        ":invalid\nother => ret other\n}",
        "(|> (call (name parse) (name text)) (match (arm (tag ok (bind value)) "
        "(name value)) (arm (set Json.Error) (block (ret (symbol invalid)))) "
        "(arm (bind other) (block (ret (name other))))))");
    expression_dump(
        &arena, "x |> {\n:ok(:some(user)) => user\n:none => ()\n_ => 0\n}",
        "(|> (name x) (match (arm (tag ok (tag some (bind user))) (name user)) "
        "(arm (tag none) (unit)) (arm (wildcard) (int 0))))");
    expression_dump(
        &arena,
        "x |> {\n-10..0 => 1\ntrue => 2\n\"s\" => 3\n'c' => 4\n() => 5\n(x) => "
        "x\n}",
        "(|> (name x) (match (arm (range (- (int 10)) (int 0)) (int 1)) (arm "
        "(literal (bool true)) (int 2)) (arm (literal (str \"s\")) (int 3)) "
        "(arm (literal (chr 'c')) (int 4)) (arm (literal (unit)) (int 5)) (arm "
        "(bind x) (name x))))");
  }
  TEST("match blocks nested in call arguments retain arm separators") {
    expression_dump(&arena, "f(\nx |> {\n:ok(x) => x\n:none => 0\n}\n)",
                    "(call (name f) (|> (name x) (match (arm (tag ok (bind x)) "
                    "(name x)) (arm (tag none) (int 0)))))");
  }
  TEST("non-chaining comparisons and ranges reject only ungrouped chains") {
    invalid(&arena, "main() { a < b < c }", "do not chain");
    invalid(&arena, "main() { a == b != c }", "do not chain");
    invalid(&arena, "main() { 0..1..2 }", "do not chain");
    expression_dump(&arena, "(a < b) < c",
                    "(< (< (name a) (name b)) (name c))");
    expression_dump(&arena, "(0..1)..2", "(.. (.. (int 0) (int 1)) (int 2))");
  }
  TEST("pipelines require calls or matches, including the documented + "
       "ambiguity") {
    invalid(&arena, "main() { x |> double() + 1 }", "pipeline stage");
    invalid(&arena, "main() { x |> value }", "pipeline stage");
    invalid(&arena, "main() { x |> { _ => 1 } + 1 }", "pipeline stage");
    expression_dump(&arena, "(x |> double()) + 1",
                    "(+ (|> (name x) (call (name double))) (int 1))");
    expression_dump(&arena, "(age >= 18) |> { true => 1\nfalse => 0 }",
                    "(|> (>= (name age) (int 18)) (match (arm (literal (bool "
                    "true)) (int 1)) (arm (literal (bool false)) (int 0))))");
  }
  TEST("pipeline call stages have only name or dotted-path callees") {
    const char *bad[] = {"x |> f().g()", "x |> f().g()?", "x |> f()()",
                         "x |> functions.(0)()", "x |> f().(0).g()"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      char text[96];
      snprintf(text, sizeof(text), "main() { %s }", bad[i]);
      invalid(&arena, text, "name or dotted path");
    }
    expression_dump(
        &arena, "x |> Report.summary(a)",
        "(|> (name x) (call (field (name Report) summary) (name a)))");
    expression_dump(
        &arena, "x |> Report.make[T]()?",
        "(|> (name x) (? (call (field (name Report) make) (types T))))");
    expression_dump(&arena, "x |> f(g())",
                    "(|> (name x) (call (name f) (call (name g))))");
    expression_dump(&arena, "(x |> f()).g()",
                    "(call (field (|> (name x) (call (name f))) g))");
    expression_dump(&arena, "x |> f() |> g()",
                    "(|> (|> (name x) (call (name f))) (call (name g)))");
  }
  TEST("semicolon syntax, malformed types and incomplete expressions are "
       "rejected") {
    invalid(&arena, "main() { x; y }", "newline");
    invalid(&arena, "main() { x;; }", "newline");
    invalid(&arena, "main() { let x = }", "expression");
    invalid(&arena, "f(x: int { x }", "')'");
    invalid(&arena, "E = Sym<:ok(int), :bad(str)\n", "'>'");
    invalid(&arena, "f() -> _ {}", "only allowed");
    invalid(&arena, "print(\"top-level statement\")", "inside a function");
  }
  TEST("top-level statement hints are exact and preserve later declarations") {
    const char *statements[] = {"let value = 1",
                                "var value = 1",
                                "print(\"Hello\")",
                                "tick()",
                                "print(value)",
                                "User.new(\"Bob\", 20)",
                                "str(1)",
                                "print(User { name = \"Bob\", age = 20 })",
                                "print(nested(1, (2)))",
                                "value = 1",
                                "value += 1",
                                "value++",
                                "value.(0) = 1",
                                "value.(0)",
                                "call(value.(0))",
                                "call(value.(other.(0)))",
                                "if true { print(\"if\") }",
                                "while false { tick() }",
                                "for i in 0..2 { print(str(i)) }",
                                "ret 1",
                                "break",
                                "continue",
                                "42",
                                "0.5",
                                "true",
                                "'x'",
                                "\"text\"",
                                ":ok(1)",
                                "()",
                                "-1",
                                "not true",
                                "~1",
                                "value",
                                "tick()?",
                                "tick()!",
                                "tick().field",
                                "print(\n\"multiline\"\n)"};
    for (size_t i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
      char text[512];
      snprintf(text, sizeof(text), "%s\nmain() {}\n", statements[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count) {
        ASSERT_EQ_STRN("Statements are only allowed inside a function.",
                       v->diagnostics.items[0]->message,
                       strlen(v->diagnostics.items[0]->message));
        ASSERT_EQ_INT(0, v->diagnostics.items[0]->span.start);
        ASSERT_EQ_INT(
            source_position(v->source, 0).line,
            source_position(v->source, v->diagnostics.items[0]->span.start)
                .line);
      }
      ASSERT_EQ_INT(1, v->program->count);
      if (v->program->count)
        ASSERT_EQ_STRN("main", v->program->items[0]->name,
                       strlen(v->program->items[0]->name));
    }
    Parsed *v =
        parse(&arena, "\r\n  print(\r\n\"Hello\"\r\n)\r\nmain() {}\r\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_INT(4, v->diagnostics.items[0]->span.start);
    SourcePosition pos =
        source_position(v->source, v->diagnostics.items[0]->span.start);
    ASSERT_EQ_INT(2, pos.line);
    ASSERT_EQ_INT(3, pos.column);
    ASSERT_EQ_INT(1, v->program->count);
  }
  TEST("declaration evidence keeps specific header errors, not statement "
       "hints") {
    invalid(&arena, "f(x) {}", "Expected ':' before type.");
    invalid(&arena, "f(1) {}", "Expected a parameter name.");
    invalid(&arena, "f(x: int)", "Expected '{' to start a block.");
    invalid(&arena, "f() -> int", "Expected '{' to start a block.");
    invalid(&arena, "f()\n{}", "Expected '{' to start a block.");
    invalid(&arena, "f()\n-> int {}", "Expected '{' to start a block.");
    invalid(&arena, "pub f(1)", "Expected a parameter name.");
    invalid(&arena, "f(", "Expected a parameter name.");
    invalid(&arena, "Box<T> { value: T }", "Expected '(' after function name");
    invalid(&arena, "first<T>(x: T) -> T { x }",
            "Expected '(' after function name");
    invalid(&arena, "User { name: }", "Expected a type.");
    invalid(&arena, "import User",
            "Expected a declaration (function, struct, named type, or impl).");
    invalid(&arena, "int() {}",
            "Expected a declaration (function, struct, named type, or impl).");
    invalid(&arena, "str(x: int) {}",
            "Expected a declaration (function, struct, named type, or impl).");
    Parsed *v = parse(&arena, "broken(@)\nmain() {}\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_STRN("Unexpected character.", v->diagnostics.items[0]->message,
                   strlen(v->diagnostics.items[0]->message));
    ASSERT_EQ_INT(1, v->program->count);
    v = parse(&arena, "pub f(x: &Self) -> Sym<:ok(int)> { :ok(1) }\n"
                      "main() {}\nconst VALUE = 1\n");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(3, v->program->count);
  }
  TEST("statement lookahead leaves filtered tokens unchanged and is "
       "repeatable") {
    Parsed *v =
        parse(&arena, "tick(make[Vec[Vec[int]]](User { name: \"Bob\" }))\n"
                      "main() {}\n");
    TokenArray filtered = newline_filter(&arena, v->raw);
    Token *saved = arena_alloc(&arena, filtered.count * sizeof(*saved));
    memcpy(saved, filtered.items, filtered.count * sizeof(*saved));
    for (unsigned i = 0; i < 2; i++) {
      Diagnostics d;
      diagnostics_init(&d, &arena);
      Program *program = parse_program(&arena, &v->names, filtered, &d);
      ASSERT_EQ_INT(1, d.count);
      ASSERT_EQ_INT(1, program->count);
      ASSERT_TRUE(
          memcmp(saved, filtered.items, filtered.count * sizeof(*saved)) == 0);
    }
  }
  TEST("recovery retains later statements, arms, fields, methods and "
       "declarations") {
    Parsed *v = parse(
        &arena, "main() {\nlet = 1\nlet valid = 2\nbad(1}\nnext() { 3 }\n");
    ASSERT_TRUE(v->diagnostics.count >= 2);
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
    ASSERT_EQ_STRN("next", v->program->items[1]->name,
                   strlen(v->program->items[1]->name));
    v = parse(&arena,
              "User {\n: bad\ngood: int\n}\nimpl User {\nbad(x) {}\ngood() "
              "{}\n}\nmain() {\nx |> {\n:ok(x) x\n:none => 0\n}\n}\n");
    ASSERT_TRUE(v->diagnostics.count >= 3);
    ASSERT_EQ_INT(3, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.structure.count);
    ASSERT_EQ_INT(1, v->program->items[1]->as.impl.count);
    Expr *pipe =
        v->program->items[2]->as.function.body->as.block.items[0].as.expr;
    ASSERT_EQ_INT(1, pipe->as.pipe.stage->as.match.count);
  }
  TEST("lexer errors do not hide syntax errors or create duplicate "
       "diagnostics") {
    Parsed *v = parse(&arena, "main() {\nlet a = @\nlet = 1\nlet good = 2\n}\n"
                              "next(x) {}\nlast() { 4 }\n");
    ASSERT_EQ_INT(3,
                  v->diagnostics.count); // One lexer error, two syntax errors.
    ASSERT_EQ_INT(2, v->program->count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
    ASSERT_EQ_STRN("last", v->program->items[1]->name,
                   strlen(v->program->items[1]->name));
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strstr(v->diagnostics.items[i]->message,
                         "Expected an expression") == NULL);
    v = parse(&arena, "@\nbad(x) {}\ngood() {}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->count);
    v = parse(&arena, "main() { f(@)\nlet a = @\nlet good = 1\n}\n");
    ASSERT_EQ_INT(2, v->diagnostics.count);
    ASSERT_EQ_INT(1, v->program->items[0]->as.function.body->as.block.count);
  }
  TEST("spans, source slices, name interning and virtual type closers survive "
       "parsing") {
    const char *text =
        "Nested = Vec[Vec[Sym<Sym<:x>>]]\nmain(x: Nested) {\nlet x = x\nx\n}\n";
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
    int found = 0;
    for (size_t i = 0; i < v->raw.count; i++)
      if (v->raw.items[i].type == TOK_RSHIFT) {
        ASSERT_EQ_INT(2, v->raw.items[i].length);
        found = 1;
      }
    ASSERT_TRUE(found);
  }
  TEST("large lists grow and deep syntax fails with a diagnostic, not stack "
       "overflow") {
    char text[20000];
    size_t n = (size_t)sprintf(text, "main() {\n");
    for (size_t i = 0; i < 300; i++)
      n += (size_t)sprintf(text + n, "let x%zu = %zu\n", i, i);
    strcpy(text + n, "}\n");
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(300, v->program->items[0]->as.function.body->as.block.count);
    n = (size_t)sprintf(text, "main() { ");
    memset(text + n, '(', 1000);
    n += 1000;
    text[n++] = '1';
    memset(text + n, ')', 1000);
    n += 1000;
    strcpy(text + n, " }");
    invalid(&arena, text, "nesting limit");
    // Flat source can still create a deeply left-associated tree. Guard that
    // too, so dump-ast and future recursive passes cannot overflow the stack.
    n = (size_t)sprintf(text, "main() { 1");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, " + 1");
    strcpy(text + n, " }");
    invalid(&arena, text, "nesting limit");
    n = (size_t)sprintf(text, "main() { value");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, ".(0)");
    strcpy(text + n, " }\nafter() {}\n");
    invalid(&arena, text, "nesting limit");
    n = (size_t)sprintf(text, "Big = ");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, "Arr[");
    n += (size_t)sprintf(text + n, "int");
    memset(text + n, ']', 1000);
    strcpy(text + n + 1000, "\nmain() {}\n");
    invalid(&arena, text, "nesting limit");
  }
  TEST("malformed and arbitrary token streams terminate under sanitizers") {
    uint32_t state = 7;
    const char alphabet[] = "abc012{}()[]\n<>=:,.!?+-*/|&\"'";
    for (size_t sample = 0; sample < 1000; sample++) {
      Arena temporary;
      arena_init(&temporary);
      char text[128];
      for (size_t i = 0; i < sizeof(text) - 1; i++) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        text[i] = alphabet[(state >> 24) % (sizeof(alphabet) - 1)];
      }
      text[sizeof(text) - 1] = '\0';
      parse(&temporary, text);
      arena_destroy(&temporary);
    }
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
