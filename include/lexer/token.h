#ifndef TOKEN_H
#define TOKEN_H

#include "common/source.h"

typedef enum {
  // Special
  TOK_EOF,
  TOK_ERR,
  TOK_NEWLINE,

  // Identifiers and literals
  TOK_IDENT,
  TOK_INT_LIT,
  TOK_FLOAT_LIT,
  TOK_STR_LIT,
  TOK_CHAR_LIT,
  TOK_BOOL_LIT,

  // Keywords
  TOK_LET,   // let
  TOK_VAR,   // var
  TOK_CONST, // const

  // Keywords Types
  TOK_BOOL,  // bool
  TOK_INT,   // int
  TOK_UINT,  // uint
  TOK_SHRT,  // shrt
  TOK_USHRT, // ushrt
  TOK_LNG,   // lng
  TOK_ULNG,  // ulng
  TOK_FLT,   // flt
  TOK_DUB,   // dub
  TOK_CHR,   // chr
  TOK_STR,   // str
  TOK_SYM,   // Sym
  TOK_FN,    // Fn (function type, as in Fn<(int): bool>)
  TOK_SELF,  // Self

  // Keywords Definitions
  TOK_TYPE,   // type
  TOK_MOD,    // mod
  TOK_IMPORT, // import
  TOK_TRAIT,  // trait
  TOK_IMPL,   // impl
  TOK_PUB,    // pub
  TOK_OVR,    // ovr

  // Keywords Control Flow
  TOK_IF,       // if
  TOK_ELSE,     // else
  TOK_FOR,      // for
  TOK_IN,       // in
  TOK_WHILE,    // while
  TOK_BREAK,    // break
  TOK_CONTINUE, // continue
  TOK_RET,      // ret

  // Arithmetic
  TOK_EQUAL,   // =
  TOK_PLUS,    // +
  TOK_MINUS,   // -
  TOK_STAR,    // *
  TOK_SLASH,   // /
  TOK_PERCENT, // %

  TOK_PLUS_PLUS,   // ++
  TOK_MINUS_MINUS, // --
  TOK_STAR_STAR,   // **

  TOK_PLUS_EQUAL,    // +=
  TOK_MINUS_EQUAL,   // -=
  TOK_STAR_EQUAL,    // *=
  TOK_SLASH_EQUAL,   // /=
  TOK_PERCENT_EQUAL, // %=

  TOK_EQUAL_EQUAL,        // ==
  TOK_BANG_EQUAL,         // !=
  TOK_LESS_THAN,          // <
  TOK_LESS_THAN_EQUAL,    // <=
  TOK_GREATER_THAN,       // >
  TOK_GREATER_THAN_EQUAL, // >=

  TOK_BANG,     // ! (logical not, postfix panic)
  TOK_QUESTION, // ? (postfix error propagation)
  TOK_AND,      // and
  TOK_OR,       // or

  // Bitwise
  TOK_TILDE,   // ~
  TOK_BIT_AND, // & (bitwise and, mutable borrow)
  TOK_BIT_OR,  // |
  TOK_BIT_XOR, // ^
  TOK_LSHIFT,  // <<
  TOK_RSHIFT,  // >>

  TOK_FAT_ARROW, // =>
  TOK_ARROW,     // ->
  TOK_PIPE,      // |>

  TOK_SEMI,      // ;
  TOK_SEMI_SEMI, // ;;
  TOK_COLON,     // : (type annotation, field, symbol prefix as in :ok)
  TOK_COMMA,     // ,
  TOK_PERIOD,    // .
  TOK_DOT_DOT,   // .. (range, end exclusive)
  TOK_LPAREN,    // (
  TOK_RPAREN,    // )
  TOK_LBRACE,    // {
  TOK_RBRACE,    // }
  TOK_LBRACKET,  // [
  TOK_RBRACKET,  // ]
} TokenType;

typedef struct {
  TokenType type;
  Span span;
  const char *start; // Source slice, including for TOK_ERR.
  size_t length;
  size_t line;         // Starting line; span is the authoritative location.
  const char *message; // Only set for TOK_ERR, never stored in start.
} Token;

typedef struct {
  Token *items;
  size_t count; // Includes errors and the final EOF token.
} TokenArray;

const char *token_type_name(TokenType type);

#endif
