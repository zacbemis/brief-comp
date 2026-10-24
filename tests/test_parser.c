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
int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("empty file and walking-skeleton function") {
    program_dump(&arena, "", "(program)\n");
    program_dump(&arena, "main() { print(\"Hello\") }\n",
                 "(program (fn main (params) _ (block (call (name print) (str "
                 "\"Hello\")))))\n");
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
    program_dump(&arena, "pub add(x: int, y: int): int { x + y }",
                 "(program (fn pub add (params (x int) (y int)) int (block (+ "
                 "(name x) (name y)))))\n");
  }
  TEST("arithmetic grouping, right-associative power, and prefix binding") {
    expression_dump(&arena, "a + b * c", "(+ (name a) (* (name b) (name c)))");
    expression_dump(&arena, "a - b - c", "(- (- (name a) (name b)) (name c))");
    expression_dump(&arena, "2 ** 3 ** 2", "(** (int 2) (** (int 3) (int 2)))");
    expression_dump(&arena, "-2 ** 2", "(- (** (int 2) (int 2)))");
    expression_dump(&arena, "2 ** -2", "(** (int 2) (- (int 2)))");
    expression_dump(&arena, "(-2) ** 2", "(** (- (int 2)) (int 2))");
    expression_dump(&arena, "!user.isActive()",
                    "(! (call (field (name user) isActive)))");
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
    Parsed *v = parse(&arena, "main() {\nuser.age = 1\nitems[0] += 2\n}");
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
  TEST("increment and decrement are standalone statements, not unary values") {
    expression_dump(&arena, "x++", "(inc (name x))");
    expression_dump(&arena, "x--", "(dec (name x))");
    expression_dump(&arena, "user.age++", "(inc (field (name user) age))");
    expression_dump(&arena, "items[0]--", "(dec (index (name items) (int 0)))");
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
                         "main() { a[x++] }",
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
    } cases[] = {
        {"++x", "++", prefix}, {"--x", "--", prefix},
        {"++ x", "++", prefix}, {"-- user.age", "--", prefix},
        {"f(++x)", "++", prefix}, {"let y = --x", "--", prefix},
        {"ret ++x", "++", prefix}, {"if ++x {}", "++", prefix},
        {"while --x {}", "--", prefix},
        {"for i in 0..++x {}", "++", prefix},
        {"x + --y", "--", prefix}, {"(++x)", "++", prefix},
        {":ok(--x)", "--", prefix}, {"items[++x]", "++", prefix},
        {"x |> { _ => ++x }", "++", prefix},
        {"true && false", "&&", and_hint}, {"true||false", "||", or_hint},
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
      snprintf(text, sizeof(text), "main() {\n%s\nlet kept = 1\n}\nafter() {}\n",
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
    const char *separated[] = {"a & &b", "a | |b", "a &\n&b", "a |\n|b",
                               "a & // gap\n&b", "a | // gap\n|b",
                               "+ +x", "a + +b"};
    for (size_t i = 0; i < sizeof(separated) / sizeof(separated[0]); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() { %s }", separated[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_EQ_INT(1, v->diagnostics.count);
      if (v->diagnostics.count)
        ASSERT_EQ_STRN("Expected an expression.", v->diagnostics.items[0]->message,
                       strlen(v->diagnostics.items[0]->message));
    }
    expression_dump(&arena, "a & b | c", "(| (& (name a) (name b)) (name c))");
    expression_dump(&arena, "a & // gap\nb", "(& (name a) (name b))");
    expression_dump(&arena, "a | // gap\nb", "(| (name a) (name b))");
    expression_dump(&arena, "a or b and !c",
                    "(or (name a) (and (name b) (! (name c))))");
    expression_dump(&arena, "- -x", "(- (- (name x)))");
    program_dump(&arena, "P {}\nf(p: &P) {}",
                 "(program (struct P) (fn f (params (p & P)) _ (block)))\n");
    Parsed *v = parse(&arena,
                      "main() { true && false\ntrue || false\n++x\nlet kept = 1 }\n");
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
      ASSERT_TRUE(memcmp(saved, filtered.items,
                         filtered.count * sizeof(*saved)) == 0);
    }
    v = parse(&arena, "main() { let value = @\ntrue && @\n--x\nlet kept = 1 }\n");
    ASSERT_EQ_INT(4, v->diagnostics.count); // Two lexer and two operator errors.
    for (size_t i = 0; i < v->diagnostics.count; i++)
      ASSERT_TRUE(strcmp(v->diagnostics.items[i]->message,
                         "Expected an expression.") != 0);
  }
  TEST("postfix chains distinguish panic, logical not, and !=") {
    expression_dump(
        &arena, "items[0].get()?!",
        "(panic (? (call (field (index (name items) (int 0)) get))))");
    expression_dump(&arena, "!x != y!", "(!= (! (name x)) (panic (name y)))");
    expression_dump(&arena, "json |> parse()? |> validate()!",
                    "(|> (|> (name json) (? (call (name parse)))) (panic (call "
                    "(name validate))))");
  }
  TEST("generic calls rewind cleanly, including split >> and ambiguity") {
    expression_dump(&arena, "Vec<User>()", "(call (name Vec) (types User))");
    expression_dump(&arena, "Vec<Vec<int>>()",
                    "(call (name Vec) (types (apply Vec int)))");
    expression_dump(&arena, "make<Sym<:ok(int), :none>>()",
                    "(call (name make) (types (Sym (:ok int) (:none))))");
    expression_dump(&arena, "check(a < b, c > (d))",
                    "(call (name check) (call (name a) (types b c) (name d)))");
    expression_dump(
        &arena, "check(a < b, c > d)",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(
        &arena, "check((a < b), (c > (d)))",
        "(call (name check) (< (name a) (name b)) (> (name c) (name d)))");
    expression_dump(&arena, "a < b >> c",
                    "(< (name a) (>> (name b) (name c)))");
  }
  TEST("bindings, typed fields, methods, written and partial return types") {
    const char *text =
        "pub type Error = Sym<:bad(str), :missing>\n"
        "pub User {\nname: str\nresult: Sym<:ok(User), Error>\ncount: int\n}\n"
        "impl User {\npub new(name: str): Sym<:ok(User), _> {\n"
        "var count: shrt = 0\ncount += 1\n:ok(User { name: name, count: count "
        "})\n}\n"
        "setAge(user: &User, age: int): () { user.count = age }\n}\n";
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
    expression_dump(&arena, "if (Guest { name: name }).isAllowed() {}",
                    "(if (call (field (struct-value (name Guest) (name (name "
                    "name))) isAllowed)) (block) none)");
    expression_dump(&arena, "if isAllowed(Guest { name: name }) {}",
                    "(if (call (name isAllowed) (struct-value (name Guest) "
                    "(name (name name)))) (block) none)");
    expression_dump(&arena, "if items[Guest { name: name }] {}",
                    "(if (index (name items) (struct-value (name Guest) (name "
                    "(name name)))) (block) none)");
    invalid(&arena, "main() { if Guest { name: name }.isAllowed() {} }",
            "Expected");
    invalid(&arena, "main() { while Guest { name: name }.ok {} }", "Expected");
    invalid(&arena, "main() { for x in Guest { name: name }.items {} }",
            "Expected");
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
                         "x |> functions[0]()", "x |> f()[0].g()"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      char text[96];
      snprintf(text, sizeof(text), "main() { %s }", bad[i]);
      invalid(&arena, text, "name or dotted path");
    }
    expression_dump(
        &arena, "x |> Report.summary(a)",
        "(|> (name x) (call (field (name Report) summary) (name a)))");
    expression_dump(
        &arena, "x |> Report.make<T>()?",
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
    invalid(&arena, "type E = Sym<:ok(int), :bad(str)\n", "'>'");
    invalid(&arena, "f(): _ {}", "only allowed");
    invalid(&arena, "print(\"top-level statement\")", "inside a function");
  }
  TEST("top-level statement hints are exact and preserve later declarations") {
    const char *statements[] = {
        "let value = 1", "var value = 1", "print(\"Hello\")", "tick()",
        "print(value)", "User.new(\"Bob\", 20)", "str(1)",
        "print(User { name: \"Bob\", age: 20 })", "print(nested(1, (2)))",
        "value = 1", "value += 1", "value++", "value[0] = 1",
        "if true { print(\"if\") }", "while false { tick() }",
        "for i in 0..2 { print(str(i)) }", "ret 1", "break", "continue",
        "42", "0.5", "true", "'x'", "\"text\"", ":ok(1)", "()", "-1",
        "!true", "~1", "value", "tick()?", "tick()!", "tick().field",
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
        ASSERT_EQ_INT(source_position(v->source, 0).line,
                      source_position(v->source,
                                      v->diagnostics.items[0]->span.start).line);
      }
      ASSERT_EQ_INT(1, v->program->count);
      if (v->program->count)
        ASSERT_EQ_STRN("main", v->program->items[0]->name,
                       strlen(v->program->items[0]->name));
    }
    Parsed *v = parse(&arena, "\r\n  print(\r\n\"Hello\"\r\n)\r\nmain() {}\r\n");
    ASSERT_EQ_INT(1, v->diagnostics.count);
    ASSERT_EQ_INT(4, v->diagnostics.items[0]->span.start);
    SourcePosition pos =
        source_position(v->source, v->diagnostics.items[0]->span.start);
    ASSERT_EQ_INT(2, pos.line);
    ASSERT_EQ_INT(3, pos.column);
    ASSERT_EQ_INT(1, v->program->count);
  }
  TEST("declaration evidence keeps specific header errors, not statement hints") {
    invalid(&arena, "f(x) {}", "Expected ':' before type.");
    invalid(&arena, "f(1) {}", "Expected a parameter name.");
    invalid(&arena, "f(x: int)", "Expected '{' to start a block.");
    invalid(&arena, "f(): int", "Expected '{' to start a block.");
    invalid(&arena, "f()\n{}", "Expected '{' to start a block.");
    invalid(&arena, "f()\n: int {}", "Expected '{' to start a block.");
    invalid(&arena, "pub f(1)", "Expected a parameter name.");
    invalid(&arena, "f(", "Expected a parameter name.");
    invalid(&arena, "Box<T> { value: T }", "Expected '(' after function name");
    invalid(&arena, "first<T>(x: T): T { x }",
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
    v = parse(&arena, "pub f(x: &Self): Sym<:ok(int)> { :ok(1) }\n"
                      "main() {}\nconst VALUE = 1\n");
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(3, v->program->count);
  }
  TEST("statement lookahead leaves filtered tokens unchanged and is repeatable") {
    Parsed *v = parse(&arena,
                      "tick(make<Vec<Vec<int>>>(User { name: \"Bob\" }))\n"
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
      ASSERT_TRUE(memcmp(saved, filtered.items,
                         filtered.count * sizeof(*saved)) == 0);
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
        "type Nested = Vec<Vec<int>>\nmain(x: Nested) {\nlet x = x\nx\n}\n";
    Parsed *v = parse(&arena, text);
    ASSERT_EQ_INT(0, v->diagnostics.count);
    ASSERT_EQ_INT(0, v->program->span.start);
    ASSERT_EQ_INT(strlen(text), v->program->span.end);
    Type *outer = v->program->items[0]->as.alias;
    Type *inner = outer->as.apply.args[0];
    ASSERT_EQ_INT(inner->span.end + 1, outer->span.end);
    ASSERT_EQ_INT('>', v->source->text[inner->span.end - 1]);
    ASSERT_EQ_INT('>', v->source->text[outer->span.end - 1]);
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
    n = (size_t)sprintf(text, "type Big = int");
    for (size_t i = 0; i < 1000; i++)
      n += (size_t)sprintf(text + n, "[1]");
    strcpy(text + n, "\nmain() {}\n");
    invalid(&arena, text, "nesting limit");
  }
  TEST("malformed and arbitrary token streams terminate under sanitizers") {
    uint32_t state = 7;
    const char alphabet[] = "abc012{}()[]\n<>=:,!?+-*/|&\"'";
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
