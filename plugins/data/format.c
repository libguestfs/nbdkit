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
#include <stdarg.h>
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

/* string + length, \0 allowed */
DEFINE_VECTOR_TYPE(string, char);

#define CLEANUP_FREE_STRING \
  __attribute__((cleanup (cleanup_free_string)))

static void
cleanup_free_string (string *v)
{
  free (v->ptr);
}

static string
substring (string s, size_t offset, size_t len)
{
  size_t i;
  string r = empty_vector;

  for (i = 0; i < len; ++i) {
    assert (offset+i < s.len);
    if (string_append (&r, s.ptr[offset+i]) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
  }

  return r;
}

typedef size_t node_id;         /* references a node in expr_table below */
DEFINE_VECTOR_TYPE(node_ids, node_id);

enum expr_type {
  EXPR_NULL = 0,              /* null expression, no effect */
  EXPR_LIST,                  /* list     - list of node IDs / nested expr */
  EXPR_BYTE,                  /* b        - single byte */
  EXPR_ABS_OFFSET,            /* ui       - absolute offset (@OFFSET) */
  EXPR_REL_OFFSET,            /* i        - relative offset (@+N or @-N) */
  EXPR_ALIGN_OFFSET,          /* ui       - align offset (@^ALIGNMENT) */
  EXPR_FILE,                  /* filename - read a file */
  EXPR_SCRIPT,                /* script   - run script */
  EXPR_STRING,                /* string   - string + length */
  EXPR_FILL,                  /* fl.n, fl.b - fill same byte b, N times */
  EXPR_NAME,                  /* name     - insert a named expression */
  EXPR_ASSIGN,                /* a.name, a.id - assign name to expr */
  EXPR_REPEAT,                /* r.id, r.n - expr * N */
  EXPR_SLICE,                 /* sl.id, sl.n, sl.m - expr[N:M] */
};

struct expr {
  enum expr_type t;
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
 * When referencing one expression from another (eg. for EXPR_REPEAT)
 * we refer to the index into this table (node_id) instead of pointing
 * to the expr_t directly.
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

/* Add the expression to the table, returning the node_id. */
static node_id
new_node (const expr_t e)
{
  if (expr_table.len == 0) {
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
  return expr_table.len-1;
}

/* Get an expression by node_id. */
static expr_t
get_node (node_id id)
{
  assert (id < expr_table.len);
  return expr_table.ptr[id];
}

static void
free_expr_table (void)
{
  size_t i;
  expr_t e;

  for (i = 0; i < expr_table.len; ++i) {
    e = get_node (i);
    switch (e.t) {
    case EXPR_LIST:   free (e.list.ptr); break;
    case EXPR_FILE:   free (e.filename); break;
    case EXPR_SCRIPT: free (e.script); break;
    case EXPR_STRING: free (e.string.ptr); break;
    case EXPR_NAME:   free (e.name); break;
    case EXPR_ASSIGN: free (e.a.name); break;
    default: break;
    }
  }

  expr_list_reset (&expr_table);
}

/* Construct an expression. */
static expr_t
expr (enum expr_type t, ...)
{
  expr_t e = { .t = t };
  va_list args;

  /* Note that you cannot pass uint8_t through varargs, so for the
   * byte fields we use int here.
   */
  va_start (args, t);
  switch (t) {
  case EXPR_NULL: /* nothing */ break;
  case EXPR_LIST: e.list = va_arg (args, node_ids); break;
  case EXPR_BYTE: e.b = va_arg (args, int); break;
  case EXPR_ABS_OFFSET:
  case EXPR_ALIGN_OFFSET:
    e.ui = va_arg (args, uint64_t);
    break;
  case EXPR_REL_OFFSET: e.i = va_arg (args, int64_t); break;
  case EXPR_FILE: e.filename = va_arg (args, char *); break;
  case EXPR_SCRIPT: e.script = va_arg (args, char *); break;
  case EXPR_STRING: e.string = va_arg (args, string); break;
  case EXPR_FILL:
    e.fl.b = va_arg (args, int);
    e.fl.n = va_arg (args, uint64_t);
    break;
  case EXPR_NAME: e.name = va_arg (args, char *); break;
  case EXPR_ASSIGN:
    e.a.name = va_arg (args, char *);
    e.a.id = va_arg (args, node_id);
    break;
  case EXPR_REPEAT:
    e.r.id = va_arg (args, node_id);
    e.r.n = va_arg (args, uint64_t);
    break;
  case EXPR_SLICE:
    e.sl.id = va_arg (args, node_id);
    e.sl.n = va_arg (args, uint64_t);
    e.sl.m = va_arg (args, int64_t);
    break;
  }
  va_end (args);

  return e;
}

/* Make a shallow copy of an expression. */
static expr_t
copy_expr (expr_t e)
{
  switch (e.t) {
    /* These have fields that have to be duplicated. */
  case EXPR_LIST:
    if (node_ids_duplicate (&e.list, &e.list) == -1) {
      nbdkit_error ("malloc");
      exit (EXIT_FAILURE);
    }
    break;
  case EXPR_FILE:
    e.filename = strdup (e.filename);
    if (e.filename == NULL) {
      nbdkit_error ("strdup");
      exit (EXIT_FAILURE);
    };
    break;
  case EXPR_SCRIPT:
    e.script = strdup (e.script);
    if (e.script == NULL) {
      nbdkit_error ("strdup");
      exit (EXIT_FAILURE);
    };
    break;
  case EXPR_STRING:
    if (string_duplicate (&e.string, &e.string) == -1) {
      nbdkit_error ("malloc");
      exit (EXIT_FAILURE);
    }
    break;
  case EXPR_NAME:
    e.name = strdup (e.name);
    if (e.name == NULL) {
      nbdkit_error ("strdup");
      exit (EXIT_FAILURE);
    };
    break;
  case EXPR_ASSIGN:
    e.a.name = strdup (e.a.name);
    if (e.a.name == NULL) {
      nbdkit_error ("strdup");
      exit (EXIT_FAILURE);
    };
    break;

    /* These don't require anything special. */
  case EXPR_NULL:
  case EXPR_BYTE:
  case EXPR_ABS_OFFSET:
  case EXPR_REL_OFFSET:
  case EXPR_ALIGN_OFFSET:
  case EXPR_FILL:
  case EXPR_REPEAT:
  case EXPR_SLICE:
    break;
  }

  return e;
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
  const expr_t e = get_node (id);
  size_t i;

  switch (e.t) {
  case EXPR_NULL:
    nbdkit_debug ("%snull", debug_indent (level));
    break;
  case EXPR_LIST:
    nbdkit_debug ("%s(", debug_indent (level));
    for (i = 0; i < e.list.len; ++i)
      debug_expr (e.list.ptr[i], level+1);
    nbdkit_debug ("%s)", debug_indent (level));
    break;
  case EXPR_BYTE:
    nbdkit_debug ("%s%" PRIu8, debug_indent (level), e.b);
    break;
  case EXPR_ABS_OFFSET:
    nbdkit_debug ("%s@%" PRIu64, debug_indent (level), e.ui);
    break;
  case EXPR_REL_OFFSET:
    nbdkit_debug ("%s@%" PRIi64, debug_indent (level), e.i);
    break;
  case EXPR_ALIGN_OFFSET:
    nbdkit_debug ("%s@^%" PRIi64, debug_indent (level), e.ui);
    break;
  case EXPR_FILE:
    nbdkit_debug ("%s<%s", debug_indent (level), e.filename);
    break;
  case EXPR_SCRIPT:
    nbdkit_debug ("%s<(%s)", debug_indent (level), e.script);
    break;
  case EXPR_STRING: {
    CLEANUP_FREE_STRING string s = empty_vector;
    static const char hex[] = "0123456789abcdef";

    for (i = 0; i < e.string.len; ++i) {
      char c = e.string.ptr[i];
      if (ascii_isprint ((char) c))
        string_append (&s, e.string.ptr[i]);
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
                  debug_indent (level), e.fl.b, e.fl.n);
    break;
  case EXPR_NAME:
    nbdkit_debug ("%s\\%s", debug_indent (level), e.name);
    break;
  case EXPR_ASSIGN:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e.a.id, level+1);
    nbdkit_debug ("%s) -> \\%s", debug_indent (level), e.a.name);
    break;
  case EXPR_REPEAT:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e.r.id, level+1);
    nbdkit_debug ("%s) *%" PRIu64, debug_indent (level), e.r.n);
    break;
  case EXPR_SLICE:
    nbdkit_debug ("%s(", debug_indent (level));
    debug_expr (e.sl.id, level+1);
    nbdkit_debug ("%s)[%" PRIu64 ":%" PRIi64 "]",
                  debug_indent (level), e.sl.n, e.sl.m);
    break;
  }
}

/* Is the expression something which contains data, or something else
 * (like an offset).  Just a light check to reject nonsense
 * expressions like "@0 * 10".
 */
static bool
is_data_expr (const expr_t e)
{
  return e.t != EXPR_ABS_OFFSET && e.t != EXPR_REL_OFFSET
    && e.t != EXPR_ALIGN_OFFSET;
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

  assert (expr_table.len == 0);

  /* Run the parser across the entire string, returning the top level
   * expression.
   */
  if (parser (0, value, &i, strlen (value), &root) == -1)
    goto out;

  if (optimize_ast (root, &root) == -1)
    goto out;

  if (data_debug_AST) {
    nbdkit_debug ("BEGIN AST (-D data.AST=1)");
    debug_expr (root, 0);
    nbdkit_debug ("END AST");
  }

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
#define APPEND_EXPR(node_id)                                  \
    do {                                                      \
      if (node_ids_append (&list, (node_id)) == -1) {         \
        nbdkit_error ("realloc: %m");                         \
        exit (EXIT_FAILURE);                                  \
      }                                                       \
    } while (0)
    node_id id;
    int j, n;
    int64_t i64, m;
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
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter after @+ must not be negative");
            return -1;
          }
          i += n;
          APPEND_EXPR (new_node (expr (EXPR_REL_OFFSET, i64)));
        }
        else
          goto parse_error;
        break;
      case '-':                 /* @-N */
        if (++i == len) goto parse_error;
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter after @- must not be negative");
            return -1;
          }
          i += n;
          APPEND_EXPR (new_node (expr (EXPR_REL_OFFSET, -i64)));
        }
        else
          goto parse_error;
        break;
      case '^':                 /* @^ALIGNMENT */
        if (++i == len) goto parse_error;
        /* We must use %i into i64 in order to parse 0x etc. */
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter after @^ must not be negative");
            return -1;
          }
          /* XXX fix this arbitrary restriction */
          if (!is_power_of_2 (i64)) {
            nbdkit_error ("data parameter @^%" PRIi64 " must be a power of 2",
                          i64);
            return -1;
          }
          i += n;
          APPEND_EXPR (new_node (expr (EXPR_ALIGN_OFFSET, (uint64_t) i64)));
        }
        else
          goto parse_error;
        break;
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        /* We must use %i into i64 in order to parse 0x etc. */
        if (sscanf (&value[i], "%" SCNi64 "%n", &i64, &n) == 1) {
          if (i64 < 0) {
            nbdkit_error ("data parameter @OFFSET must not be negative");
            return -1;
          }
          i += n;
          APPEND_EXPR (new_node (expr (EXPR_ABS_OFFSET, (uint64_t) i64)));
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
      /* parser() always returns a list. */
      assert (get_node (id).t == EXPR_LIST);
      APPEND_EXPR (id);
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
      if (list.len == 0) {
        nbdkit_error ("*N must follow an expression");
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.len-1]))) {
        nbdkit_error ("*N cannot be applied to this type of expression");
        return -1;
      }
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
      id = list.ptr[list.len-1];
      list.len--;
      APPEND_EXPR (new_node (expr (EXPR_REPEAT, id, (uint64_t) i64)));
      break;

    case '[':                 /* expr[k:m] */
      i++;
      if (list.len == 0) {
        nbdkit_error ("[N:M] must follow an expression");
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.len-1]))) {
        nbdkit_error ("[N:M] cannot be applied to this type of expression");
        return -1;
      }
      i64 = 0;
      m = -1;
      if (sscanf (&value[i], "%" SCNi64 ":%" SCNi64 "]%n",
                  &i64, &m, &n) == 2 ||
          sscanf (&value[i], ":%" SCNi64 "]%n", &m, &n) == 1 ||
          sscanf (&value[i], "%" SCNi64 ":]%n", &i64, &n) == 1)
        i += n;
      else if (strncmp (&value[i], ":]", 2) == 0)
        i += 2;
      else {
        nbdkit_error ("enclosed pattern (...)[N:M] not numeric");
        return -1;
      }
      id = list.ptr[list.len-1];
      list.len--;
      APPEND_EXPR (new_node (expr (EXPR_SLICE, id, i64, m)));
      break;

    case '<':
      if (i+1 < len && value[i+1] == '(') { /* <(SCRIPT) */
        char *script;

        i += 2;

        flen = get_script (value, i, len);
        if (flen == 0) goto parse_error;
        script = strndup (&value[i], flen);
        if (script == NULL) {
          nbdkit_error ("strndup: %m");
          return -1;
        }
        i += flen + 1;          /* +1 for trailing ) */
        APPEND_EXPR (new_node (expr (EXPR_SCRIPT, script)));
      }
      else {                    /* <FILE */
        char *filename;

        i++;

        /* The filename follows next in the string. */
        flen = strcspn (&value[i], "*[) \t\n");
        if (flen == 0) {
          nbdkit_error ("data parameter <FILE not a filename");
          return -1;
        }
        filename = strndup (&value[i], flen);
        if (filename == NULL) {
          nbdkit_error ("strndup: %m");
          return -1;
        }
        i += flen;
        APPEND_EXPR (new_node (expr (EXPR_FILE, filename)));
      }
      break;

    case '"': {                 /* "String" */
      string str = empty_vector;

      i++;
      if (parse_string (value, &i, len, &str) == -1)
        return -1;
      APPEND_EXPR (new_node (expr (EXPR_STRING, str)));
      break;
    }

    case '\\': {                /* \\NAME */
      char *name;

      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      name = strndup (&value[i], flen);
      if (name == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
      }
      i += flen;
      APPEND_EXPR (new_node (expr (EXPR_NAME, name)));
      break;
    }

    case '-': {                 /* -> \\NAME */
      char *name;

      i++;
      if (value[i] != '>') goto parse_error;
      i++;
      if (list.len == 0) {
        nbdkit_error ("-> must follow an expression");
        return -1;
      }
      if (! is_data_expr (get_node (list.ptr[list.len-1]))) {
        nbdkit_error ("-> cannot be applied to this type of expression");
        return -1;
      }
      flen = get_name (value, i, len, &i);
      if (flen == 0) goto parse_error;
      name = strndup (&value[i], flen);
      if (name == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
      }
      id = list.ptr[list.len-1];
      i += flen;
      list.len--;
      APPEND_EXPR (new_node (expr (EXPR_ASSIGN, name, id)));
      break;
    }

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
      /* parser() always returns a list. */
      assert (get_node (id).t == EXPR_LIST);
      APPEND_EXPR (id);
      break;
    }

    case '0': case '1': case '2': case '3': case '4': /* BYTE */
    case '5': case '6': case '7': case '8': case '9':
      /* We need to use %i here so it scans 0x etc correctly. */
      if (sscanf (&value[i], "%i%n", &j, &n) == 1)
        i += n;
      else
        goto parse_error;
      if (j < 0 || j > 255) {
        nbdkit_error ("data parameter BYTE must be in the range 0..255");
        return -1;
      }
      APPEND_EXPR (new_node (expr (EXPR_BYTE, j)));
      break;

    case 'l': case 'b': {       /* le or be + NN: + WORD */
      string str = empty_vector;

      if (parse_word (value, &i, len, &str) == -1)
        return -1;
      APPEND_EXPR (new_node (expr (EXPR_STRING, str)));
      break;
    }

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
  *rtn = new_node (expr (EXPR_LIST, list));
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
  copy.len = n + 1;
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
    endian = little; rtn->len = 2;
  }
  else if (strncmp (copy.ptr, "le32:", 5) == 0) {
    endian = little; rtn->len = 4;
  }
  else if (strncmp (copy.ptr, "le64:", 5) == 0) {
    endian = little; rtn->len = 8;
  }
  else if (strncmp (copy.ptr, "be16:", 5) == 0) {
    endian = big; rtn->len = 2;
  }
  else if (strncmp (copy.ptr, "be32:", 5) == 0) {
    endian = big; rtn->len = 4;
  }
  else if (strncmp (copy.ptr, "be64:", 5) == 0) {
    endian = big; rtn->len = 8;
  }
  else {
    nbdkit_error ("data parameter: expected \"le16/32/64:\" "
                  "or \"be16/32/64:\" at offset %zu", i);
    return -1;
  }

  /* Parse the word field into a host-order unsigned int. */
  switch (rtn->len) {
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
    switch (rtn->len) {
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
    switch (rtn->len) {
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
static bool expr_safe_to_inline (const expr_t);
static bool list_safe_to_inline (const node_ids);
static bool expr_is_single_byte (const expr_t, uint8_t *b);
static bool exprs_can_combine (expr_t e0, expr_t e1, node_id *id_rtn);

static int
optimize_ast (node_id root, node_id *root_rtn)
{
  size_t i, j;
  node_id id;
  node_ids list = empty_vector;
  expr_t e2;

  switch (get_node (root).t) {
  case EXPR_LIST:
    /* For convenience this makes a new list node. */

    /* Optimize each element of the list. */
    for (i = 0; i < get_node (root).list.len; ++i) {
      id = get_node (root).list.ptr[i];
      if (optimize_ast (id, &id) == -1)
        return -1;
      switch (get_node (id).t) {
      case EXPR_NULL:
        /* null elements of a list can be ignored. */
        break;
      case EXPR_LIST:
        /* Simple lists can be inlined, but be careful with
         * assignments, offsets and other expressions which are scoped
         * because flattening the list changes the scope.
         */
        if (list_safe_to_inline (get_node (id).list)) {
          for (j = 0; j < get_node (id).list.len; ++j) {
            if (node_ids_append (&list, get_node (id).list.ptr[j]) == -1)
              goto list_append_error;
          }
          break;
        }
        /*FALLTHROUGH*/
      default:
        if (node_ids_append (&list, id) == -1) {
        list_append_error:
          nbdkit_error ("realloc: %m");
          return -1;
        }
      }
    }

    /* Combine adjacent pairs of elements if possible. */
    for (i = 1; i < list.len; ++i) {
      node_id id0, id1;

      id0 = list.ptr[i-1];
      id1 = list.ptr[i];
      if (exprs_can_combine (get_node (id0), get_node (id1), &id)) {
        list.ptr[i-1] = id;
        node_ids_remove (&list, i);
        --i;
      }
    }

    /* List of length 0 is replaced with null. */
    if (list.len == 0) {
      free (list.ptr);
      *root_rtn = new_node (expr (EXPR_NULL));
      return 0;
    }

    /* List of length 1 is replaced with the first element, but as
     * above avoid inlining if it is not a safe expression.
     */
    if (list.len == 1 && expr_safe_to_inline (get_node (list.ptr[0]))) {
      id = list.ptr[0];
      free (list.ptr);
      *root_rtn = id;
      return 0;
    }

    *root_rtn = new_node (expr (EXPR_LIST, list));
    return 0;

  case EXPR_ASSIGN:
    id = get_node (root).a.id;
    if (optimize_ast (id, &id) == -1)
      return -1;
    e2 = copy_expr (get_node (root));
    e2.a.id = id;
    *root_rtn = new_node (e2);
    return 0;

  case EXPR_REPEAT:
    /* Repeating zero times can be replaced by null. */
    if (get_node (root).r.n == 0) {
      *root_rtn = new_node (expr (EXPR_NULL));
      return 0;
    }
    id = get_node (root).r.id;
    if (optimize_ast (id, &id) == -1)
      return -1;

    /* expr*1 can be replaced with simply expr.
     * null*N can be replaced with null.
     */
    if (get_node (root).r.n == 1 || get_node (id).t == EXPR_NULL) {
      *root_rtn = id;
      return 0;
    }
    /* expr*X*Y can be replaced by expr*(X*Y). */
    if (get_node (id).t == EXPR_REPEAT) {
      *root_rtn = new_node (expr (EXPR_REPEAT,
                                  get_node (id).r.id,
                                  get_node (root).r.n * get_node (id).r.n));
      return 0;
    }
    /* fill(b,X)*Y can be replaced by fill(b,X*Y). */
    if (get_node (id).t == EXPR_FILL) {
      *root_rtn = new_node (expr (EXPR_FILL,
                                  get_node (id).fl.b,
                                  get_node (root).r.n * get_node (id).fl.n));
      return 0;
    }
    /* For short strings and small values or N, string*N can be
     * replaced by N copies of the string.
     */
    if (get_node (id).t == EXPR_STRING &&
        get_node (root).r.n <= 4 &&
        get_node (id).string.len <= 512) {
      string s = empty_vector;
      size_t n = get_node (root).r.n;
      const string sub = get_node (id).string;

      for (i = 0; i < n; ++i) {
        for (j = 0; j < sub.len; ++j) {
          if (string_append (&s, sub.ptr[j]) == -1) {
            nbdkit_error ("realloc: %m");
            return -1;
          }
        }
      }

      *root_rtn = new_node (expr (EXPR_STRING, s));
      return 0;
    }
    /* Single byte expression * N can be replaced by a fill. */
    {
      uint8_t b;

      if (expr_is_single_byte (get_node (id), &b)) {
        *root_rtn = new_node (expr (EXPR_FILL, b, get_node (root).r.n));
        return 0;
      }
    }

    e2 = copy_expr (get_node (root));
    e2.r.id = id;
    *root_rtn = new_node (e2);
    return 0;

  case EXPR_SLICE: {
    uint64_t n = get_node (root).sl.n;
    int64_t m = get_node (root).sl.m;
    uint64_t len;

    /* A zero-length slice can be replaced by null. */
    if (n == m) {
      *root_rtn = new_node (expr (EXPR_NULL));
      return 0;
    }
    id = get_node (root).sl.id;
    if (optimize_ast (id, &id) == -1)
      return -1;

    /* Some constant expressions can be sliced into something shorter.
     * Be conservative.  If the slice is invalid then we prefer to do
     * nothing here because the whole expression might be optimized
     * away and if it isn't then we will give an error later.
     */
    switch (get_node (id).t) {
    case EXPR_NULL:             /* null[:0] or null[0:] => null */
      if (m == 0 || (n == 0 && m == -1)) {
        *root_rtn = new_node (expr (EXPR_NULL));
        return 0;
      }
      break;
    case EXPR_BYTE:             /* byte[:1] or byte[0:] => byte */
      if (m == 1 || (n == 0 && m == -1)) {
        *root_rtn = new_node (expr (EXPR_BYTE, get_node (id).b));
        return 0;
      }
      break;
    case EXPR_STRING:           /* substring */
      len = get_node (id).string.len;
      if (m >= 0 && n <= m && m <= len) {
        if (m-n == 1)
          *root_rtn = new_node (expr (EXPR_BYTE, get_node (id).string.ptr[n]));
        else {
          string sub = substring (get_node (id).string, n, m-n);
          *root_rtn = new_node (expr (EXPR_STRING, sub));
        }
        return 0;
      }
      if (m == -1 && n <= len) {
        if (len-n == 1)
          *root_rtn = new_node (expr (EXPR_BYTE, get_node (id).string.ptr[n]));
        else {
          string sub = substring (get_node (id).string, n, len-n);
          *root_rtn = new_node (expr (EXPR_STRING, sub));
        }
        return 0;
      }
      break;
    case EXPR_FILL:             /* slice of a fill is a shorter fill */
      len = get_node (id).fl.n;
      if (m >= 0 && n <= m && m <= len) {
        if (m-n == 1)
          *root_rtn = new_node (expr (EXPR_BYTE, get_node (id).fl.b));
        else
          *root_rtn = new_node (expr (EXPR_FILL, get_node (id).fl.b, m-n));
        return 0;
      }
      if (m == -1 && n <= len) {
        if (len-n == 1)
          *root_rtn = new_node (expr (EXPR_BYTE, get_node (id).fl.b));
        else
          *root_rtn = new_node (expr (EXPR_FILL, get_node (id).fl.b, len-n));
        return 0;
      }
      break;
    default: ;
    }

    e2 = copy_expr (get_node (root));
    e2.sl.id = id;
    *root_rtn = new_node (e2);
    return 0;
  }

  case EXPR_STRING:
    /* A zero length string can be replaced with null. */
    if (get_node (root).string.len == 0) {
      *root_rtn = new_node (expr (EXPR_NULL));
      return 0;
    }
    /* Strings containing the same character can be replaced by a
     * fill.  These can be produced by other optimizations.
     */
    if (get_node (root).string.len > 1) {
      const string s = get_node (root).string;
      uint8_t b = s.ptr[0];

      for (i = 1; i < s.len; ++i)
        if (s.ptr[i] != b)
          break;

      if (i == s.len) {
        *root_rtn = new_node (expr (EXPR_FILL, b, (uint64_t) s.len));
        return 0;
      }
    }
    *root_rtn = root;
    return 0;

  case EXPR_FILL:
    /* Zero-length fill can be replaced by null. */
    if (get_node (root).fl.n == 0) {
      *root_rtn = new_node (expr (EXPR_NULL));
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

/* Test if an expression can be safely inlined in a superior list
 * without changing the meaning of any scoped expressions.
 */
static bool
expr_safe_to_inline (const expr_t e)
{
  switch (e.t) {
    /* Assignments and named expressions are scoped. */
  case EXPR_ASSIGN:
  case EXPR_NAME:
    return false;

    /* @Offsets are scoped. */
  case EXPR_ABS_OFFSET:
  case EXPR_REL_OFFSET:
  case EXPR_ALIGN_OFFSET:
    return false;

    /* Everything else should be safe. */
  case EXPR_NULL:
  case EXPR_LIST:
  case EXPR_BYTE:
  case EXPR_FILE:
  case EXPR_SCRIPT:
  case EXPR_STRING:
  case EXPR_FILL:
  case EXPR_REPEAT:
  case EXPR_SLICE:
    ;
  }

  return true;
}

/* Test if a list of expressions is safe to inline in a superior list. */
static bool
list_safe_to_inline (const node_ids list)
{
  size_t i;

  for (i = 0; i < list.len; ++i) {
    if (!expr_safe_to_inline (get_node (list.ptr[i])))
      return false;
  }

  return true;
}

/* For some constant expressions which are a length 1 byte, return
 * true and the byte.
 */
static bool
expr_is_single_byte (const expr_t e, uint8_t *b)
{
  switch (e.t) {
  case EXPR_BYTE:               /* A single byte. */
    if (b) *b = e.b;
    return true;
  case EXPR_LIST:               /* A single element list if it is single byte */
    if (e.list.len != 1)
      return false;
    return expr_is_single_byte (get_node (e.list.ptr[0]), b);
  case EXPR_STRING:             /* A length-1 string. */
    if (e.string.len != 1)
      return false;
    if (b) *b = e.string.ptr[0];
    return true;
  case EXPR_FILL:               /* A length-1 fill. */
    if (e.fl.n != 1)
      return false;
    if (b) *b = e.fl.b;
    return true;
  case EXPR_REPEAT:             /* EXPR*1 if EXPR is single byte */
    if (e.r.n != 1)
      return false;
    return expr_is_single_byte (get_node (e.r.id), b);
  default:
    return false;
  }
}

/* Test if two adjacent constant expressions can be combined and if so
 * return a new expression which is the combination of both.  For
 * example, two bytes are combined into a string (1 2 => "\x01\x02"),
 * or a string and a byte into a longer string ("\x01\x02" 3 =>
 * "\x01\x02\x03").
 */
static bool
exprs_can_combine (expr_t e0, expr_t e1, node_id *id_rtn)
{
  string s = empty_vector;
  size_t len, len1;
  expr_t e2;

  switch (e0.t) {
  case EXPR_BYTE:
    switch (e1.t) {
    case EXPR_BYTE:             /* byte byte => fill | string */
      if (e0.b == e1.b) {
        *id_rtn = new_node (expr (EXPR_FILL, e0.b, UINT64_C(2)));
      }
      else {
        if (string_append (&s, e0.b) == -1 ||
            string_append (&s, e1.b) == -1)
          goto out_of_memory;
        *id_rtn = new_node (expr (EXPR_STRING, s));
      }
      return true;
    case EXPR_STRING:           /* byte string => string */
      len = e1.string.len;
      if (string_reserve (&s, len+1) == -1)
        goto out_of_memory;
      s.len = len+1;
      s.ptr[0] = e0.b;
      memcpy (&s.ptr[1], e1.string.ptr, len);
      *id_rtn = new_node (expr (EXPR_STRING, s));
      return true;
    case EXPR_FILL:             /* byte fill => fill, if the same */
      if (e0.b != e1.fl.b)
        return false;
      e2 = copy_expr (e1);
      e2.fl.n++;
      *id_rtn = new_node (e2);
      return true;
    default:
      return false;
    }

  case EXPR_STRING:
    switch (e1.t) {
    case EXPR_BYTE:             /* string byte => string */
      len = e0.string.len;
      if (string_reserve (&s, len+1) == -1)
        goto out_of_memory;
      s.len = len+1;
      memcpy (s.ptr, e0.string.ptr, len);
      s.ptr[len] = e1.b;
      *id_rtn = new_node (expr (EXPR_STRING, s));
      return true;
    case EXPR_STRING:           /* string string => string */
      len = e0.string.len;
      len1 = e1.string.len;
      if (string_reserve (&s, len+len1) == -1)
        goto out_of_memory;
      s.len = len+len1;
      memcpy (s.ptr, e0.string.ptr, len);
      memcpy (&s.ptr[len], e1.string.ptr, len1);
      *id_rtn = new_node (expr (EXPR_STRING, s));
      return true;
    default:
      return false;
    }

  case EXPR_FILL:
    switch (e1.t) {
    case EXPR_BYTE:             /* fill byte => fill, if the same */
      if (e0.fl.b != e1.b)
        return false;
      e2 = copy_expr (e0);
      e2.fl.n++;
      *id_rtn = new_node (e2);
      return true;
    case EXPR_FILL:             /* fill fill => fill, if the same */
      if (e0.fl.b != e1.fl.b)
        return false;
      e2 = copy_expr (e0);
      e2.fl.n += e1.fl.n;
      *id_rtn = new_node (e2);
      return true;
    default:
      return false;
    }

  default:
    return false;
  }

 out_of_memory:
  nbdkit_error ("realloc: %m");
  exit (EXIT_FAILURE);
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
  node_ids list;

  /* Extract the list from the current node.  If the current node is
   * not EXPR_LIST then make one for convenience below.
   */
  if (get_node (root).t == EXPR_LIST) {
    list = get_node (root).list;
  }
  else {
    list.len = 1;
    list.ptr = &root;
  }

  for (i = 0; i < list.len; ++i) {
    const expr_t e = get_node (list.ptr[i]);

    switch (e.t) {
    case EXPR_NULL: /* does nothing */ break;

    case EXPR_BYTE:
      /* Store the byte. */
      if (a->f->write (a, &e.b, 1, *offset) == -1)
        return -1;
      (*offset)++;
      break;

    case EXPR_ABS_OFFSET:
      /* XXX Check it does not overflow 63 bits. */
      *offset = e.ui;
      break;

    case EXPR_REL_OFFSET:
      if (e.i < 0 && -e.i > *offset) {
        nbdkit_error ("data parameter @-%" PRIi64 " "
                      "must not be larger than the current offset %" PRIu64,
                      -e.i, *offset);
        return -1;
      }
      /* XXX Check it does not overflow 63 bits. */
      *offset += e.i;
      break;

    case EXPR_ALIGN_OFFSET:
      *offset = ROUND_UP (*offset, e.ui);
      break;

    case EXPR_FILE:
      if (store_file (a, e.filename, offset) == -1)
        return -1;
      break;

    case EXPR_SCRIPT:
      if (store_script (a, e.script, offset) == -1)
        return -1;
      break;

    case EXPR_STRING:
      /* Copy the string into the allocator. */
      if (a->f->write (a, e.string.ptr, e.string.len, *offset) == -1)
        return -1;
      *offset += e.string.len;
      break;

    case EXPR_FILL:
      if (a->f->fill (a, e.fl.b, e.fl.n, *offset) == -1)
        return -1;
      *offset += e.fl.n;
      break;

    case EXPR_ASSIGN: {
      dict_t *d_next = d;

      d = malloc (sizeof *d);
      if (d == NULL) {
        nbdkit_error ("malloc: %m");
        return -1;
      }
      d->next = d_next;
      d->name = e.a.name;
      d->id = e.a.id;
      break;
    }

    case EXPR_NAME: {
      CLEANUP_FREE_ALLOCATOR struct allocator *a2 = NULL;
      uint64_t offset2 = 0, size2 = 0;
      dict_t *t;

      /* Look up the expression in the current dictionary. */
      for (t = d; t != NULL; t = t->next)
        if (strcmp (t->name, e.name) == 0)
          break;
      if (t == NULL) {
        nbdkit_error ("\\%s not defined", e.name);
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

    case EXPR_LIST:
    case EXPR_REPEAT:
    case EXPR_SLICE:
      /* Optimize some cases so we don't always have to create a
       * new allocator.
       */

      /* <FILE[N:M] can be optimized by not reading in the whole file.
       * For files like /dev/urandom which are infinite this stops an
       * infinite loop.
       */
      if (e.t == EXPR_SLICE && get_node (e.sl.id).t == EXPR_FILE) {
        if (store_file_slice (a, get_node (e.sl.id).filename,
                              e.sl.n, e.sl.m, offset) == -1)
          return -1;
      }

      /* <(SCRIPT)[:LEN] must be optimized by truncating the
       * output of the script.
       */
      else if (e.t == EXPR_SLICE && e.sl.n == 0 &&
               get_node (e.sl.id).t == EXPR_SCRIPT) {
        if (store_script_len (a, get_node (e.sl.id).script, e.sl.m,
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
        node_id id;

        switch (e.t) {
        case EXPR_LIST:   id = list.ptr[i]; break;
        case EXPR_REPEAT: id = e.r.id; break;
        case EXPR_SLICE:  id = e.sl.id; break;
        default: abort ();
        }

        a2 = create_allocator ("sparse", false);
        if (a2 == NULL) {
          nbdkit_error ("malloc: %m");
          return -1;
        }
        if (evaluate (d, id, a2, &offset2, &size2) == -1)
          return -1;

        switch (e.t) {
        case EXPR_LIST:
          if (a->f->blit (a2, a, size2, 0, *offset) == -1)
            return -1;
          *offset += size2;
          break;
        case EXPR_REPEAT:
          /* Duplicate the allocator a2 N times. */
          for (j = 0; j < e.r.n; ++j) {
            if (a->f->blit (a2, a, size2, 0, *offset) == -1)
              return -1;
            *offset += size2;
          }
          break;
        case EXPR_SLICE:
          /* Slice [N:M] */
          m = e.sl.m < 0 ? size2 : e.sl.m;
          if (e.sl.n < 0 || m < 0 ||
              e.sl.n > size2 || m > size2 ||
              e.sl.n > m ) {
            nbdkit_error ("[N:M] does not describe a valid slice");
            return -1;
          }
          /* Take a slice from the allocator. */
          if (a->f->blit (a2, a, m-e.sl.n, e.sl.n, *offset) == -1)
            return -1;
          *offset += m-e.sl.n;
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
