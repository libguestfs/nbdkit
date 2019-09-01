/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <XSUB.h>
#include <EXTERN.h>
#include <perl.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"

/* Use of perl.h insists on shadowing my_perl during XS(). */
#pragma GCC diagnostic ignored "-Wshadow"

static PerlInterpreter *my_perl;
static const char *script;

static void
perl_load (void)
{
  int argc = 1;
  const char *argv[2] = { "nbdkit", NULL };

  /* Full Perl interpreter initialization is deferred until we read
   * the first config parameter (which MUST be "script").
   */
  PERL_SYS_INIT3 (&argc, (char ***) &argv, &environ);
  my_perl = perl_alloc ();
  if (!my_perl) {
    nbdkit_error ("out of memory allocating Perl interpreter");
    exit (EXIT_FAILURE);
  }
  perl_construct (my_perl);
}

static void
perl_unload (void)
{
  if (my_perl != NULL) {
    perl_destruct (my_perl);
    perl_free (my_perl);
    PERL_SYS_TERM ();
  }
}

/* We use this function to test if the named callback is defined
 * in the loaded Perl code.
 */
static int
callback_defined (const char *perl_func_name)
{
  SV *ret;
  CLEANUP_FREE char *cmd = NULL;

  if (asprintf (&cmd, "defined &%s", perl_func_name) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  ret = eval_pv (cmd, FALSE);

  return SvTRUE (ret);
}

/* Check for a Perl exception, and convert it to an nbdkit error. */
static int
check_perl_failure (void)
{
  SV *errsv = get_sv ("@", TRUE);

  if (SvTRUE (errsv)) {
    const char *err;
    STRLEN n;
    CLEANUP_FREE char *err_copy = NULL;

    err = SvPV (errsv, n);

    /* Need to chop off the final \n if there is one.  The only way to
     * do this is to copy the string.
     */
    err_copy = strndup (err, n);
    if (err_copy == NULL) {
      nbdkit_error ("malloc failure: original error: %s", err);
      return -1;
    }
    if (n > 0 && err_copy[n-1] == '\n')
      err_copy[n-1] = '\0';

    nbdkit_error ("%s", err_copy);

    return -1;
  }

  return 0;
}

static int last_error;

XS(set_error)
{
  dXSARGS;
  /* Is it worth adding error checking for bad arguments? */
  if (items >= 1) {
    last_error = SvIV (ST (0));
    nbdkit_set_error (last_error);
  }
  XSRETURN_EMPTY;
}

EXTERN_C void boot_DynaLoader (pTHX_ CV *cv);

static void
xs_init (pTHX)
{
  char *file = __FILE__;
  newXS ("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
  newXS ("Nbdkit::set_error", set_error, file);
}

static void
perl_dump_plugin (void)
{
  dSP;

#ifdef PERL_VERSION_STRING
  printf ("perl_version=%s\n", PERL_VERSION_STRING);
#endif

  if (script && callback_defined ("dump_plugin")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    PUTBACK;
    call_pv ("dump_plugin", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;
  }
}

static int
perl_config (const char *key, const char *value)
{
  if (!script) {
    int argc = 2;
    char *argv[3] = { "nbdkit", NULL, NULL };

    /* The first parameter MUST be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be "
                    "script=/path/to/perl/script.pl");
      return -1;
    }
    script = value;

    assert (my_perl);

    /* Load the Perl script. */
    argv[1] = (char *) script;
    if (perl_parse (my_perl, xs_init, argc, argv, NULL) == -1) {
      nbdkit_error ("%s: error parsing this script", script);
      return -1;
    }

    /* Run the Perl script.  Note that top-level definitions such as
     * global variables don't work at all unless you do this.
     */
    if (perl_run (my_perl) == -1) {
      nbdkit_error ("%s: error running this script", script);
      return -1;
    }

    /* Minimal set of callbacks which are required (by nbdkit itself). */
    if (!callback_defined ("open") ||
        !callback_defined ("get_size") ||
        !callback_defined ("pread")) {
      nbdkit_error ("%s: one of the required callbacks "
                    "'open', 'get_size' or 'pread' "
                    "is not defined by this Perl script.  "
                    "nbdkit requires these callbacks.", script);
      return -1;
    }
  }
  else if (callback_defined ("config")) {
    dSP;

    /* Other parameters are passed to the Perl .config callback. */
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (sv_2mortal (newSVpv (key, strlen (key))));
    XPUSHs (sv_2mortal (newSVpv (value, strlen (value))));
    PUTBACK;
    call_pv ("config", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (check_perl_failure () == -1)
      return -1;
  }
  else {
    /* Emulate what core nbdkit does if a config callback is NULL. */
    nbdkit_error ("%s: this plugin does not need command line configuration",
                  script);
    return -1;
  }

  return 0;
}

static int
perl_config_complete (void)
{
  dSP;

  if (callback_defined ("config_complete")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    PUTBACK;
    call_pv ("config_complete", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;
    if (check_perl_failure () == -1)
      return -1;
  }

  return 0;
}

static void *
perl_open (int readonly)
{
  SV *sv;
  dSP;

  /* We check in perl_config that this callback is defined. */
  ENTER;
  SAVETMPS;
  PUSHMARK (SP);
  XPUSHs (readonly ? &PL_sv_yes : &PL_sv_no);
  PUTBACK;
  call_pv ("open", G_EVAL|G_SCALAR);
  SPAGAIN;
  sv = newSVsv (POPs);
  PUTBACK;
  FREETMPS;
  LEAVE;

  if (check_perl_failure () == -1)
    return NULL;

  nbdkit_debug ("open returns handle (SV *) = %p (type %d)",
                sv, SvTYPE (sv));

  return sv;
}

static void
perl_close (void *handle)
{
  dSP;

  nbdkit_debug ("close called with handle (SV *) = %p (type %d)",
                handle, SvTYPE ((SV *) handle));

  if (callback_defined ("close")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    PUTBACK;
    call_pv ("close", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    check_perl_failure ();      /* ignore return value */
  }

  /* Since nbdkit has closed (and forgotten) the handle, we can now
   * drop its refcount.
   */
  SvREFCNT_dec ((SV *) handle);
}

static int64_t
perl_get_size (void *handle)
{
  dSP;
  SV *sv;
  int64_t size;

  /* We check in perl_config that this callback is defined. */
  ENTER;
  SAVETMPS;
  PUSHMARK (SP);
  XPUSHs (handle);
  PUTBACK;
  call_pv ("get_size", G_EVAL|G_SCALAR);
  SPAGAIN;
  /* For some reason, this only works if split into two separate statements: */
  sv = POPs;
  size = SvIV (sv);
  PUTBACK;
  FREETMPS;
  LEAVE;

  if (check_perl_failure () == -1)
    return -1;

  nbdkit_debug ("get_size returned %" PRIi64, size);

  return size;
}

static int
perl_boolean (void *handle, const char *callback_name, const char *fn_name)
{
  dSP;
  SV *sv;
  int r;

  if (callback_defined (callback_name)) {
    /* If there's a Perl callback, call it. */
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    PUTBACK;
    call_pv (callback_name, G_EVAL|G_SCALAR);
    SPAGAIN;
    sv = POPs;
    r = SvIV (sv);
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (check_perl_failure () == -1)
      return -1;

    return r;
  }
  /* No Perl callback.  If the function is defined, return 1. */
  else if (fn_name && callback_defined (fn_name))
    return 1;
  else
    return 0;
}

static int
perl_can_write (void *handle)
{
  return perl_boolean (handle, "can_write", "write");
}

static int
perl_can_flush (void *handle)
{
  return perl_boolean (handle, "can_flush", "flush");
}

static int
perl_can_trim (void *handle)
{
  return perl_boolean (handle, "can_trim", "trim");
}

static int
perl_is_rotational (void *handle)
{
  return perl_boolean (handle, "is_rotational", NULL);
}

static int
perl_pread (void *handle, void *buf,
            uint32_t count, uint64_t offset)
{
  dSP;
  SV *sv;
  const char *pbuf;
  STRLEN len;
  int ret = 0;

  /* We check in perl_config that this callback is defined. */
  ENTER;
  SAVETMPS;
  PUSHMARK (SP);
  XPUSHs (handle);
  XPUSHs (sv_2mortal (newSViv (count)));
  XPUSHs (sv_2mortal (newSViv (offset)));
  PUTBACK;
  call_pv ("pread", G_EVAL|G_SCALAR);
  SPAGAIN;
  sv = POPs;
  pbuf = SvPV (sv, len);
  if (len < count) {
    nbdkit_error ("buffer returned from pread is too small");
    ret = -1;
  }
  else
    memcpy (buf, pbuf, count);
  PUTBACK;
  FREETMPS;
  LEAVE;

  if (check_perl_failure () == -1)
    ret = -1;

  return ret;
}

static int
perl_pwrite (void *handle, const void *buf,
             uint32_t count, uint64_t offset)
{
  dSP;

  if (callback_defined ("pwrite")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    XPUSHs (sv_2mortal (newSVpv (buf, count)));
    XPUSHs (sv_2mortal (newSViv (offset)));
    PUTBACK;
    call_pv ("pwrite", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (check_perl_failure () == -1)
      return -1;

    return 0;
  }

  nbdkit_error ("write not implemented");
  return -1;
}

static int
perl_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  dSP;

  if (callback_defined ("zero")) {
    last_error = 0;
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    XPUSHs (sv_2mortal (newSViv (count)));
    XPUSHs (sv_2mortal (newSViv (offset)));
    XPUSHs (sv_2mortal (newSViv (may_trim)));
    PUTBACK;
    call_pv ("zero", G_EVAL|G_SCALAR);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (last_error == EOPNOTSUPP || last_error == ENOTSUP) {
      /* When user requests this particular error, we want to
         gracefully fall back, and to accomodate both a normal return
         and an exception. */
      nbdkit_debug ("zero requested falling back to pwrite");
      return -1;
    }
    if (check_perl_failure () == -1)
      return -1;

    return 0;
  }

  nbdkit_debug ("zero falling back to pwrite");
  nbdkit_set_error (EOPNOTSUPP);
  return -1;
}

static int
perl_flush (void *handle)
{
  dSP;

  if (callback_defined ("flush")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    PUTBACK;
    call_pv ("flush", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (check_perl_failure () == -1)
      return -1;

    return 0;
  }

  /* Ignore lack of flush callback in Perl, although probably nbdkit
   * will never call this since .can_flush returns false.
   */
  return 0;
}

static int
perl_trim (void *handle, uint32_t count, uint64_t offset)
{
  dSP;

  if (callback_defined ("trim")) {
    ENTER;
    SAVETMPS;
    PUSHMARK (SP);
    XPUSHs (handle);
    XPUSHs (sv_2mortal (newSViv (count)));
    XPUSHs (sv_2mortal (newSViv (offset)));
    PUTBACK;
    call_pv ("trim", G_EVAL|G_VOID|G_DISCARD);
    SPAGAIN;
    PUTBACK;
    FREETMPS;
    LEAVE;

    if (check_perl_failure () == -1)
      return -1;

    return 0;
  }

  /* Ignore lack of trim callback in Perl, although probably nbdkit
   * will never call this since .can_trim returns false.
   */
  return 0;
}

#define perl_config_help \
  "script=<FILENAME>     (required) The Perl plugin to run.\n" \
  "[other arguments may be used by the plugin that you load]"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "perl",
  .version           = PACKAGE_VERSION,

  .load              = perl_load,
  .unload            = perl_unload,
  .dump_plugin       = perl_dump_plugin,

  .config            = perl_config,
  .config_complete   = perl_config_complete,
  .config_help       = perl_config_help,

  .open              = perl_open,
  .close             = perl_close,

  .get_size          = perl_get_size,
  .can_write         = perl_can_write,
  .can_flush         = perl_can_flush,
  .is_rotational     = perl_is_rotational,
  .can_trim          = perl_can_trim,

  .pread             = perl_pread,
  .pwrite            = perl_pwrite,
  .flush             = perl_flush,
  .trim              = perl_trim,
  .zero              = perl_zero,
};

NBDKIT_REGISTER_PLUGIN(plugin)
