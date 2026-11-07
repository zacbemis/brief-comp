#include "codegen/c.h"
#include "lexer/lexer.h"
#include "lexer/newline.h"
#include "parser/parser.h"
#include "test_utils.h"

static const char *prefix =
    "Result = :ok[int] | :!bad[str]\n"
    "f(n: int) -> Result { if n < 0 { ret :!bad[\"owned\"] }\nn }\n";
static const char *join_message =
    "Outcome binding is handled on some paths but not others.";
static const char *exit_message =
    "Outcome binding is not handled before leaving its scope.";
typedef struct {
  SemaProgram *program;
  Diagnostics errors;
} Result;
static Result check(Arena *a, const char *text) {
  Result r;
  diagnostics_init(&r.errors, a);
  InternTable names;
  intern_init(&names, a);
  SourceFile *s = source_create(a, "paths.brf", text, strlen(text));
  Program *ast = parse_program(
      a, &names, newline_filter(a, lexer_tokenize(a, s, &r.errors)), &r.errors);
  r.program = sema_resolve(a, ast, &r.errors);
  sema_check(r.program, &r.errors);
  return r;
}
static char *newlines(Arena *a, const char *source, int crlf) {
  char *text = arena_alloc(a, 2 * strlen(source) + 1);
  size_t length = 0;
  for (const char *s = source; *s; s++) {
    if (crlf && *s == '\n')
      text[length++] = '\r';
    text[length++] = *s;
  }
  text[length] = 0;
  return text;
}
static void valid(Arena *a, const char *source) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = newlines(a, source, crlf);
    Result r = check(a, text);
    const SemaType **types = arena_calloc(a, r.program->count, sizeof(*types));
    for (size_t i = 0; i < r.program->count; i++)
      types[i] = r.program->functions[i].symbol.return_type;
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(0, r.errors.count);
      ASSERT_TRUE(r.program->checked && !strcmp(text, r.program->module->text));
      for (size_t i = 0; i < r.program->count; i++)
        ASSERT_TRUE(types[i] == r.program->functions[i].symbol.return_type);
    }
    IrProgram *ir = ir_lower(a, r.program);
    ASSERT_TRUE(ir != NULL);
    if (ir) {
      ir_insert_rc(a, ir);
      size_t count = ir->functions[ir->entry].count;
      ir_insert_rc(a, ir);
      ASSERT_EQ_INT(count, ir->functions[ir->entry].count);
      FILE *out = tmpfile();
      ASSERT_TRUE(out != NULL);
      if (out) {
        ASSERT_TRUE(emit_c(out, ir));
        fclose(out);
      }
    }
  }
}
static const char *location(const char *text, const char *site) {
  return site ? strstr(text, site) : strrchr(text, '}');
}
static void reject(Arena *a, const char *source, const char *message,
                   const char *site, const char *path_site, size_t width,
                   const char *path_message, const char *declaration) {
  for (int crlf = 0; crlf < 2; crlf++) {
    char *text = newlines(a, source, crlf);
    const char *at = location(text, site ? newlines(a, site, crlf) : NULL);
    const char *pending =
        path_site ? strstr(text, newlines(a, path_site, crlf)) : NULL;
    const char *decl = declaration ? strstr(text, declaration) : NULL;
    ASSERT_TRUE(at != NULL);
    if (path_site)
      ASSERT_TRUE(pending != NULL);
    if (declaration)
      ASSERT_TRUE(decl != NULL);
    Result r = check(a, text);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
      if (repeat) {
        diagnostics_init(&r.errors, a);
        sema_check(r.program, &r.errors);
      }
      if (r.errors.count != 1)
        diagnostics_print(&r.errors, stderr);
      ASSERT_EQ_INT(1, r.errors.count);
      ASSERT_TRUE(!r.program->checked && ir_lower(a, r.program) == NULL);
      ASSERT_TRUE(!strcmp(text, r.program->module->text));
      if (r.errors.count == 1) {
        Diagnostic *d = r.errors.items[0];
        ASSERT_EQ_INT(0, strcmp(message, d->message));
        if (at)
          ASSERT_EQ_INT((size_t)(at - text), d->span.start);
        ASSERT_TRUE(!d->fixes);
        DiagnosticNote *note = d->notes;
        if (path_site) {
          ASSERT_TRUE(note != NULL);
          if (note) {
            ASSERT_EQ_INT(0, strcmp(path_message, note->message));
            if (pending) {
              ASSERT_EQ_INT((size_t)(pending - text), note->span.start);
              ASSERT_EQ_INT((size_t)(pending - text) + width, note->span.end);
            }
            note = note->next;
          }
        }
        if (declaration) {
          ASSERT_TRUE(note != NULL);
          if (note) {
            ASSERT_EQ_INT(0, strcmp("The outcome binding is declared here.",
                                    note->message));
            if (decl)
              ASSERT_EQ_INT((size_t)(decl - text), note->span.start);
            note = note->next;
          }
        }
        ASSERT_TRUE(note == NULL);
      }
    }
  }
}
static char *program(Arena *a, const char *body) {
  size_t size = strlen(prefix) + strlen(body) + 32;
  char *text = arena_alloc(a, size);
  snprintf(text, size, "%smain() { %s\n}", prefix, body);
  return text;
}
int main(void) {
  Arena a;
  arena_init(&a);
  TEST("handled mutable outcomes can be rearmed and must be handled again") {
    const char *bodies[] = {
        "var r = f(1)\nr!\nr = f(2)\nr!",
        "var r: Result = f(1)\nr?\nr = :ok[2]\nr?",
        "var r = f(1)\nr |> { _ => () }\nr = f(2)\nret r",
        "var r = f(1)\nr = r |> { _ => f(2) }\nr!",
        ("var flag = true\nvar r = f(1)\nif flag { r! } "
         "else { r! }\nr = f(2)\nr!"),
        ("var flag = true\nvar r = f(1)\nr!\nif flag { r = "
         "f(2) } else { r = f(3) }\nr!"),
        "var r = f(1)\nfor i in 0..3 { r!\nr = f(i) }\nr!",
        "var r = f(1)\nr!\nfor i in 0..3 { r = f(i)\nr! }",
        ("var r = f(1)\nr!\nr = if true { f(2) } else { ret "
         ":!bad[\"early\"] }\nr!")};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); i++)
      valid(&a, program(&a, bodies[i]));
    reject(&a, program(&a, "var r = f(1)\nr!\nr = f(2)"), exit_message, NULL,
           NULL, 0, NULL, "var r =");
    reject(&a, program(&a, "var r = f(1)\nr = f(2)\nr!"),
           "Outcome binding cannot be reassigned.", "r = f(2)", NULL, 0, NULL,
           "var r =");
    reject(&a, program(&a, "let r = f(1)\nr!\nr = f(2)"),
           "Outcome binding cannot be reassigned.", "r = f(2)", NULL, 0, NULL,
           "let r =");
    reject(&a, program(&a, "var r = f(1)\nr!\nr = true"),
           "Assignment value has the wrong type.", "true", NULL, 0, NULL, NULL);
  }
  TEST("if and match report once at the join with an unhandled path witness") {
    reject(&a, program(&a, "var flag = true\nlet r = f(1)\nif flag { r! }\n()"),
           join_message, "if flag", "flag {", 4,
           "The outcome is still pending when this condition is false.",
           "let r =");
    reject(&a,
           program(&a, "var flag = true\nlet r = f(1)\nif flag { r! } else "
                       "{\n()\n} // pending\n()"),
           join_message, "if flag", "} // pending", 1,
           "The outcome is still pending on this path.", "let r =");
    reject(
        &a,
        program(
            &a,
            "var flag = true\nlet r = f(1)\nif flag {\n()\n} else { r! }\n()"),
        join_message, "if flag", "} else", 1,
        "The outcome is still pending on this path.", "let r =");
    reject(&a,
           program(&a, "var flag = true\nlet r = f(1)\nflag |> { true => { r! "
                       "}\nfalse => { () } }\n()"),
           join_message, "flag |>", "} }", 1,
           "The outcome is still pending on this path.", "let r =");
    reject(
        &a,
        program(&a,
                "var flag = true\nvar r = f(1)\nif flag { r! }\nr = f(2)\nr!"),
        join_message, "if flag", "flag {", 4,
        "The outcome is still pending when this condition is false.",
        "var r =");
  }
  TEST("logical short circuit labels the skipped path instead of the scope") {
    const char *logical = "Flag = :ok[bool] | :!bad\nf() -> Flag { true }\n";
    for (unsigned op = 0; op < 2; op++) {
      char text[1024];
      snprintf(
          text, sizeof(text),
          "%smain() { var flag = true\nlet r = f()\nlet value = flag %s r!\n}",
          logical, op ? "or" : "and");
      reject(&a, text, join_message, op ? "flag or" : "flag and",
             op ? "flag or" : "flag and", 4,
             op ? "The outcome is still pending when this condition is true."
                : "The outcome is still pending when this condition is false.",
             "let r =");
    }
  }
  TEST("loop joins have one primary, including after later reads or exits") {
    const char *tails[] = {"", "r!", "ret"};
    for (size_t i = 0; i < sizeof(tails) / sizeof(*tails); i++) {
      char body[1024];
      snprintf(body, sizeof(body),
               "var flag = true\nlet r = f(1)\nwhile flag { r! }\n%s",
               tails[i]);
      reject(&a, program(&a, body), join_message, "while flag", "flag {", 4,
             "The outcome is still pending on entry to this loop.", "let r =");
    }
    reject(&a, program(&a, "let r = f(1)\nfor i in 0..2 { r! }"), join_message,
           "for i", "0..2", 1,
           "The outcome is still pending on entry to this loop.", "let r =");
    reject(&a,
           program(&a, "var flag = true\nvar r = f(1)\nr!\nwhile flag { r = "
                       "f(2) } // back\n()"),
           join_message, "while flag", "} // back", 1,
           "The outcome is still pending on this loop back edge.", "var r =");
    reject(&a,
           program(&a, "var flag = true\nvar r = f(1)\nr!\nwhile flag { r = "
                       "f(2)\ncontinue }\n()"),
           join_message, "while flag", "continue", 8,
           "The outcome is still pending on this loop back edge.", "var r =");
  }
  TEST("nested and jump joins retain the first witness without cascades") {
    reject(&a,
           program(&a, "var flag = true\nvar other = true\nlet r = f(1)\n"
                       "while flag { if other { r! } }\n()"),
           join_message, "if other", "other {", 5,
           "The outcome is still pending when this condition is false.",
           "let r =");
    reject(&a,
           program(&a, "var flag = true\nvar other = true\nlet r = f(1)\n"
                       "if flag { if other { r! } } else { r! }\n()"),
           join_message, "if other", "other {", 5,
           "The outcome is still pending when this condition is false.",
           "let r =");
    reject(&a,
           program(&a, "var flag = true\nlet r = f(1)\n"
                       "while flag { r!\nbreak }\n()"),
           join_message, "while flag", "flag {", 4,
           "The outcome is still pending on entry to this loop.", "let r =");
    reject(&a,
           program(&a, "var flag = true\nlet r = f(1)\n"
                       "while true { if flag { r!\nbreak } else { break } // "
                       "pending\n}\n()"),
           join_message, "while true", "break } // pending", 5,
           "The outcome is still pending on this path.", "let r =");
  }
  TEST("departing scopes check their own obligations before jump snapshots") {
    reject(
        &a,
        program(&a, "var flag = true\nif flag { let r = f(1)\n} // inner\n()"),
        exit_message, "} // inner", NULL, 0, NULL, "let r =");
    reject(&a, program(&a, "let r = f(1)\nret\n"), exit_message, "ret\n", NULL,
           0, NULL, "let r =");
    reject(&a, program(&a, "let r = f(1)\nf(2)?\nr!"), exit_message, "f(2)?",
           NULL, 0, NULL, "let r =");
    reject(&a, program(&a, "while true { let r = f(1)\nbreak }"), exit_message,
           "break", NULL, 0, NULL, "let r =");
    reject(&a, program(&a, "for i in 0..2 { let r = f(1)\ncontinue }"),
           exit_message, "continue", NULL, 0, NULL, "let r =");
    reject(
        &a,
        program(
            &a,
            "var flag = true\nlet r = f(1)\nif flag { r! } else { ret\n}\n()"),
        exit_message, "ret\n", NULL, 0, NULL, "let r =");
    reject(&a, program(&a, "while true { var r = f(1)\nr!\nr = f(2)\nbreak }"),
           exit_message, "break", NULL, 0, NULL, "var r =");
    valid(&a, program(&a, "let r = f(1)\nwhile true { break }\nr!"));
    valid(&a, program(&a, "for i in 0..2 { let r = f(i)\nr!\ncontinue }"));
    valid(&a,
          program(
              &a,
              "var flag = true\nlet r = f(1)\nif flag { ret r } else { r! }"));
  }
  TEST("return and rearm RHS exits do not invent a pending replacement") {
    reject(
        &a,
        program(&a, "var r = f(1)\nr = f(2) |> { _ => ret :!bad[\"early\"] }"),
        exit_message, "ret :!bad[\"early\"]", NULL, 0, NULL, "var r =");
    reject(&a,
           program(&a, "var r = f(1)\nr!\nlet other = f(3)\nr = if true { f(2) "
                       "} else { ret :!bad[\"early\"] }\nother!\nr!"),
           exit_message, "ret :!bad[\"early\"]", NULL, 0, NULL, "let other =");
    valid(&a, "Box { text: str }\nR = :ok[Box] | :!bad\n"
              "source(box: Box) -> R { box }\nfresh() -> R { Box { text = "
              "\"new\" } }\n"
              "mutate(box: Box) { var r = source(box)\nlet old = r!\n"
              "r = fresh()\nlet new = r!\nnew.text = \"ok\" }\nmain() {}");
    reject(&a,
           "Box { text: str }\nR = :ok[Box] | :!bad\n"
           "source(box: Box) -> R { box }\nfresh() -> R { Box { text = \"new\" "
           "} }\n"
           "mutate(box: Box) { var r = fresh()\nr!\nr = source(box)\n"
           "let alias = r!\nalias.text = \"wrong\" }\nmain() {}",
           "Cannot mutate through an ordinary parameter; use &.", "alias.text",
           NULL, 0, NULL, NULL);
  }
  arena_destroy(&a);
  RUN_TESTS();
}
