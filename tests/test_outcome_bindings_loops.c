#include "compile_support.h"

static const char *prefix =
    "R = :ok[int] | :!bad[str]\n"
    "f(n: int) -> R { if n < 0 { ret :!bad[\"owned\"] }\nn }\n";
static const char *mixed =
    "Outcome binding is handled on some paths but not others.";
static const char *unhandled =
    "Outcome binding is not handled before leaving its scope.";
typedef struct {
  const char *message, *site, *path, *path_message, *declaration;
} Expected;
static char *program(Arena *a, const char *body) {
  size_t size = strlen(prefix) + strlen(body) + 32;
  char *text = arena_alloc(a, size);
  snprintf(text, size, "%smain() { %s\n}\n", prefix, body);
  return text;
}
static void at(const char *text, const char *site, Span span) {
  const char *p = strstr(text, site);
  ASSERT_TRUE(p != NULL);
  if (p)
    ASSERT_EQ_INT((size_t)(p - text), span.start);
  ASSERT_TRUE(span.end >= span.start && span.end <= strlen(text));
}
static void rejects(Arena *a, const char *source, const Expected *expected,
                    size_t count) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = lines(a, source, crlf);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != count)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(count, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      for (size_t i = 0; i < r.errors.count && i < count; i++) {
        Diagnostic *d = r.errors.items[i];
        const Expected *e = &expected[i];
        ASSERT_EQ_INT(0, strcmp(e->message, d->message));
        at(text, lines(a, e->site, crlf), d->span);
        ASSERT_TRUE(!d->fixes);
        DiagnosticNote *note = d->notes;
        if (e->path) {
          ASSERT_TRUE(note != NULL);
          if (note) {
            ASSERT_EQ_INT(0, strcmp(e->path_message, note->message));
            at(text, lines(a, e->path, crlf), note->span);
            note = note->next;
          }
        }
        if (e->declaration) {
          ASSERT_TRUE(note != NULL);
          if (note) {
            ASSERT_EQ_INT(0, strcmp("The outcome binding is declared here.",
                                    note->message));
            at(text, e->declaration, note->span);
            note = note->next;
          }
        }
        ASSERT_TRUE(note == NULL);
      }
    }
  }
}
static void rejects_one(Arena *a, const char *body, Expected expected) {
  rejects(a, program(a, body), &expected, 1);
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("outer values cannot be consumed again on normal or continue back "
       "edges") {
    const char *bodies[] = {"let r = f(1)\nwhile true { r! }",
                            "let r = f(1)\nwhile true { r!\ncontinue }",
                            "let r = f(1)\nfor i in 0..2 { r!\ncontinue }",
                            "let r = f(1)\nwhile r! > 0 {}",
                            ("var flag = true\nlet r = f(1)\nwhile true { r!\n"
                             "if flag { break } else { continue } }")};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      rejects_one(
          &a, bodies[i],
          (Expected){mixed, i == 2 ? "for i" : "while",
                     i == 2   ? "0..2"
                     : i == 3 ? "r! > 0"
                              : "true {",
                     "The outcome is still pending on entry to this loop.",
                     "let r ="});
  }
  TEST("range bounds run once and conditions can consume freshly rearmed "
       "values") {
    const char *bodies[] = {
        "let r = f(1)\nfor i in r!..3 {}",
        "let r = f(2)\nfor i in 0..r! {}",
        "let r = f(1)\nwhile true { r!\nbreak }",
        "let r = f(1)\nwhile r! > 0 { break }",
        "var r = f(1)\nwhile r! < 3 { r = f(3) }",
        "var r = f(1)\nfor i in 0..2 { r!\nr = f(i)\ncontinue }\nr!",
        ("var r = f(1)\nr!\nfor i in 0..2 { r = f(i)\nr!\n"
         "continue }")};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      accepts(&a, program(&a, bodies[i]));
    rejects_one(&a, "let r = f(2)\nfor i in 0..r! { r! }",
                (Expected){"Outcome binding has already been handled.", "r! }",
                           NULL, NULL, "let r ="});
  }
  TEST("range entry witnesses include outcomes produced by the bounds") {
    rejects_one(
        &a,
        "var flag = true\nvar r = f(1)\nr!\nfor i in "
        "(if flag { r = f(2)\n0 } else { r = f(3)\n0 })..2 { r! }",
        (Expected){
            mixed, "for i", "(if flag { r = f(2)\n0 } else { r = f(3)\n0 })",
            "The outcome is still pending on entry to this loop.", "var r ="});
  }
  TEST("jumps in nested conditions and bounds discharge the enclosing body") {
    const char *loops[] = {"for i in (if flag { break } else { 0 })..2 {}",
                           "for i in 0..(if flag { continue } else { 2 }) {}",
                           "while (if flag { break } else { false }) {}",
                           "while (if flag { continue } else { false }) {}"};
    for (size_t i = 0; i < sizeof(loops) / sizeof(*loops); i++) {
      char body[1024];
      snprintf(body, sizeof(body),
               "var flag = true\nwhile true { let r = f(1)\n%s\nr!\nbreak }",
               loops[i]);
      rejects_one(&a, body,
                  (Expected){unhandled,
                             i == 1 || i == 3 ? "continue" : "break } else",
                             NULL, NULL, "let r ="});
      snprintf(body, sizeof(body),
               "var flag = true\nwhile true { let r = f(1)\nr!\n%s\nbreak }",
               loops[i]);
      accepts(&a, program(&a, body));
    }
    rejects_one(&a,
                "var flag = true\nlet r = f(1)\nwhile true { for i in "
                "(if flag { r!\nbreak } else { break })..2 {} }",
                (Expected){mixed, "while true", "break })",
                           "The outcome is still pending on this path.",
                           "let r ="});
  }
  TEST("nested loops preserve local generations and the first inner join") {
    accepts(&a, program(&a, "let r = f(1)\nwhile true { while true { r!\n"
                            "break }\nbreak }"));
    accepts(&a, program(&a, "for i in 0..2 { let r = f(i)\nwhile true { r!\n"
                            "break }\ncontinue }"));
    accepts(
        &a,
        program(&a, "let r = f(1)\nfor i in 0..2 { let r = f(i)\nr! }\nr!"));
    rejects_one(
        &a, "let r = f(1)\nfor outer in 0..2 { for inner in 1..3 { r! } }\nr!",
        (Expected){mixed, "for inner", "1..3",
                   "The outcome is still pending on entry to this loop.",
                   "let r ="});
    rejects_one(
        &a,
        "var flag = true\nfor outer in 0..2 { let r = f(1)\n"
        "while flag { r! }\nr! }",
        (Expected){mixed, "while flag", "flag {",
                   "The outcome is still pending on entry to this loop.",
                   "let r ="});
    rejects_one(
        &a,
        "var flag = true\nfor outer in 0..2 { let r = f(1)\n"
        "for inner in 0..2 { if flag { r! } }\nr! }",
        (Expected){mixed, "if flag", "flag {",
                   "The outcome is still pending when this condition is false.",
                   "let r ="});
    rejects_one(
        &a,
        "let r = f(1)\nfor outer in 0..2 { r |> { :ok[v] => continue\n"
        ":!bad[s] => continue } }",
        (Expected){mixed, "for outer", "0..2",
                   "The outcome is still pending on entry to this loop.",
                   "let r ="});
    rejects_one(
        &a,
        "var flag = true\nfor outer in 0..2 { let r = f(1)\n"
        "let value = flag and r! > 0\nr! }",
        (Expected){mixed, "flag and", "flag and",
                   "The outcome is still pending when this condition is false.",
                   "let r ="});
    rejects_one(&a, "for outer in 0..2 { let r = f(1)\nf(2)?\nr! }",
                (Expected){unhandled, "f(2)?", NULL, NULL, "let r ="});
    accepts(&a, program(&a, "while true { let r = f(1)\nret r }"));
  }
  TEST("reporting recovery preserves independent bindings and typing errors") {
    rejects_one(
        &a, "let r = f(1)\nlet copy = r\nwhile true { r! }",
        (Expected){
            "Outcome binding must be handled with ?, !, a match, or return.",
            "r\nwhile", NULL, NULL, "let r ="});
    const Expected errors[] = {
        {"Function argument has the wrong type.", "true)", NULL, NULL, NULL},
        {mixed, "while flag", "flag {",
         "The outcome is still pending on entry to this loop.", "let r ="},
        {unhandled, "continue", NULL, NULL, "let local ="}};
    rejects(&a,
            program(&a, "var flag = true\nlet r = f(1)\nwhile flag { r!\n"
                        "let local = f(2)\nf(true)!\ncontinue }\nr!"),
            errors, sizeof(errors) / sizeof(*errors));
    const Expected independent[] = {
        {mixed, "while flag", "flag {",
         "The outcome is still pending on entry to this loop.", "let r ="},
        {mixed, "while flag", "flag {",
         "The outcome is still pending on entry to this loop.", "let other ="}};
    rejects(&a,
            program(&a, "var flag = true\nlet r = f(1)\nlet other = f(2)\n"
                        "while flag { r!\nother! }\nr!\nother!"),
            independent, sizeof(independent) / sizeof(*independent));
    rejects_one(
        &a, "while true { let r = f(1)\nlet copy = r\nr!\ncontinue }",
        (Expected){
            "Outcome binding must be handled with ?, !, a match, or return.",
            "r\nr!", NULL, NULL, "let r ="});
    rejects_one(&a,
                "ret\nwhile true { let r = :!bad[\"dead\"]\nr = :!bad[\"x\"] }",
                (Expected){"Cannot reassign an immutable binding.",
                           "r = :!bad[\"x\"]", NULL, NULL, NULL});
    accepts(&a, program(&a, "ret\nwhile true { let r = f(1)\nr!\nr! }"));
  }
  TEST("nested outcome handling retains ordinary parameter permissions") {
    const char *source = "Box { text: str }\nR = :ok[Box] | :!bad\n"
                         "f(box: Box) -> R { box }\n"
                         "touch(box: Box) { for i in 0..2 { let r = f(box)\n"
                         "let alias = r!\nalias.text = \"bad\" } }\nmain() {}";
    const Expected error = {
        "Cannot mutate through an ordinary parameter; use &.", "alias.text",
        NULL, NULL, NULL};
    rejects(&a, source, &error, 1);
    accepts(&a, "Box { text: str }\nR = :ok[Box] | :!bad\n"
                "f(box: Box) -> R { box }\n"
                "touch(box: &Box) { for i in 0..2 { let r = f(box)\n"
                "let alias = r!\nalias.text = \"ok\" } }\nmain() {}");
  }
  TEST("whole-tree fixed points converge deeply and reset on each check") {
    char body[4096];
    size_t n = (size_t)snprintf(body, sizeof(body), "let r = f(1)\n");
    for (unsigned i = 0; i < 24; i++)
      n += (size_t)snprintf(body + n, sizeof(body) - n,
                            "for level%u in 0..2 {\n", i);
    n += (size_t)snprintf(body + n, sizeof(body) - n, "r!\n");
    for (unsigned i = 0; i < 24; i++)
      n += (size_t)snprintf(body + n, sizeof(body) - n, "}\n");
    ASSERT_TRUE(n < sizeof(body));
    rejects_one(
        &a, body,
        (Expected){mixed, "for level23", "0..2 {\nr!",
                   "The outcome is still pending on entry to this loop.",
                   "let r ="});
    accepts(&a,
            program(&a, "var r = f(1)\nfor outer in 0..2 { for inner in 0..2 "
                        "{ r!\nr = f(inner) } }\nr!"));
  }
  arena_destroy(&a);
  RUN_TESTS();
}
