/* nbdkit
 * Copyright (C) 2014-2020 Red Hat Inc.
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
#include <errno.h>

#include <caml/alloc.h>
#include <caml/callback.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>
#include <caml/printexc.h>
#include <caml/threads.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "plugin.h"

/* This constructor runs when the plugin loads, and initializes the
 * OCaml runtime, and lets the plugin set up its callbacks.
 */
static void constructor (void) __attribute__((constructor));
static void
constructor (void)
{
  char *argv[2] = { "nbdkit", NULL };

  /* Initialize OCaml runtime. */
  caml_startup (argv);
}

/* Instead of using the NBDKIT_REGISTER_PLUGIN macro, we construct the
 * nbdkit_plugin struct and return it from our own plugin_init
 * function.
 */
static void unload_wrapper (void);
static void remove_roots (void);

static struct nbdkit_plugin plugin = {
  ._struct_size = sizeof (plugin),
  ._api_version = NBDKIT_API_VERSION,
  ._thread_model = NBDKIT_THREAD_MODEL_PARALLEL,

  /* The following field is used as a canary to detect whether the
   * OCaml code started up and called us back successfully.  If it's
   * still set to NULL when plugin_init is called, then we can print a
   * suitable error message.
   */
  .name = NULL,

  .unload = unload_wrapper,
};

NBDKIT_DLL_PUBLIC struct nbdkit_plugin *
plugin_init (void)
{
  if (plugin.name == NULL) {
    fprintf (stderr, "error: OCaml code did not call NBDKit.register_plugin\n");
    exit (EXIT_FAILURE);
  }
  return &plugin;
}

/* There is one global per callback called <callback>_fn.  These
 * globals store the OCaml functions that we actually call.  Also the
 * assigned ones are roots to ensure the GC doesn't free them.
 */
#define CB(name) static value name##_fn;
#include "callbacks.h"
#undef CB

/*----------------------------------------------------------------------*/
/* Wrapper functions that translate calls from C (ie. nbdkit) to OCaml. */

static void
load_wrapper (void)
{
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();
  caml_callback (load_fn, Val_unit);
}

/* We always have an unload function, since it also has to free the
 * globals we allocated.
 */
static void
unload_wrapper (void)
{
  if (unload_fn) {
    LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();
    caml_callback (unload_fn, Val_unit);
  }

  free ((char *) plugin.name);
  free ((char *) plugin.longname);
  free ((char *) plugin.version);
  free ((char *) plugin.description);
  free ((char *) plugin.config_help);

  remove_roots ();

#ifdef HAVE_CAML_SHUTDOWN
  caml_shutdown ();
#endif
}

static void
dump_plugin_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (dump_plugin_fn, Val_unit);
  if (Is_exception_result (rv))
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
  CAMLreturn0;
}

static int
config_wrapper (const char *key, const char *val)
{
  CAMLparam0 ();
  CAMLlocal3 (keyv, valv, rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  keyv = caml_copy_string (key);
  valv = caml_copy_string (val);

  rv = caml_callback2_exn (config_fn, keyv, valv);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
config_complete_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (config_complete_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
thread_model_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (thread_model_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Int_val (rv));
}

static int
get_ready_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (get_ready_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
after_fork_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (after_fork_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static void
cleanup_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (cleanup_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturn0;
  }

  CAMLreturn0;
}

static int
preconnect_wrapper (int readonly)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (preconnect_fn, Val_bool (readonly));
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
list_exports_wrapper (int readonly, int is_tls, struct nbdkit_exports *exports)
{
  CAMLparam0 ();
  CAMLlocal2 (rv, v);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback2_exn (list_exports_fn, Val_bool (readonly),
                           Val_bool (is_tls));
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  /* Convert exports list into calls to nbdkit_add_export. */
  while (rv != Val_emptylist) {
    const char *name, *desc = NULL;

    v = Field (rv, 0);          /* export struct */
    name = String_val (Field (v, 0));
    if (Is_block (Field (v, 1)))
      desc = String_val (Field (Field (v, 1), 0));
    if (nbdkit_add_export (exports, name, desc) == -1) {
      CAMLreturnT (int, -1);
    }

    rv = Field (rv, 1);
  }

  CAMLreturnT (int, 0);
}

static const char *
default_export_wrapper (int readonly, int is_tls)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  const char *name;
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback2_exn (default_export_fn, Val_bool (readonly),
                           Val_bool (is_tls));
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (const char *, NULL);
  }

  name = nbdkit_strdup_intern (String_val (rv));
  CAMLreturnT (const char *, name);
}

static void *
open_wrapper (int readonly)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  value *ret;
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (open_fn, Val_bool (readonly));
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (void *, NULL);
  }

  /* Allocate a root on the C heap that points to the OCaml handle. */
  ret = malloc (sizeof *ret);
  if (ret == NULL) abort ();
  *ret = rv;
  caml_register_generational_global_root (ret);

  CAMLreturnT (void *, ret);
}

static void
close_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (close_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    /*FALLTHROUGH*/
  }

  caml_remove_generational_global_root (h);
  free (h);

  CAMLreturn0;
}

static const char *
export_description_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  const char *desc;
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (export_description_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (const char *, NULL);
  }

  desc = nbdkit_strdup_intern (String_val (rv));
  CAMLreturnT (const char *, desc);
}

static int64_t
get_size_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  int64_t r;
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (get_size_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int64_t, -1);
  }

  r = Int64_val (rv);
  CAMLreturnT (int64_t, r);
}

static int
can_write_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_write_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_flush_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_flush_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
is_rotational_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (is_rotational_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_trim_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_trim_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_zero_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_zero_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_fua_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_fua_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Int_val (rv));
}

static int
can_fast_zero_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_fast_zero_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_cache_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_cache_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Int_val (rv));
}

static int
can_extents_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_extents_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_multi_conn_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  rv = caml_callback_exn (can_multi_conn_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, Bool_val (rv));
}

static value
Val_flags (uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal2 (consv, rv);

  rv = Val_unit;
  if (flags & NBDKIT_FLAG_MAY_TRIM) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 0); /* 0 = May_trim */
    Store_field (consv, 1, rv);
    rv = consv;
  }
  if (flags & NBDKIT_FLAG_FUA) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 1); /* 1 = FUA */
    Store_field (consv, 1, rv);
    rv = consv;
  }
  if (flags & NBDKIT_FLAG_REQ_ONE) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 2); /* 2 = Req_one */
    Store_field (consv, 1, rv);
    rv = consv;
  }

  CAMLreturn (rv);
}

static int
pread_wrapper (void *h, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  mlsize_t len;
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (pread_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  len = caml_string_length (rv);
  if (len < count) {
    nbdkit_error ("buffer returned from pread is too small");
    CAMLreturnT (int, -1);
  }

  memcpy (buf, String_val (rv), count);
  CAMLreturnT (int, 0);
}

static int
pwrite_wrapper (void *h, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, strv, offsetv, flagsv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  strv = caml_alloc_initialized_string (count, buf);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, strv, offsetv, flagsv };
  rv = caml_callbackN_exn (pwrite_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
flush_wrapper (void *h, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal2 (rv, flagsv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  flagsv = Val_flags (flags);

  rv = caml_callback2_exn (flush_fn, *(value *) h, flagsv);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
trim_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (trim_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
zero_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (zero_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

static int
extents_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents)
{
  CAMLparam0 ();
  CAMLlocal5 (rv, countv, offsetv, flagsv, v);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (extents_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  /* Convert extents list into calls to nbdkit_add_extent. */
  while (rv != Val_emptylist) {
    uint64_t length;
    uint32_t type = 0;

    v = Field (rv, 0);          /* extent struct */
    offset = Int64_val (Field (v, 0));
    length = Int64_val (Field (v, 1));
    if (Bool_val (Field (v, 2)))
      type |= NBDKIT_EXTENT_HOLE;
    if (Bool_val (Field (v, 3)))
      type |= NBDKIT_EXTENT_ZERO;
    if (nbdkit_add_extent (extents, offset, length, type) == -1) {
      CAMLreturnT (int, -1);
    }

    rv = Field (rv, 1);
  }

  CAMLreturnT (int, 0);
}

static int
cache_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  LEAVE_BLOCKING_SECTION_FOR_CURRENT_SCOPE ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (cache_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  CAMLreturnT (int, 0);
}

/*----------------------------------------------------------------------*/
/* set_* functions called from OCaml code at load time to initialize
 * fields in the plugin struct.
 */

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_name (value namev)
{
  plugin.name = strdup (String_val (namev));
  return Val_unit;
}

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_longname (value longnamev)
{
  plugin.longname = strdup (String_val (longnamev));
  return Val_unit;
}

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_version (value versionv)
{
  plugin.version = strdup (String_val (versionv));
  return Val_unit;
}

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_description (value descriptionv)
{
  plugin.description = strdup (String_val (descriptionv));
  return Val_unit;
}

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_config_help (value helpv)
{
  plugin.config_help = strdup (String_val (helpv));
  return Val_unit;
}

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_field (value fieldv, value fv)
{
  const char *field = String_val (fieldv);
  value *root;

  /* This isn't very efficient because we string-compare the field
   * names.  However it is only called when the plugin is being loaded
   * for a handful of fields so it's not performance critical.
   */
#define CB(name)                                \
  if (strcmp (field, #name) == 0) {             \
    plugin.name = name##_wrapper;               \
    name##_fn = fv;                             \
    root = &name##_fn;                          \
  } else
#include "callbacks.h"
#undef CB
  /* else if the field is not known */ abort ();

  caml_register_generational_global_root (root);
  return Val_unit;
}

/* Called from unload() to remove the GC roots registered by set* functions. */
static void
remove_roots (void)
{
#define CB(name) \
  if (name##_fn) caml_remove_generational_global_root (&name##_fn);
#include "callbacks.h"
#undef CB
}
