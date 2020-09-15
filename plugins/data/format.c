/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "allocator.h"
#include "ascii-ctype.h"
#include "cleanup.h"
#include "ispowerof2.h"
#include "minmax.h"
#include "rounding.h"
#include "strndup.h"
#include "vector.h"
#include "windows-compat.h"

#include "data.h"
#include "format.h"

/* To print the AST, use -D data.AST=1 */
int data_debug_AST = 0;

/* The abstract syntax tree. */
typedef struct expr expr_t;

DEFINE_VECTOR_TYPE(string, char); /* string + length, \0 allowed */

struct expr {
  enum {
    EXPR_LIST,                  /* list     - list of expressions */
    EXPR_BYTE,                  /* b        - single byte */
    EXPR_ABS_OFFSET,            /* ui       - absolute offset (@OFFSET) */
    EXPR_REL_OFFSET,            /* i        - relative offset (@+N or @-N) */
    EXPR_ALIGN_OFFSET,          /* ui       - align offset (@^ALIGNMENT) */
    EXPR_EXPR,                  /* expr     - nested expression */
    EXPR_FILE,                  /* filename - read a file */
    EXPR_SCRIPT,                /* script   - run script */
    EXPR_STRING,                /* string   - string + length */
    EXPR_NAME,                  /* name     - insert a named expression */
    EXPR_ASSIGN,                /* a.name, a.expr - assign name to expr */
    EXPR_REPEAT,                /* r.expr, r.n - expr * N */
    EXPR_SLICE,                 /* sl.expr, sl.n, sl.m - expr[N:M] */
  } t;
  union {
    struct generic_vector /* really expr_list */ list;
    uint8_t b;
    int64_t i;
    uint64_t ui;
    expr_t *expr;
    char *filename;
    char *script;
    string string;
    char *name;
    struct {
      char *name;
      expr_t *expr;
    } a;
    struct {
      expr_t *expr;
      uint64_t n;
    } r;
    struct {
      expr_t *expr;
      uint64_t n;
      int64_t m;
    } sl;
  };
};
DEFINE_VECTOR_TYPE(expr_list, expr_t);

static void free_expr (expr_t *e);

static void
free_expr_fields (expr_t *e)
{
  expr_list list;
  size_t i;

  if (e) {
    switch (e->t) {
    case EXPR_LIST:
      memcpy (&list, &e->list, sizeof list);
      for (i = 0; i < list.size; ++i)
        free_expr_fields (&list.ptr[i]);
      free (list.ptr);
      break;
    case EXPR_EXPR:   free_expr (e->expr); break;
    case EXPR_FILE:   free (e->filename); break;
    case EXPR_SCRIPT: free (e->script); break;
    case EXPR_STRING: free (e->string.ptr); break;
    case EXPR_NAME:   free (e->name); break;
    case EXPR_ASSIGN: free (e->a.name); free_expr (e->a.expr); break;
    case EXPR_REPEAT: free_expr (e->r.expr); break;
    case EXPR_SLICE:  free_expr (e->sl.expr); break;
    default: break;
    }
  }
}

static void
free_expr (expr_t *e)
{
  free_expr_fields (e);
  free (e);
}

/* Note this only does a shallow copy. */
static expr_t *
copy_expr (const expr_t *e)
{
  expr_t *r;

  r = malloc (sizeof *r);
  if (r == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  memcpy (r, e, sizeof *r);
  return r;
}

#define CLEANUP_FREE_EXPR __attribute__((cleanup (cleanup_free_expr)))
static void
cleanup_free_expr (expr_t **ptr)
{
  free_expr (*ptr);
}

static const char *
debug_indent (int level)
{
  static const char spaces[41] = "                                        ";

  if (level >= 10)
    return spaces;
  else
    return &spaces[40-4*level];
}

static void
debug_expr (expr_t *e, int level)
{
  expr_list list;
  size_t i;

  switch (e->t) {
  case EXPR_LIST:
    memcpy (&list, &e->list, sizeof list);
    nbdkit_debug ("%s[", debug_indent (level));
    for (i = 0; i < list.size; ++i)
      debug_expr (&list.ptr[i], level+1);
    nbdkit_debug ("%s]", debug_indent (level));
    break;
  case EXPR_BYTE:
    nbdkit_debug ("%s%" PRIu8, debug_indent (level), e->b);
    break;
  case EXPR_ABS_OFFSET:
    nbdkit_debug ("%s@%" PRIu64, debug_indent (level), e->ui);
    break;
  case EXPR_REL_OFFSET:
    nbdkit_debug ("%s@%" PRIi64, debug_indent (level), e->i);
    break;
  case EXPR_ALIGN_OFFSET:
    nbdkit_debug ("%s@^%" PRIi64, debug_indent (level), e->ui);
    break;
  case EXPR_EXPR:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->expr, level+1);
    nbdkit_debug ("%s)", debug_indent (level));
    break;
  case EXPR_FILE:
    nbdkit_debug ("%s<%s", debug_indent (level), e->filename);
    break;
  case EXPR_SCRIPT:
    nbdkit_debug ("%s<(%s)", debug_indent (level), e->script);
    break;
  case EXPR_STRING:
    nbdkit_debug ("%s\"<STRING>\"", debug_indent (level));
    break;
  case EXPR_NAME:
    nbdkit_debug ("%s\\%s", debug_indent (level), e->name);
    break;
  case EXPR_ASSIGN:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->a.expr, level+1);
    nbdkit_debug ("%s) -> \\%s", debug_indent (level), e->a.name);
    break;
  case EXPR_REPEAT:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->r.expr, level+1);
    nbdkit_debug ("%s) *%" PRIu64, debug_indent (level), e->r.n);
    break;
  case EXPR_SLICE:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->sl.expr, level+1);
    nbdkit_debug ("%s)[%" PRIu64 ":%" PRIi64 "]",
                  debug_indent (level), e->sl.n, e->sl.m);
    break;
  }
}

/* Is the expression something which contains data, or something else
 * (like an offset).  Just a light check to reject nonsense
 * expressions like "@0 * 10".
 */
static bool
is_data_expr (const expr_t *e)
{
  return e->t != EXPR_ABS_OFFSET && e->t != EXPR_REL_OFFSET
    && e->t != EXPR_ALIGN_OFFSET;
}

/* Simple dictionary of name -> expression. */
typedef struct dict dict_t;
struct dict {
  dict_t *next;
  const char *name;             /* Name excluding \ character. */
  const expr_t *expr;           /* Associated expression. */
};

static expr_t *parser (int level, const char *value, size_t *start, size_t len);
static int evaluate (const dict_t *dict, const expr_t *expr,
                     struct allocator *a,
                     uint64_t *offset, uint64_t *size);

int
read_data_format (const char *value, struct allocator *a, uint64_t *size_rtn)
{
  size_t i = 0;
  CLEANUP_FREE_EXPR expr_t *expr = NULL;
  uint64_t offset = 0;

  /* Run the parser across the entire string, returning the top level
   * expression.
   */
  expr = parser (0, value, &i, strlen (value));
  if (!expr)
    return -1;

  if (data_debug_AST)
    debug_expr (expr, 0);

  /* Evaluate the expression into the allocator. */
  return evaluate (NULL, expr, a, &offset, size_rtn);
}

static int parse_string (const char *value, size_t *start, size_t len,
                         string *rtn);
static size_t get_name (const char *value, size_t i, size_t len,
                        size_t *initial);
static size_t get_var (const char *value, size_t i, size_t len,
                       size_t *initial);
static size_t get_script (const char *value, size_t i, size_t len);

/* This is the format parser.  It returns an expression that must be
 * freed by the caller (or NULL in case of an error).
 */
static expr_t *
parser (int level, const char *value, size_t *start, size_t len)
{
  size_t i = *start;
  /* List of expr_t that we are building up at this level.  This is
   * leaked on error paths, but we're going to call exit(1).
   */
  expr_list list = empty_vector;

  while (i < len) {
    /* Used as a scratch buffer while creating an expr to append to list. */
    expr_t e = { 0 };
#define APPEND_EXPR                          \
    if (expr_list_append (&list, e) == -1) { \
      nbdkit_error ("nbdkit_realloc: %m");   \
      return NULL;                           \
    }
    expr_t *ep;
    int j, n;
    int64_t i64;
    size_t flen;

    switch (value[i]) {
    case '#':                   /* # comment */
      i++;
      while (i < len && value[i] != '\n')
        i++;
      break;

    case '@':                   /* @OFFSET */
      if (++i == len) goto parse_error;
      switch (value[i]) {
      case '+':                 /* @+N */
        if (++i == len) goto parse_error;
        e.t = EXPR_REL_OFFSET;
        if (sscanf (&value[i], "%" SCNi64 "%n", &e.i, &n) == 1) {
          if (e.i < 0) {
            nbdkit_error ("data parameter after @+ must not be negative");
            return NULL;
          }
          i += n;
          APPEND_EXPR;
        }
        else
          goto parse_error;
        break;
      case '-':                 /* @-N */
        if (++i == len) goto parse_error;
        e.t = EXPR_REL_OFFSET;
        if (sscanf (&value[i], "%" SCNi64 "%n", &e.i, &n) == 1) {
          if (e.i < 0) {
            nbdkit_error ("data parameter after @- must not be negative");
            return NULL;
          }
          i += n;
          e.i = -e.i;
          APPEND_EXPR;
        }
        else
          goto parse_error;
        break;
      case '^':                 /* @^ALIGNMENT */
        if (++i == len) goto parse_error;
        e.t = EXPR_ALIGN_OFFSET;
        /* We must use %i into i64 in order to parse 0x etc. */
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter after @^ must not be negative");
            return NULL;
          }
          e.ui = (uint64_t) i64;
          /* XXX fix this arbitrary restriction */
          if (!is_power_of_2 (e.ui)) {
            nbdkit_error ("data parameter @^%" PRIu64 " must be a power of 2",
                          e.ui);
            return NULL;
          }
          i += n;
          APPEND_EXPR;
        }
        else
          goto parse_error;
        break;
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        e.t = EXPR_ABS_OFFSET;
        /* We must use %i into i64 in order to parse 0x etc. */
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter @OFFSET must not be negative");
            return NULL;
          }
          e.ui = (uint64_t) i64;
          i += n;
          APPEND_EXPR;
        }
        else
          goto parse_error;
        break;
      default:
        goto parse_error;
      }
      break;

    case '(':                   /* ( */
      i++;

      /* Call self recursively. */
      ep = parser (level+1, value, &i, len);
      if (ep == NULL)
        return NULL;
      e.t = EXPR_EXPR;
      e.expr = ep;
      APPEND_EXPR;
      break;

    case '*':                   /* expr*N */
      i++;
      if (list.size == 0) {
        nbdkit_error ("*N must follow an expression");
        return NULL;
      }
      if (! is_data_expr (&list.ptr[list.size-1])) {
        nbdkit_error ("*N cannot be applied to this type of expression");
        return NULL;
      }
      e.t = EXPR_REPEAT;
      if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
        if (i64 < 0) {
          nbdkit_error ("data parameter @OFFSET must not be negative");
          return NULL;
        }
        i += n;
      }
      else {
        nbdkit_error ("*N not numeric");
        return NULL;
      }
      e.r.n = (uint64_t) i64;
      e.r.expr = copy_expr (&list.ptr[list.size-1]);
      if (e.r.expr == NULL) return NULL;
      list.size--;
      APPEND_EXPR;
      break;

    case '[':                 /* expr[k:m] */
      i++;
      if (list.size == 0) {
        nbdkit_error ("[N:M] must follow an expression");
        return NULL;
      }
      if (! is_data_expr (&list.ptr[list.size-1])) {
        nbdkit_error ("[N:M] cannot be applied to this type of expression");
        return NULL;
      }
      e.t = EXPR_SLICE;
      i64 = 0;
      e.sl.m = -1;
      if (sscanf (&value[i], "%" SCNi64 ":%" SCNi64 "]%n",
                  &i64, &e.sl.m, &n) == 2 ||
          sscanf (&value[i], ":%" SCNi64 "]%n", &e.sl.m, &n) == 1 ||
          sscanf (&value[i], "%" SCNi64 ":]%n", &i64, &n) == 1)
        i += n;
      else if (strncmp (&value[i], ":]", 2) == 0)
        i += 2;
      else {
        nbdkit_error ("enclosed pattern (...)[N:M] not numeric");
        return NULL;
      }
      e.sl.n = i64;
      e.sl.expr = copy_expr (&list.ptr[list.size-1]);
      if (e.sl.expr == NULL) return NULL;
      list.size--;
      APPEND_EXPR;
      break;

    case '<':
      if (i+1 < len && value[i+1] == '(') { /* <(SCRIPT) */
        i += 2;

        e.t = EXPR_SCRIPT;
        flen = get_script (value, i, len);
        if (flen == 0) goto parse_error;
        e.script = strndup (&value[i], flen);
        if (e.script == NULL) {
          nbdkit_error ("strndup: %m");
          return NULL;
        }
        i += flen + 1;          /* +1 for trailing ) */
      }
      else {                    /* <FILE */
        i++;

        e.t = EXPR_FILE;
        /* The filename follows next in the string. */
        flen = strcspn (&value[i], "*[) \t\n");
        if (flen == 0) {
          nbdkit_error ("data parameter <FILE not a filename");
          return NULL;
        }
        e.filename = strndup (&value[i], flen);
        if (e.filename == NULL) {
          nbdkit_error ("strndup: %m");
          return NULL;
        }
        i += flen;
      }
      APPEND_EXPR;
      break;

    case '"':                   /* "String" */
      i++;
      e.t = EXPR_STRING;
      if (parse_string (value, &i, len, &e.string) == -1)
        return NULL;
      APPEND_EXPR;
      break;

    case '\\':                  /* \\NAME */
      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      e.t = EXPR_NAME;
      e.name = strndup (&value[i], flen);
      if (e.name == NULL) {
        nbdkit_error ("strndup: %m");
        return NULL;
      }
      i += flen;
      APPEND_EXPR;
      break;

    case '-':                   /* -> \\NAME */
      i++;
      if (value[i] != '>') goto parse_error;
      i++;
      if (list.size == 0) {
        nbdkit_error ("-> must follow an expression");
        return NULL;
      }
      if (! is_data_expr (&list.ptr[list.size-1])) {
        nbdkit_error ("-> cannot be applied to this type of expression");
        return NULL;
      }
      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      e.t = EXPR_ASSIGN;
      e.a.name = strndup (&value[i], flen);
      if (e.a.name == NULL) {
        nbdkit_error ("strndup: %m");
        return NULL;
      }
      e.a.expr = copy_expr (&list.ptr[list.size-1]);
      if (e.a.expr == NULL) return NULL;
      i += flen;
      list.size--;
      APPEND_EXPR;
      break;

    case '$': {                 /* $VAR */
      CLEANUP_FREE char *name = NULL;
      const char *content;
      size_t ci;

      flen = get_var (value, i, len, &i);
      if (flen == 0) goto parse_error;
      name = strndup (&value[i], flen);
      if (name == NULL) {
        nbdkit_error ("strndup: %m");
        return NULL;
      }
      i += flen;

      /* Look up the variable. */
      content = get_extra_param (name);
      if (!content) {
        content = getenv (name);
        if (!content) {
          nbdkit_error ("$%s: variable not found", name);
          return NULL;
        }
      }

      /* Call self recursively on the variable content. */
      ci = 0;
      ep = parser (0, content, &ci, strlen (content));
      if (ep == NULL)
        return NULL;
      e.t = EXPR_EXPR;
      e.expr = ep;
      APPEND_EXPR;
      break;
    }

    case '0': case '1': case '2': case '3': case '4': /* BYTE */
    case '5': case '6': case '7': case '8': case '9':
      e.t = EXPR_BYTE;
      /* We need to use %i here so it scans 0x etc correctly. */
      if (sscanf (&value[i], "%i%n", &j, &n) == 1)
        i += n;
      else
        goto parse_error;
      if (j < 0 || j > 255) {
        nbdkit_error ("data parameter BYTE must be in the range 0..255");
        return NULL;
      }
      e.b = j;
      APPEND_EXPR;
      break;

    case ')':                   /* ) */
      if (level < 1) {
        nbdkit_error ("unmatched ')' in data string");
        return NULL;
      }
      i++;
      goto out;

    case ' ': case '\t': case '\n': /* Skip whitespace. */
    case '\f': case '\r': case '\v':
      i++;
      break;

    default:
    parse_error:
      nbdkit_error ("data parameter: parsing error at offset %zu", i);
      return NULL;
    } /* switch */
  } /* for */

  /* If we reach the end of the string and level != 0 that means
   * there is an unmatched '(' in the string.
   */
  if (level > 0) {
    nbdkit_error ("unmatched '(' in data string");
    return NULL;
  }

 out:
  *start = i;

  /* Return a new expression node. */
  expr_t e2 = { .t = EXPR_LIST };
  memcpy (&e2.list, &list, sizeof e2.list);
  return copy_expr (&e2);
}

/* Return the next \NAME in the input.  This skips whitespace, setting
 * *initial to the index of the start of the NAME (minus backslash).
 * It returns the length of NAME (minus backslash), or 0 if not found.
 */
static size_t
get_name (const char *value, size_t i, size_t len, size_t *initial)
{
  size_t r = 0;

  while (i < len) {
    if (!ascii_isspace (value[i]))
      break;
    i++;
  }

  if (i >= len || value[i] != '\\') return 0;
  i++;
  if (i >= len) return 0;
  *initial = i;

  while (i < len &&
         (ascii_isalnum (value[i]) || value[i] == '_' || value[i] == '-')) {
    i++;
    r++;
  }

  return r;
}

/* Like get_name above, but for $VAR variables.  The accepted variable
 * name is /\$[a-z_][a-z0-9_]+/i
 */
static size_t
get_var (const char *value, size_t i, size_t len, size_t *initial)
{
  size_t r = 0;

  while (i < len) {
    if (!ascii_isspace (value[i]))
      break;
    i++;
  }

  if (i >= len || value[i] != '$') return 0;
  i++;
  if (i >= len) return 0;
  *initial = i;

  if (!ascii_isalpha (value[i]) && value[i] != '_')
    return 0;

  while (i < len && (ascii_isalnum (value[i]) || value[i] == '_')) {
    i++;
    r++;
  }

  return r;
}

/* Find end of a <(SCRIPT), ignoring nested (). */
static size_t
get_script (const char *value, size_t i, size_t len)
{
  int lvl = 0;
  size_t r = 0;

  for (; i < len; ++i, ++r) {
    if (value[i] == '(')
      lvl++;
    else if (value[i] == ')') {
      if (lvl > 0)
        lvl--;
      else
        break;
    }
  }

  if (i >= len)
    return 0;

  return r;
}

/* Parse a "String" with C-like escaping, and store it in the string
 * vector.  When this is called we have already consumed the initial
 * double quote character.
 */
static unsigned char
hexdigit (const char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else /* if (c >= 'A' && c <= 'F') */
    return c - 'A' + 10;
}

static int
parse_string (const char *value, size_t *start, size_t len, string *rtn)
{
  size_t i = *start;
  unsigned char c, x0, x1;

  *rtn = (string) empty_vector;

  for (; i < len; ++i) {
    c = value[i];
    switch (c) {
    case '"':
      /* End of the string. */
      *start = i+1;
      return 0;

    case '\\':
      /* Start of escape sequence. */
      if (++i == len) goto unexpected_end_of_string;
      c = value[i];
      switch (c) {
      case 'a': c = 0x7; break;
      case 'b': c = 0x8; break;
      case 'f': c = 0xc; break;
      case 'n': c = 0xa; break;
      case 'r': c = 0xd; break;
      case 't': c = 0x9; break;
      case 'v': c = 0xb; break;
      case '\\': case '"': break;
      case 'x':
        if (++i == len) goto unexpected_end_of_string;
        x0 = value[i];
        if (++i == len) goto unexpected_end_of_string;
        x1 = value[i];
        if (!ascii_isxdigit (x0) || !ascii_isxdigit (x1)) {
          nbdkit_error ("data: \\xNN must be followed by exactly "
                        "two hexadecimal characters");
          return -1;
        }
        c = hexdigit (x0) * 16 + hexdigit (x1);
        break;
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
      case 'u':
        nbdkit_error ("data: string numeric and unicode sequences "
                      "are not yet implemented");
        return -1;
      }
      /*FALLTHROUGH*/
    default:
      /* Any other character is added to the string. */
      if (string_append (rtn, c) == -1) {
        nbdkit_error ("realloc: %m");
        return -1;
      }
    }
  }

  /* If we reach here then we have run off the end of the data string
   * without finding the final quote.
   */
 unexpected_end_of_string:
  nbdkit_error ("data parameter: unterminated string");
  return -1;
}

static int store_file (struct allocator *a,
                       const char *filename, uint64_t *offset);
static int store_file_slice (struct allocator *a,
                             const char *filename,
                             uint64_t skip, int64_t end, uint64_t *offset);
static int store_script (struct allocator *a,
                         const char *script, uint64_t *offset);
static int store_script_len (struct allocator *a,
                             const char *script,
                             int64_t len, uint64_t *offset);

/* This is the evaluator.  It takes a parsed expression (expr_t) and
 * evaulates it into the allocator.
 */
static int
evaluate (const dict_t *dict, const expr_t *e,
          struct allocator *a, uint64_t *offset, uint64_t *size)
{
  /* 'd' is the local dictionary for this function.  Assignments are
   * added to the dictionary in this scope and passed to nested
   * scopes.  This is leaked on error paths, but we're going to call
   * exit(1).
   */
  dict_t *d = (dict_t *) dict;
  expr_list list = empty_vector;
  size_t i, j;

  if (e->t == EXPR_LIST)
    memcpy (&list, &e->list, sizeof list);
  else {
    list.size = 1;
    list.ptr = (expr_t *) e;
  }

  for (i = 0; i < list.size; ++i) {
    e = &list.ptr[i];

    switch (e->t) {
    case EXPR_LIST: abort ();

    case EXPR_BYTE:
      /* Store the byte. */
      if (a->f->write (a, &e->b, 1, *offset) == -1)
        return -1;
      (*offset)++;
      break;

    case EXPR_ABS_OFFSET:
      /* XXX Check it does not overflow 63 bits. */
      *offset = e->ui;
      break;

    case EXPR_REL_OFFSET:
      if (e->i < 0 && -e->i > *offset) {
        nbdkit_error ("data parameter @-%" PRIi64 " "
                      "must not be larger than the current offset %" PRIu64,
                      -e->i, *offset);
        return -1;
      }
      /* XXX Check it does not overflow 63 bits. */
      *offset += e->i;
      break;

    case EXPR_ALIGN_OFFSET:
      *offset = ROUND_UP (*offset, e->ui);
      break;

    case EXPR_FILE:
      if (store_file (a, e->filename, offset) == -1)
        return -1;
      break;

    case EXPR_SCRIPT:
      if (store_script (a, e->script, offset) == -1)
        return -1;
      break;

    case EXPR_STRING:
      /* Copy the string into the allocator. */
      if (a->f->write (a, e->string.ptr, e->string.size, *offset) == -1)
        return -1;
      *offset += e->string.size;
      break;

    case EXPR_ASSIGN: {
      dict_t *d_next = d;

      d = malloc (sizeof *d);
      if (d == NULL) {
        nbdkit_error ("malloc: %m");
        return -1;
      }
      d->next = d_next;
      d->name = e->a.name;
      d->expr = e->a.expr;
      break;
    }

    case EXPR_NAME: {
      CLEANUP_FREE_ALLOCATOR struct allocator *a2 = NULL;
      uint64_t offset2 = 0, size2 = 0;
      dict_t *t;

      /* Look up the expression in the current dictionary. */
      for (t = d; t != NULL; t = t->next)
        if (strcmp (t->name, e->name) == 0)
          break;
      if (t == NULL) {
        nbdkit_error ("\\%s not defined", e->name);
        return -1;
      }

      /* Evaluate and then substitute the expression. */
      a2 = create_allocator ("sparse", false);
      if (a2 == NULL) {
        nbdkit_error ("malloc: %m");
        return -1;
      }
      /* NB: We pass the environment at the time that the assignment was
       * made (t->next) not the current environment.  This is deliberate.
       */
      if (evaluate (t->next, t->expr, a2, &offset2, &size2) == -1)
        return -1;

      if (a->f->blit (a2, a, size2, 0, *offset) == -1)
        return -1;
      *offset += size2;
      break;
    }

    case EXPR_EXPR:
    case EXPR_REPEAT:
    case EXPR_SLICE:
      /* Optimize some cases so we don't always have to create a
       * new allocator.
       */

      /* BYTE*N was optimized in the previous ad hoc parser so it makes
       * sense to optimize it here.
       */
      if (e->t == EXPR_REPEAT && e->expr->t == EXPR_BYTE) {
        if (a->f->fill (a, e->expr->b, e->r.n, *offset) == -1)
          return -1;
        *offset += e->r.n;
      }

      /* <FILE[N:M] can be optimized by not reading in the whole file.
       * For files like /dev/urandom which are infinite this stops an
       * infinite loop.
       */
      else if (e->t == EXPR_SLICE && e->expr->t == EXPR_FILE) {
        if (store_file_slice (a, e->expr->filename,
                              e->sl.n, e->sl.m, offset) == -1)
          return -1;
      }

      /* <(SCRIPT)[:LEN] must be optimized by truncating the
       * output of the script.
       */
      else if (e->t == EXPR_SLICE && e->sl.n == 0 &&
               e->expr->t == EXPR_SCRIPT) {
        if (store_script_len (a, e->expr->script, e->sl.m, offset) == -1)
          return -1;
      }

      else {
        /* This is the non-optimized case.
         *
         * Nesting creates a new context where there is a new allocator
         * and the offset is reset to 0.
         */
        CLEANUP_FREE_ALLOCATOR struct allocator *a2 = NULL;
        uint64_t offset2 = 0, size2 = 0, m;

        a2 = create_allocator ("sparse", false);
        if (a2 == NULL) {
          nbdkit_error ("malloc: %m");
          return -1;
        }
        if (evaluate (d, e->expr, a2, &offset2, &size2) == -1)
          return -1;

        switch (e->t) {
        case EXPR_EXPR:
          if (a->f->blit (a2, a, size2, 0, *offset) == -1)
            return -1;
          *offset += size2;
          break;
        case EXPR_REPEAT:
          /* Duplicate the allocator a2 N times. */
          for (j = 0; j < e->r.n; ++j) {
            if (a->f->blit (a2, a, size2, 0, *offset) == -1)
              return -1;
            *offset += size2;
          }
          break;
        case EXPR_SLICE:
          /* Slice [N:M] */
          m = e->sl.m < 0 ? size2 : e->sl.m;
          if (e->sl.n < 0 || m < 0 ||
              e->sl.n > size2 || m > size2 ||
              e->sl.n > m ) {
            nbdkit_error ("[N:M] does not describe a valid slice");
            return -1;
          }
          /* Take a slice from the allocator. */
          if (a->f->blit (a2, a, m-e->sl.n, e->sl.n, *offset) == -1)
            return -1;
          *offset += m-e->sl.n;
          break;
        default:
          abort ();
        }
      }
      break;
    }

    /* Adjust the size if the offset is now larger. */
    if (*size < *offset)
      *size = *offset;
  } /* for */

  /* Free assignments in the local dictionary. */
  while (d != dict) {
    dict_t *t = d;
    d = d->next;
    free (t);
  }

  return 0;
}

/* Store file at current offset in the allocator, updating the offset. */
static int
store_file (struct allocator *a,
            const char *filename, uint64_t *offset)
{
  FILE *fp;
  char buf[BUFSIZ];
  size_t n;

  fp = fopen (filename, "r");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", filename);
    return -1;
  }

  while (!feof (fp)) {
    n = fread (buf, 1, BUFSIZ, fp);
    if (n > 0) {
      if (a->f->write (a, buf, n, *offset) == -1) {
        fclose (fp);
        return -1;
      }
    }
    if (ferror (fp)) {
      nbdkit_error ("fread: %s: %m", filename);
      fclose (fp);
      return -1;
    }
    (*offset) += n;
  }

  if (fclose (fp) == EOF) {
    nbdkit_error ("fclose: %s: %m", filename);
    return -1;
  }

  return 0;
}

/* <FILE[N:M] */
static int
store_file_slice (struct allocator *a,
                  const char *filename,
                  uint64_t skip, int64_t end, uint64_t *offset)
{
  FILE *fp;
  char buf[BUFSIZ];
  size_t n;
  uint64_t len = 0;

  if ((end >= 0 && skip > end) || end < -2) {
    nbdkit_error ("<FILE[N:M] does not describe a valid slice");
    return -1;
  }

  if (end >= 0)
    len = end - skip;

  fp = fopen (filename, "r");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", filename);
    return -1;
  }

  if (fseek (fp, skip, SEEK_SET) == -1) {
    nbdkit_error ("%s: fseek: %m", filename);
    return -1;
  }

  while (!feof (fp) && (end == -1 || len > 0)) {
    n = fread (buf, 1, end == -1 ? BUFSIZ : MIN (len, BUFSIZ), fp);
    if (n > 0) {
      if (a->f->write (a, buf, n, *offset) == -1) {
        fclose (fp);
        return -1;
      }
    }
    if (ferror (fp)) {
      nbdkit_error ("fread: %s: %m", filename);
      fclose (fp);
      return -1;
    }
    (*offset) += n;
    len -= n;
  }

  if (fclose (fp) == EOF) {
    nbdkit_error ("fclose: %s: %m", filename);
    return -1;
  }

  return 0;
}

#ifndef WIN32

/* Run the script and store the output in the allocator from offset. */
static int
store_script (struct allocator *a,
              const char *script, uint64_t *offset)
{
  FILE *pp;
  char buf[BUFSIZ];
  size_t n;

  pp = popen (script, "r");
  if (pp == NULL) {
    nbdkit_error ("popen: %m");
    return -1;
  }

  while (!feof (pp)) {
    n = fread (buf, 1, BUFSIZ, pp);
    if (n > 0) {
      if (a->f->write (a, buf, n, *offset) == -1) {
        pclose (pp);
        return -1;
      }
    }
    if (ferror (pp)) {
      nbdkit_error ("fread: %m");
      pclose (pp);
      return -1;
    }
    (*offset) += n;
  }

  if (pclose (pp) == EOF) {
    nbdkit_error ("pclose: %m");
    return -1;
  }

  return 0;
}

/* Run the script and store up to len bytes of the output in the
 * allocator from offset.
 */
static int
store_script_len (struct allocator *a,
                  const char *script,
                  int64_t len, uint64_t *offset)
{
  FILE *pp;
  char buf[BUFSIZ];
  size_t n;

  pp = popen (script, "r");
  if (pp == NULL) {
    nbdkit_error ("popen: %m");
    return -1;
  }

  while (!feof (pp) && len > 0) {
    n = fread (buf, 1, MIN (len, BUFSIZ), pp);
    if (n > 0) {
      if (a->f->write (a, buf, n, *offset) == -1) {
        pclose (pp);
        return -1;
      }
    }
    if (ferror (pp)) {
      nbdkit_error ("fread: %m");
      pclose (pp);
      return -1;
    }
    (*offset) += n;
    len -= n;
  }

  if (pclose (pp) == EOF) {
    nbdkit_error ("pclose: %m");
    return -1;
  }

  return 0;
}

#else /* WIN32 */

static int
store_script (struct allocator *a,
              const char *script, uint64_t *offset)
{
  NOT_IMPLEMENTED_ON_WINDOWS ("<(SCRIPT)");
}

static int
store_script_len (struct allocator *a,
                  const char *script,
                  int64_t len, uint64_t *offset)
{
  NOT_IMPLEMENTED_ON_WINDOWS ("<(SCRIPT)");
}

#endif /* WIN32 */
