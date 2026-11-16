#include "parser_support.h"

int main(void) {
  Arena arena;
  arena_init(&arena);
  TEST("not has prefix precedence below power and postfix, above comparisons") {
    expression_dump(&arena, "not not flag", "(not (not (name flag)))");
    expression_dump(&arena, "not (a and b)", "(not (and (name a) (name b)))");
    expression_dump(&arena, "not a == b", "(== (not (name a)) (name b))");
    expression_dump(&arena, "not a ** b", "(not (** (name a) (name b)))");
    expression_dump(&arena, "not f()!", "(not (panic (call (name f))))");
    expression_dump(&arena, "not(a)", "(not (name a))");
    expression_dump(&arena, "not\n// continued\nflag", "(not (name flag))");
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
        "main() { ret a = 1 }",   "main() { :ok[a = 1] }",
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
        {"a[0", "Expected a type."},
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
                         "main() { :ok[x--] }"};
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
    expression_dump(&arena, "make[:ok[int] | :none]()",
                    "(call (name make) (types (union (:ok int) (:none))))");
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
  TEST("typed collection literals retain constructors, types, items and "
       "chains") {
    expression_dump(
        &arena, "Arr[int] { 1, 2 + 3, }",
        "(collection (name Arr) (types int) (int 1) (+ (int 2) (int 3)))");
    expression_dump(&arena, "Vec[str] {}",
                    "(collection (name Vec) (types str))");
    expression_dump(&arena, "Vec[Arr[int]] { Arr[int] { 1 } }",
                    "(collection (name Vec) (types (apply Arr int)) "
                    "(collection (name Arr) (types int) (int 1)))");
    expression_dump(
        &arena, "Vec[User] { User { name = \"A\" } }.(0).name",
        "(field (index (collection (name Vec) (types User) (struct-value (name "
        "User) (name (str \"A\")))) (int 0)) name)");
    expression_dump(&arena, "for n in (Arr[int] {1, 2}) { n }",
                    "(for n (collection (name Arr) (types int) (int 1) (int "
                    "2)) (block (name n)))");
    const char *lf =
        "main() {\nlet values = Arr[int] {\n1,\n2,\n}\n}\nafter() {}\n";
    for (unsigned crlf = 0; crlf < 2; crlf++) {
      char text[256];
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
      if (v->program->count == 2) {
        Expr *e = v->program->items[0]
                      ->as.function.body->as.block.items[0]
                      .as.decl->as.binding.value;
        ASSERT_EQ_INT(EXPR_COLLECTION, e->kind);
        ASSERT_EQ_INT(2, e->as.collection.count);
        ASSERT_EQ_INT(1, e->as.collection.type_count);
        ASSERT_TRUE(e->span.file == v->source);
        ASSERT_EQ_INT((size_t)(strstr(text, "Arr") - text), e->span.start);
        ASSERT_EQ_INT('}', text[e->span.end - 1]);
        ASSERT_EQ_INT(2, e->depth);
      }
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
  }
  TEST("collection literal errors recover") {
    const char *bad[] = {"Arr[int] { 1 2 }", "Vec[str] {,}",
                         "Arr[int] { 1,,2 }", "Map[str, int] { 1, 2 }",
                         "[1, 2]"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      char text[256];
      snprintf(text, sizeof(text), "main() { %s }\nafter() {}\n", bad[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_TRUE(v->diagnostics.count > 0);
      ASSERT_EQ_INT(2, v->program->count);
      for (size_t j = 0; j < v->diagnostics.count; j++)
        ASSERT_TRUE(v->diagnostics.items[j]->fixes == NULL);
    }
    const char *headers[] = {"if", "while", "for n in"};
    for (size_t i = 0; i < 3; i++) {
      char text[256];
      snprintf(text, sizeof(text),
               "main() { %s Arr[int] { 1, 2 } {} }\nafter() {}\n", headers[i]);
      Parsed *v = parse(&arena, text);
      ASSERT_TRUE(v->diagnostics.count > 0);
      ASSERT_EQ_INT(2, v->program->count);
      if (v->diagnostics.count)
        ASSERT_EQ_INT(0, strcmp("Wrap collection literals in conditions or "
                                "iterables in parentheses.",
                                v->diagnostics.items[0]->message));
    }
    Parsed *missing = parse(&arena, "main() { Arr[int] { 1");
    ASSERT_TRUE(missing->diagnostics.count > 0);
    if (missing->diagnostics.count) {
      ASSERT_EQ_INT(0, strcmp("Expected '}' after collection items.",
                              missing->diagnostics.items[0]->message));
      ASSERT_TRUE(missing->diagnostics.items[0]->fixes == NULL);
    }
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
  arena_destroy(&arena);
  RUN_TESTS();
}
