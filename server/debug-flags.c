/* nbdkit
 * Copyright Red Hat
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
#include <string.h>

#include <dlfcn.h>

#include "internal.h"
#include "strndup.h"

struct debug_flag {
  struct debug_flag *next;
  char *name;                   /* plugin or filter name */
  char *flag;                   /* flag name */
  char *symbol;                 /* symbol, eg. "myplugin_debug_foo" */
  int value;                    /* value of flag */
  bool used;                    /* if flag was successfully set */
};

/* Synthesize the name of the *_debug_* variable from the plugin name
 * and flag.
 */
static char *
symbol_of_debug_flag (const char *name, const char *flag)
{
  char *var;
  size_t i;
  int len;

  len = asprintf (&var, "%s_debug_%s", name, flag);
  if (len == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  /* If there are any '.'s remaining in the name, convert them to '_'. */
  for (i = 0; i < (size_t) len; ++i) {
    if (var[i] == '.')
      var[i] = '_';
  }

  return var;                   /* caller frees */
}

/* Parse and add a single -D flag from the command line.
 *
 * Debug Flag must be "NAME.FLAG=N".
 *                     ^    ^    ^
 *                   arg    p    q  (after +1 adjustment below)
 */
void
add_debug_flag (const char *arg)
{
  struct debug_flag *flag;
  char *p, *q;

  p = strchr (arg, '.');
  q = strchr (arg, '=');
  if (p == NULL || q == NULL) {
  bad_debug_flag:
    fprintf (stderr,
             "%s: -D (Debug Flag) must have the format NAME.FLAG=N\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  p++;                          /* +1 adjustment */
  q++;

  if (p - arg <= 1) goto bad_debug_flag; /* NAME too short */
  if (p > q) goto bad_debug_flag;
  if (q - p <= 1) goto bad_debug_flag;   /* FLAG too short */
  if (*q == '\0') goto bad_debug_flag;   /* N too short */

  flag = malloc (sizeof *flag);
  if (flag == NULL) {
  debug_flag_perror:
    perror ("malloc");
    exit (EXIT_FAILURE);
  }

  flag->name = strndup (arg, p-arg-1);
  if (!flag->name) goto debug_flag_perror;
  flag->flag = strndup (p, q-p-1);
  if (!flag->flag) goto debug_flag_perror;
  if (nbdkit_parse_int ("flag", q, &flag->value) == -1)
    goto bad_debug_flag;
  flag->used = false;
  flag->symbol = symbol_of_debug_flag (flag->name, flag->flag);

  /* Add flag to the linked list. */
  flag->next = debug_flags;
  debug_flags = flag;
}

/* Apply all debug flags applicable to this backend. */
void
apply_debug_flags (void *dl, const char *name)
{
  struct debug_flag *flag;

  for (flag = debug_flags; flag != NULL; flag = flag->next) {
    if (!flag->used && strcmp (name, flag->name) == 0) {
      int *sym;

      /* Find the symbol. */
      sym = dlsym (dl, flag->symbol);
      if (sym) {
        /* Set the flag. */
        *sym = flag->value;
      }
      else {
        fprintf (stderr,
                 "%s: warning: -D %s.%s: %s does not contain a "
                 "global variable called %s\n",
                 program_name, name, flag->flag, name, flag->symbol);
      }

      /* Mark this flag as used. */
      flag->used = true;
    }
  }
}

void
free_debug_flags (void)
{
  while (debug_flags != NULL) {
    struct debug_flag *next = debug_flags->next;

    if (!debug_flags->used)
      fprintf (stderr, "%s: warning: debug flag -D %s.%s was not used\n",
               program_name, debug_flags->name, debug_flags->flag);
    free (debug_flags->name);
    free (debug_flags->flag);
    free (debug_flags->symbol);
    free (debug_flags);
    debug_flags = next;
  }
}
