/* nbdkit
 * Copyright (C) 2018-2021 Red Hat Inc.
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
#include <assert.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "allocator.h"
#include "ascii-ctype.h"
#include "byte-swapping.h"
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
NBDKIT_DLL_PUBLIC int data_debug_AST = 0;

/* The abstract syntax tree. */
typedef struct expr expr_t;

DEFINE_VECTOR_TYPE(string, char); /* string + length, \0 allowed */

#define CLEANUP_FREE_STRING \
  __attribute__((cleanup (cleanup_free_string)))

static void
cleanup_free_string (string *v)
{
  free (v->ptr);
}

typedef size_t node_id;         /* references a node in expr_table below */
DEFINE_VECTOR_TYPE(node_ids, node_id);

struct expr {
  enum {
    EXPR_NULL = 0,              /* null expression, no effect */
    EXPR_LIST,                  /* list     - list of node IDs */
    EXPR_BYTE,                  /* b        - single byte */
    EXPR_ABS_OFFSET,            /* ui       - absolute offset (@OFFSET) */
    EXPR_REL_OFFSET,            /* i        - relative offset (@+N or @-N) */
    EXPR_ALIGN_OFFSET,          /* ui       - align offset (@^ALIGNMENT) */
    EXPR_EXPR,                  /* id       - nested expression */
    EXPR_FILE,                  /* filename - read a file */
    EXPR_SCRIPT,                /* script   - run script */
    EXPR_STRING,                /* string   - string + length */
    EXPR_FILL,                  /* fl.n, fl.b - fill same byte b, N times */
    EXPR_NAME,                  /* name     - insert a named expression */
    EXPR_ASSIGN,                /* a.name, a.id - assign name to expr */
    EXPR_REPEAT,                /* r.id, r.n - expr * N */
    EXPR_SLICE,                 /* sl.id, sl.n, sl.m - expr[N:M] */
  } t;
  union {
    node_ids list;
    uint8_t b;
    int64_t i;
    uint64_t ui;
    node_id id;
    char *filename;
    char *script;
    string string;
    struct {
      uint64_t n;
      uint8_t b;
    } fl;
    char *name;
    struct {
      char *name;
      node_id id;
    } a;
    struct {
      node_id id;
      uint64_t n;
    } r;
    struct {
      node_id id;
      uint64_t n;
      int64_t m;
    } sl;
  };
};

/* We store a list of expressions (expr_t) in a global table.
 *
 * When referencing one expression from another (eg. for EXPR_EXPR) we
 * refer to the index into this table (node_id) instead of pointing to
 * the expr_t directly.
 *
 * This allows us to have nodes which reference each other or are
 * shared or removed without having to worry about reference counting.
 * The whole table is freed when the plugin is unloaded.
 *
 * As an optimization, the zeroth element (node_id == 0) is a common
 * EXPR_NULL.  (This is optimized automatically, don't use node_id 0
 * explicitly, in particular because if the table hasn't yet been
 * allocated then there is no zeroth element).
 */
DEFINE_VECTOR_TYPE(expr_list, expr_t);
static expr_list expr_table;

/* Add expression to the table, returning the node_id. */
static node_id
new_node (const expr_t e)
{
  if (expr_table.size == 0) {
    static const expr_t enull = { .t = EXPR_NULL };
    if (expr_list_append (&expr_table, enull) == -1)
      goto out_of_memory;
  }
  if (e.t == EXPR_NULL)
    return 0;
  if (expr_list_append (&expr_table, e) == -1) {
  out_of_memory:
    nbdkit_error ("realloc");
    exit (EXIT_FAILURE);
  }
  return expr_table.size-1;
}

/* Get a pointer to an expression by node_id. */
static expr_t *
get_node (node_id id)
{
  assert (id < expr_table.size);
  return &expr_table.ptr[id];
}

static void
free_expr_table (void)
{
  size_t i;
  expr_t *e;

  for (i = 0; i < expr_table.size; ++i) {
    e = get_node (i);
    switch (e->t) {
    case EXPR_LIST:   free (e->list.ptr); break;
    case EXPR_FILE:   free (e->filename); break;
    case EXPR_SCRIPT: free (e->script); break;
    case EXPR_STRING: free (e->string.ptr); break;
    case EXPR_NAME:   free (e->name); break;
    case EXPR_ASSIGN: free (e->a.name); break;
    default: break;
    }
  }

  expr_list_reset (&expr_table);
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
debug_expr (node_id id, int level)
{
  const expr_t *e = get_node (id);
  size_t i;

  switch (e->t) {
  case EXPR_NULL:
    nbdkit_debug ("%snull", debug_indent (level));
    break;
  case EXPR_LIST:
    nbdkit_debug ("%s[", debug_indent (level));
    for (i = 0; i < e->list.size; ++i)
      debug_expr (e->list.ptr[i], level+1);
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
    debug_expr (e->id, level+1);
    nbdkit_debug ("%s)", debug_indent (level));
    break;
  case EXPR_FILE:
    nbdkit_debug ("%s<%s", debug_indent (level), e->filename);
    break;
  case EXPR_SCRIPT:
    nbdkit_debug ("%s<(%s)", debug_indent (level), e->script);
    break;
  case EXPR_STRING: {
    CLEANUP_FREE_STRING string s = empty_vector;
    static const char hex[] = "0123456789abcdef";

    for (i = 0; i < e->string.size; ++i) {
      char c = e->string.ptr[i];
      if (ascii_isprint ((char) c))
        string_append (&s, e->string.ptr[i]);
      else {
        string_append (&s, '\\');
        string_append (&s, 'x');
        string_append (&s, hex[(c & 0xf0) >> 4]);
        string_append (&s, hex[c & 0xf]);
      }
    }
    string_append (&s, '\0');
    nbdkit_debug ("%s\"%s\"", debug_indent (level), s.ptr);
    break;
  }
  case EXPR_FILL:
    nbdkit_debug ("%sfill(%" PRIu8 "*%" PRIu64 ")",
                  debug_indent (level), e->fl.b, e->fl.n);
    break;
  case EXPR_NAME:
    nbdkit_debug ("%s\\%s", debug_indent (level), e->name);
    break;
  case EXPR_ASSIGN:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->a.id, level+1);
    nbdkit_debug ("%s) -> \\%s", debug_indent (level), e->a.name);
    break;
  case EXPR_REPEAT:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->r.id, level+1);
    nbdkit_debug ("%s) *%" PRIu64, debug_indent (level), e->r.n);
    break;
  case EXPR_SLICE:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e->sl.id, level+1);
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
  node_id id;                   /* Associated expression. */
};

static int parser (int level, const char *value, size_t *start, size_t len,
                   node_id *root_rtn);
static int optimize_ast (node_id root, node_id *root_rtn);
static int evaluate (const dict_t *dict, node_id root,
                     struct allocator *a,
                     uint64_t *offset, uint64_t *size);

int
read_data_format (const char *value, struct allocator *a, uint64_t *size_rtn)
{
  size_t i = 0;
  node_id root;
  uint64_t offset = 0;
  int r = -1;

  assert (expr_table.size == 0);

  /* Run the parser across the entire string, returning the top level
   * expression.
   */
  if (parser (0, value, &i, strlen (value), &root) == -1)
    goto out;

  if (optimize_ast (root, &root) == -1)
    goto out;

  if (data_debug_AST)
    debug_expr (root, 0);

  /* Evaluate the expression into the allocator. */
  r = evaluate (NULL, root, a, &offset, size_rtn);

 out:
  free_expr_table ();
  return r;
}

static int parse_string (const char *value, size_t *start, size_t len,
                         string *rtn);
static int parse_word (const char *value, size_t *start, size_t len,
                       string *rtn);
static size_t get_name (const char *value, size_t i, size_t len,
                        size_t *initial);
static size_t get_var (const char *value, size_t i, size_t len,
                       size_t *initial);
static size_t get_script (const char *value, size_t i, size_t len);

/* This is the format parser. */
static int
parser (int level, const char *value, size_t *start, size_t len,
        node_id *rtn)
{
  size_t i = *start;
  /* List of node_ids that we are building up at this level.  This is
   * leaked on error paths, but we're going to call exit(1).
   */
  node_ids list = empty_vector;

  while (i < len) {
    /* Used as a scratch buffer while creating an expr to append to list. */
    expr_t e = { 0 };
#define APPEND_EXPR                             \
    do {                                        \
      node_id _id = new_node (e);               \
      if (node_ids_append (&list, _id) == -1) { \
        nbdkit_error ("realloc: %m");           \
        exit (EXIT_FAILURE);                    \
      }                                         \
    } while (0)
    node_id id;
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
            return -1;
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
            return -1;
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
            return -1;
          }
          e.ui = (uint64_t) i64;
          /* XXX fix this arbitrary restriction */
          if (!is_power_of_2 (e.ui)) {
            nbdkit_error ("data parameter @^%" PRIu64 " must be a power of 2",
                          e.ui);
            return -1;
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
            return -1;
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
      if (parser (level+1, value, &i, len, &id) == -1)
        return -1;
      e.t = EXPR_EXPR;
      e.id = id;
      APPEND_EXPR;
      break;

    case ')':                   /* ) */
      if (level < 1) {
        nbdkit_error ("unmatched ')' in data string");
        return -1;
      }
      i++;
      goto out;

    case '*':                   /* expr*N */
      i++;
      if (list.size == 0) {
        nbdkit_error ("*N must follow an expression");
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.size-1]))) {
        nbdkit_error ("*N cannot be applied to this type of expression");
        return -1;
      }
      e.t = EXPR_REPEAT;
      if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
        if (i64 < 0) {
          nbdkit_error ("data parameter @OFFSET must not be negative");
          return -1;
        }
        i += n;
      }
      else {
        nbdkit_error ("*N not numeric");
        return -1;
      }
      e.r.n = (uint64_t) i64;
      e.r.id = list.ptr[list.size-1];
      list.size--;
      APPEND_EXPR;
      break;

    case '[':                 /* expr[k:m] */
      i++;
      if (list.size == 0) {
        nbdkit_error ("[N:M] must follow an expression");
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.size-1]))) {
        nbdkit_error ("[N:M] cannot be applied to this type of expression");
        return -1;
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
        return -1;
      }
      e.sl.n = i64;
      e.sl.id = list.ptr[list.size-1];
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
          return -1;
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
          return -1;
        }
        e.filename = strndup (&value[i], flen);
        if (e.filename == NULL) {
          nbdkit_error ("strndup: %m");
          return -1;
        }
        i += flen;
      }
      APPEND_EXPR;
      break;

    case '"':                   /* "String" */
      i++;
      e.t = EXPR_STRING;
      if (parse_string (value, &i, len, &e.string) == -1)
        return -1;
      APPEND_EXPR;
      break;

    case '\\':                  /* \\NAME */
      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      e.t = EXPR_NAME;
      e.name = strndup (&value[i], flen);
      if (e.name == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
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
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.size-1]))) {
        nbdkit_error ("-> cannot be applied to this type of expression");
        return -1;
      }
      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      e.t = EXPR_ASSIGN;
      e.a.name = strndup (&value[i], flen);
      if (e.a.name == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
      }
      e.a.id = list.ptr[list.size-1];
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
        return -1;
      }
      i += flen;

      /* Look up the variable. */
      content = get_extra_param (name);
      if (!content) {
        content = getenv (name);
        if (!content) {
          nbdkit_error ("$%s: variable not found", name);
          return -1;
        }
      }

      /* Call self recursively on the variable content. */
      ci = 0;
      if (parser (0, content, &ci, strlen (content), &id) == -1)
        return -1;
      e.t = EXPR_EXPR;
      e.id = id;
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
        return -1;
      }
      e.b = j;
      APPEND_EXPR;
      break;

    case 'l': case 'b':         /* le or be + NN: + WORD */
      e.t = EXPR_STRING;
      if (parse_word (value, &i, len, &e.string) == -1)
        return -1;
      APPEND_EXPR;
      break;

    case ' ': case '\t': case '\n': /* Skip whitespace. */
    case '\f': case '\r': case '\v':
      i++;
      break;

    default:
    parse_error:
      nbdkit_error ("data parameter: parsing error at offset %zu", i);
      return -1;
    } /* switch */
  } /* for */

  /* If we reach the end of the string and level != 0 that means
   * there is an unmatched '(' in the string.
   */
  if (level > 0) {
    nbdkit_error ("unmatched '(' in data string");
    return -1;
  }

 out:
  *start = i;

  /* Return the node ID. */
  expr_t e2 = { .t = EXPR_LIST, .list = list };
  *rtn = new_node (e2);
  return 0;
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

/* Parse le<NN>:WORD be<NN>:WORD expressions.  These are parsed into strings. */
static int
parse_word (const char *value, size_t *start, size_t len, string *rtn)
{
  size_t i = *start;
  CLEANUP_FREE_STRING string copy = empty_vector;
  size_t n;
  enum { little, big } endian;
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;

  *rtn = (string) empty_vector;

  /* It's convenient to use the nbdkit_parse* functions below because
   * they deal already with overflow etc.  However these functions
   * require a \0-terminated string, so we have to copy what we are
   * parsing to a new string here.
   */
  while (i < len) {
    if (ascii_isspace (value[i]))
      break;
    i++;
  }
  n = i - *start;
  assert (n > 0);          /* must be because caller parsed 'l'/'b' */
  if (string_reserve (&copy, n + 1) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  memcpy (copy.ptr, &value[*start], n);
  copy.ptr[n] = '\0';
  copy.size = n + 1;
  *start = i;

  /* Reserve enough space in the return buffer for the longest
   * possible bitstring (64 bits / 8 bytes).
   */
  if (string_reserve (rtn, 8) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }

  /* Parse the rest of {le|be}{16|32|64}: */
  if (strncmp (copy.ptr, "le16:", 5) == 0) {
    endian = little; rtn->size = 2;
  }
  else if (strncmp (copy.ptr, "le32:", 5) == 0) {
    endian = little; rtn->size = 4;
  }
  else if (strncmp (copy.ptr, "le64:", 5) == 0) {
    endian = little; rtn->size = 8;
  }
  else if (strncmp (copy.ptr, "be16:", 5) == 0) {
    endian = big; rtn->size = 2;
  }
  else if (strncmp (copy.ptr, "be32:", 5) == 0) {
    endian = big; rtn->size = 4;
  }
  else if (strncmp (copy.ptr, "be64:", 5) == 0) {
    endian = big; rtn->size = 8;
  }
  else {
    nbdkit_error ("data parameter: expected \"le16/32/64:\" "
                  "or \"be16/32/64:\" at offset %zu", i);
    return -1;
  }

  /* Parse the word field into a host-order unsigned int. */
  switch (rtn->size) {
  case 2:
    if (nbdkit_parse_uint16_t ("data", &copy.ptr[5], &u16) == -1)
      return -1;
    break;
  case 4:
    if (nbdkit_parse_uint32_t ("data", &copy.ptr[5], &u32) == -1)
      return -1;
    break;
  case 8:
    if (nbdkit_parse_uint64_t ("data", &copy.ptr[5], &u64) == -1)
      return -1;
    break;
  default: abort ();
  }

  /* Now depending on the endianness and size, convert to the final
   * bitstring.
   */
  switch (endian) {
  case little:
    switch (rtn->size) {
    case 2:                     /* le16: */
      *((uint16_t *) rtn->ptr) = htole16 (u16);
      break;
    case 4:                     /* le32: */
      *((uint32_t *) rtn->ptr) = htole32 (u32);
      break;
    case 8:                     /* le64: */
      *((uint64_t *) rtn->ptr) = htole64 (u64);
      break;
    default: abort ();
    }
    break;

  case big:
    switch (rtn->size) {
    case 2:                     /* be16: */
      *((uint16_t *) rtn->ptr) = htobe16 (u16);
      break;
    case 4:                     /* be32: */
      *((uint32_t *) rtn->ptr) = htobe32 (u32);
      break;
    case 8:                     /* be64: */
      *((uint64_t *) rtn->ptr) = htobe64 (u64);
      break;
    default: abort ();
    }
  }

  return 0;
}

/* This simple optimization pass over the AST simplifies some
 * expressions.
 */
static bool expr_list_only_bytes (const expr_t *);
static bool expr_is_single_byte (const expr_t *, uint8_t *b);

static int
optimize_ast (node_id root, node_id *root_rtn)
{
  size_t i, j;
  node_id id;
  expr_t e = { 0 };

  switch (get_node (root)->t) {
  case EXPR_LIST:
    /* Optimize each element of the list. */
    e.t = EXPR_LIST;

    for (i = 0; i < get_node (root)->list.size; ++i) {
      id = get_node (root)->list.ptr[i];
      if (optimize_ast (id, &id) == -1)
        return -1;
      switch (get_node (id)->t) {
      case EXPR_NULL:
        /* null elements of a list can be ignored. */
        break;
      case EXPR_LIST:
        /* List within a list is flattened. */
        for (j = 0; j < get_node (id)->list.size; ++j) {
          if (node_ids_append (&e.list, get_node (id)->list.ptr[j]) == -1) {
          append_error:
            nbdkit_error ("realloc: %m");
            exit (EXIT_FAILURE);
          }
        }
        break;
      default:
        if (node_ids_append (&e.list, id) == -1) goto append_error;
      }
    }

    /* List of length 0 is replaced with null. */
    if (e.list.size == 0) {
      free (e.list.ptr);
      e.t = EXPR_NULL;
      *root_rtn = new_node (e);
      return 0;
    }

    /* List of length 1 is replaced with the first element. */
    if (e.list.size == 1) {
      id = e.list.ptr[0];
      free (e.list.ptr);
      *root_rtn = id;
      return 0;
    }

    /* List that contains only byte elements can be replaced by a string. */
    if (expr_list_only_bytes (&e)) {
      string s = empty_vector;
      for (i = 0; i < e.list.size; ++i) {
        char c = (char) get_node (e.list.ptr[i])->b;
        if (string_append (&s, c) == -1) {
          nbdkit_error ("realloc: %m");
          exit (EXIT_FAILURE);
        }
      }
      free (e.list.ptr);
      e.t = EXPR_STRING;
      e.string = s;
      *root_rtn = new_node (e);
      return 0;
    }

    *root_rtn = new_node (e);
    return 0;

  case EXPR_EXPR:
    id = get_node (root)->id;
    if (optimize_ast (id, &id) == -1)
      return -1;
    /* For a range of constant subexpressions we can simply replace
     * the nested expression with the constant, eg.
     * ( "String" ) => "String", ( null ) => null.
     */
    switch (get_node (id)->t) {
    case EXPR_NULL:
    case EXPR_BYTE:
    case EXPR_FILE:
    case EXPR_SCRIPT:
    case EXPR_STRING:
    case EXPR_NAME:
      *root_rtn = id;
      return 0;
    default: ;
    }
    /* ( ( expr ) ) can be replaced by ( expr ) */
    if (get_node (id)->t == EXPR_EXPR) {
      e.t = EXPR_EXPR;
      e.id = get_node (id)->id;
      *root_rtn = new_node (e);
      return 0;
    }
    get_node (root)->id = id;
    *root_rtn = root;
    return 0;

  case EXPR_ASSIGN:
    id = get_node (root)->a.id;
    if (optimize_ast (id, &id) == -1)
      return -1;
    get_node (root)->a.id = id;
    *root_rtn = root;
    return 0;

  case EXPR_REPEAT:
    /* Repeating zero times can be replaced by null. */
    if (get_node (root)->r.n == 0) {
      e.t = EXPR_NULL;
      *root_rtn = new_node (e);
      return 0;
    }
    id = get_node (root)->r.id;
    if (optimize_ast (id, &id) == -1)
      return -1;

    /* expr*1 can be replaced with simply expr.
     * null*N can be replaced with null.
     */
    if (get_node (root)->r.n == 1 || get_node (id)->t == EXPR_NULL) {
      *root_rtn = id;
      return 0;
    }
    /* expr*X*Y can be replaced by expr*(X*Y). */
    if (get_node (id)->t == EXPR_REPEAT) {
      e.t = EXPR_REPEAT;
      e.r.n = get_node (root)->r.n * get_node (id)->r.n;
      e.r.id = get_node (id)->r.id;
      *root_rtn = new_node (e);
      return 0;
    }
    /* fill(b,X)*Y can be replaced by fill(b,X*Y). */
    if (get_node (id)->t == EXPR_FILL) {
      e.t = EXPR_FILL;
      e.fl.n = get_node (root)->r.n * get_node (id)->fl.n;
      e.fl.b = get_node (id)->fl.b;
      *root_rtn = new_node (e);
      return 0;
    }
    /* For short strings and small values or N, string*N can be
     * replaced by N copies of the string.
     */
    if (get_node (id)->t == EXPR_STRING &&
        get_node (root)->r.n <= 4 &&
        get_node (id)->string.size <= 512) {
      string s = empty_vector;
      size_t n = get_node (root)->r.n;
      const string *sub = &get_node (id)->string;

      for (i = 0; i < n; ++i) {
        for (j = 0; j < sub->size; ++j) {
          if (string_append (&s, sub->ptr[j]) == -1) {
            nbdkit_error ("realloc: %m");
            return -1;
          }
        }
      }

      e.t = EXPR_STRING;
      e.string = s;
      *root_rtn = new_node (e);
      return 0;
    }
    /* Single byte expression * N can be replaced by a fill. */
    {
      uint8_t b;

      if (expr_is_single_byte (get_node (id), &b)) {
        e.t = EXPR_FILL;
        e.fl.n = get_node (root)->r.n;
        e.fl.b = b;
        *root_rtn = new_node (e);
        return 0;
      }
    }

    get_node (root)->r.id = id;
    *root_rtn = root;
    return 0;

  case EXPR_SLICE:
    /* A zero-length slice can be replaced by null. */
    if (get_node (root)->sl.n == get_node (root)->sl.m) {
      e.t = EXPR_NULL;
      *root_rtn = new_node (e);
      return 0;
    }
    id = get_node (root)->sl.id;
    if (optimize_ast (id, &id) == -1)
      return -1;
    get_node (root)->sl.id = id;
    *root_rtn = root;
    return 0;

  case EXPR_STRING:
    /* A zero length string can be replaced with null. */
    if (get_node (root)->string.size == 0) {
      e.t = EXPR_NULL;
      *root_rtn = new_node (e);
      return 0;
    }
    *root_rtn = root;
    return 0;

  case EXPR_FILL:
    /* Zero-length fill can be replaced by null. */
    if (get_node (root)->fl.n == 0) {
      e.t = EXPR_NULL;
      *root_rtn = new_node (e);
      return 0;
    }
    *root_rtn = root;
    return 0;

  case EXPR_NULL:
  case EXPR_BYTE:
  case EXPR_ABS_OFFSET:
  case EXPR_REL_OFFSET:
  case EXPR_ALIGN_OFFSET:
  case EXPR_FILE:
  case EXPR_SCRIPT:
  case EXPR_NAME:
    *root_rtn = root;
    return 0;
  }

  abort ();
}

static bool
expr_list_only_bytes (const expr_t *e)
{
  size_t i;

  assert (e->t == EXPR_LIST);
  for (i = 0; i < e->list.size; ++i) {
    if (get_node (e->list.ptr[i])->t != EXPR_BYTE)
      return false;
  }
  return true;
}

/* For some constant expressions which are a length 1 byte, return
 * true and the byte.
 */
static bool
expr_is_single_byte (const expr_t *e, uint8_t *b)
{
  switch (e->t) {
  case EXPR_BYTE:               /* A single byte. */
    if (b) *b = e->b;
    return true;
  case EXPR_LIST:               /* A single element list if it is single byte */
    if (e->list.size != 1)
      return false;
    return expr_is_single_byte (get_node (e->list.ptr[0]), b);
  case EXPR_STRING:             /* A length-1 string. */
    if (e->string.size != 1)
      return false;
    if (b) *b = e->string.ptr[0];
    return true;
  case EXPR_FILL:               /* A length-1 fill. */
    if (e->fl.n != 1)
      return false;
    if (b) *b = e->fl.b;
    return true;
  case EXPR_REPEAT:             /* EXPR*1 if EXPR is single byte */
    if (e->r.n != 1)
      return false;
    return expr_is_single_byte (get_node (e->r.id), b);
  default:
    return false;
  }
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

/* This is the evaluator.  It takes the root (node_id) of the parsed
 * abstract syntax treea and evaulates it into the allocator.
 */
static int
evaluate (const dict_t *dict, node_id root,
          struct allocator *a, uint64_t *offset, uint64_t *size)
{
  /* 'd' is the local dictionary for this function.  Assignments are
   * added to the dictionary in this scope and passed to nested
   * scopes.  This is leaked on error paths, but we're going to call
   * exit(1).
   */
  dict_t *d = (dict_t *) dict;
  size_t i, j;
  const expr_t *e;
  node_ids list;

  e = get_node (root);
  if (e->t == EXPR_LIST)
    memcpy (&list, &e->list, sizeof list);
  else {
    list.size = 1;
    list.ptr = &root;
  }

  for (i = 0; i < list.size; ++i) {
    e = get_node (list.ptr[i]);

    switch (e->t) {
    case EXPR_LIST: abort ();

    case EXPR_NULL: /* does nothing */ break;

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

    case EXPR_FILL:
      if (a->f->fill (a, e->fl.b, e->fl.n, *offset) == -1)
        return -1;
      *offset += e->fl.n;
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
      d->id = e->a.id;
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
      if (evaluate (t->next, t->id, a2, &offset2, &size2) == -1)
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

      /* <FILE[N:M] can be optimized by not reading in the whole file.
       * For files like /dev/urandom which are infinite this stops an
       * infinite loop.
       */
      if (e->t == EXPR_SLICE && get_node (e->sl.id)->t == EXPR_FILE) {
        if (store_file_slice (a, get_node (e->sl.id)->filename,
                              e->sl.n, e->sl.m, offset) == -1)
          return -1;
      }

      /* <(SCRIPT)[:LEN] must be optimized by truncating the
       * output of the script.
       */
      else if (e->t == EXPR_SLICE && e->sl.n == 0 &&
               get_node (e->sl.id)->t == EXPR_SCRIPT) {
        if (store_script_len (a, get_node (e->sl.id)->script, e->sl.m,
                              offset) == -1)
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
        if (evaluate (d, e->id, a2, &offset2, &size2) == -1)
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
