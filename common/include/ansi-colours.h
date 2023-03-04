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

#ifndef NBDKIT_ANSI_COLOURS_H
#define NBDKIT_ANSI_COLOURS_H

#include <stdbool.h>

/* For the ansi_* functions, the main program should declare this
 * variable, and initialize it in main() / option parsing.  See
 * libnbd.git/dump/dump.c for an example of how to initialize it.
 */
extern bool colour;

/* Restore the terminal colours to the default.
 *
 * As well as doing this before normal exit, you should also set a
 * signal handler which calls this and fflush(fp).  See
 * libnbd.git/dump/dump.c for an example.
 */
static inline void
ansi_restore (FILE *fp)
{
  if (colour)
    fputs ("\033[0m", fp);
}

/* Set the terminal colour. */
static inline void
ansi_colour (const char *c, FILE *fp)
{
  if (colour)
    fprintf (fp, "\033[%sm", c);
}

#define ANSI_FG_BOLD_BLACK     "1;30"
#define ANSI_FG_BLUE           "22;34"
#define ANSI_FG_BRIGHT_BLUE    "1;34"
#define ANSI_FG_BRIGHT_CYAN    "1;36"
#define ANSI_FG_BRIGHT_GREEN   "1;32"
#define ANSI_FG_BRIGHT_MAGENTA "1;35"
#define ANSI_FG_BRIGHT_RED     "1;31"
#define ANSI_FG_BRIGHT_WHITE   "1;37"
#define ANSI_FG_BRIGHT_YELLOW  "1;33"
#define ANSI_FG_CYAN           "22;36"
#define ANSI_FG_GREEN          "22;32"
#define ANSI_FG_GREY           "22;90"
#define ANSI_FG_MAGENTA        "22;35"
#define ANSI_FG_RED            "22;31"
#define ANSI_FG_YELLOW         "22;33"

#define ANSI_BG_BLACK          "40"
#define ANSI_BG_LIGHT_GREY     "47"
#define ANSI_BG_GREY           "100"

/* Unconditional versions of above (don't depend on global ‘colour’). */
static inline void
ansi_force_restore (FILE *fp)
{
  fputs ("\033[0m", fp);
}

static inline void
ansi_force_colour (const char *c, FILE *fp)
{
  fprintf (fp, "\033[%sm", c);
}

#endif /* NBDKIT_ANSI_COLOURS_H */
