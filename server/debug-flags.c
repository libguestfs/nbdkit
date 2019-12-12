/* nbdkit
 * Copyright (C) 2013-2019 Red Hat Inc.
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

/* Apply all debug flags applicable to this backend. */
void
apply_debug_flags (void *dl, const char *name)
{
  struct debug_flag *flag;

  for (flag = debug_flags; flag != NULL; flag = flag->next) {
    if (!flag->used && strcmp (name, flag->name) == 0) {
      CLEANUP_FREE char *var = NULL;
      int *sym;

      /* Synthesize the name of the variable. */
      if (asprintf (&var, "%s_debug_%s", name, flag->flag) == -1) {
        perror ("asprintf");
        exit (EXIT_FAILURE);
      }

      /* Find the symbol. */
      sym = dlsym (dl, var);
      if (sym) {
        /* Set the flag. */
        *sym = flag->value;
      }
      else {
        fprintf (stderr,
                 "%s: warning: -D %s.%s: %s does not contain a "
                 "global variable called %s\n",
                 program_name, name, flag->flag, name, var);
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
    free (debug_flags);
    debug_flags = next;
  }
}
