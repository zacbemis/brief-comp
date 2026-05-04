#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

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
  TOK_LET, // let
  TOK_VAR, // var
  TOK_MUT, // mut
  TOK_SHR, // shr

  // Keywords Types
  TOK_UNIT,  // unit
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

  // Keywords Definitions
  TOK_STRUCT, // struct
  TOK_ENUM,   // enum

  // Keywords Control Flow
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

  TOK_BANG,    // !
  TOK_AND_AND, // &&
  TOK_OR_OR,   // ||

  // Bitwise
  TOK_TILDE,  // ~
  TOK_AND,    // &
  TOK_OR,     // |
  TOK_XOR,    // ^
  TOK_LSHIFT, // <<
  TOK_RSHIFT, // >>

  TOK_FAT_ARROW, // =>
  TOK_ARROW,     // ->
  TOK_RETURN,    // ret

  TOK_SEMI,     // ;
  TOK_COLON,    // :
  TOK_COMMA,    // ,
  TOK_PERIOD,   // .
  TOK_DOT_DOT,  // ..
  TOK_LPAREN,   // (
  TOK_RPAREN,   // )
  TOK_LBRACE,   // {
  TOK_RBRACE,   // }
  TOK_LBRACKET, // [
  TOK_RBRACKET, // ]
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  size_t length;
  int line;
} Token;

const char *token_type_name(TokenType type);

#endif
