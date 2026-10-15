#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "test_utils.h"

static void check(Arena *arena, const char *raw_text,
                  const char *expected_text) {
  Diagnostics diagnostics;
  diagnostics_init(&diagnostics, arena);
  SourceFile *source =
      source_create(arena, "raw.brf", raw_text, strlen(raw_text));
  TokenArray raw = lexer_tokenize(arena, source, &diagnostics);
  TokenArray before = {.count = raw.count};
  before.items = arena_calloc(arena, raw.count, sizeof(Token));
  memcpy(before.items, raw.items, raw.count * sizeof(Token));
  TokenArray filtered = newline_filter(arena, raw);
  SourceFile *expected_source = source_create(
      arena, "expected.brf", expected_text, strlen(expected_text));
  TokenArray expected = lexer_tokenize(arena, expected_source, &diagnostics);
  ASSERT_EQ_INT(0, diagnostics.count);
  ASSERT_EQ_INT(expected.count, filtered.count);
  ASSERT_TRUE(memcmp(before.items, raw.items, raw.count * sizeof(Token)) == 0);
  if (expected.count == filtered.count) {
    for (size_t i = 0; i < filtered.count; i++) {
      ASSERT_EQ_INT(expected.items[i].type, filtered.items[i].type);
      ASSERT_EQ_INT(expected.items[i].length, filtered.items[i].length);
      ASSERT_TRUE(memcmp(expected.items[i].start, filtered.items[i].start,
                         filtered.items[i].length) == 0);
      ASSERT_TRUE(filtered.items[i].span.file == source);
      ASSERT_TRUE(filtered.items[i].start ==
                  source->text + filtered.items[i].span.start);
    }
  }
}
int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("ordinary newlines and blank lines remain") {
    check(&arena, "\nlet x = 1\n\nlet y = 2\n", "\nlet x = 1\n\nlet y = 2\n");
  }
  TEST("parentheses and brackets suppress newlines") {
    check(&arena, "f(\n1,\n a[\n2\n]\n)\n", "f(1, a[2])\n");
  }
  TEST("innermost braces restore arm and field separators") {
    check(&arena, "f(\nx |> {\n:ok(x) => x\n:none => 0\n}\n)\n",
          "f(x |> {\n:ok(x) => x\n:none => 0\n})\n");
    check(&arena, "f(User {\nx: (\n1 +\n2\n)\ny: 3\n})\n",
          "f(User {\nx: (1 + 2)\ny: 3\n})\n");
  }
  TEST("continuation tokens and lines cross blank lines and comments") {
    check(&arena, "let x =\n\na +\n b\n\n|> f()\n .g()\n",
          "let x = a + b |> f() .g()\n");
    check(&arena, "x + /* a\n\nb */ y\nz\n", "x + y\nz\n");
    check(&arena, "x /* a\nb */ y", "x\ny");
  }
  TEST("all binary operator continuations, prefix and postfix bang") {
    const char *ops[] = {"+",  "-",  "*",  "/",  "%",  "**", "==", "!=", "<",
                         "<=", ">",  ">=", "<<", ">>", "&",  "|",  "^",  "and",
                         "or", "..", "=",  "+=", "-=", "*=", "/=", "%="};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
      char raw[64], expected[64];
      snprintf(raw, sizeof(raw), "a %s\n b", ops[i]);
      snprintf(expected, sizeof(expected), "a %s b", ops[i]);
      check(&arena, raw, expected);
    }
    check(&arena, "!\nflag\nx!\ny?\nz++\na--\n", "!flag\nx!\ny?\nz++\na--\n");
    check(&arena, "!\n!\nflag\nx!\n!\nflag", "!!flag\nx!\n!flag");
    check(&arena, "let first = :a < :b\nlet next = x >\n 0\n",
          "let first = :a < :b\nlet next = x > 0\n");
    check(&arena, "User { flag: a < b and c >\n d }\n",
          "User { flag: a < b and c > d }\n");
  }
  TEST("documented continuation keywords do not end statements") {
    const char *keywords[] = {"let", "var",  "const", "type", "impl", "pub",
                              "if",  "else", "for",   "in",   "while"};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
      char raw[64], expected[64];
      snprintf(raw, sizeof(raw), "%s\n\nx", keywords[i]);
      snprintf(expected, sizeof(expected), "%s x", keywords[i]);
      check(&arena, raw, expected);
    }
    check(&arena, "ret\nx\nbreak\ncontinue\n", "ret\nx\nbreak\ncontinue\n");
  }
  TEST("type closers preserve separators, including nested >> and aliases") {
    const char *text =
        "type Error = Sym<:bad>\nmain() {}\nUser {\n"
        "x: Vec<Sym<:ok(int), :none>>\ny: int\nz: Mod.Box<int>\n}\n";
    check(&arena, text, text);
    check(&arena, "f(): Sym<\n:ok(int),\n:none> { 1 }\n",
          "f(): Sym<:ok(int), :none> { 1 }\n");
    check(&arena, "type\nUsers =\nVec<int>\nmain() {}\n",
          "type Users = Vec<int>\nmain() {}\n");
    check(&arena, "User {\nusers:\nMod.\nVec<User>\ncount: int\n}\n",
          "User {\nusers: Mod.Vec<User>\ncount: int\n}\n");
  }
  TEST("malformed delimiters do not discard EOF or diagnose twice") {
    check(&arena, "}\nfoo)\nbar]\n", "}\nfoo)\nbar]\n");
    check(&arena, "f({ x\ny\n}\n)\n", "f({ x\ny\n})\n");
  }
  arena_destroy(&arena);
  RUN_TESTS();
}
