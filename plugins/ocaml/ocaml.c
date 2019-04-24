/* nbdkit
 * Copyright (C) 2014-2019 Red Hat Inc.
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

  /* The following field is used as a canary to detect whether the
   * OCaml code started up and called us back successfully.  If it's
   * still set to NULL when plugin_init is called, then we can print a
   * suitable error message.
   */
  .name = NULL,

  .unload = unload_wrapper,
};

struct nbdkit_plugin *
plugin_init (void)
{
  if (plugin.name == NULL) {
    fprintf (stderr, "error: OCaml code did not call NBDKit.register_plugin\n");
    exit (EXIT_FAILURE);
  }
  return &plugin;
}

/* These globals store the OCaml functions that we actually call.
 * Also the assigned ones are roots to ensure the GC doesn't free them.
 */
static value load_fn;
static value unload_fn;

static value config_fn;
static value config_complete_fn;

static value open_fn;
static value close_fn;

static value get_size_fn;

static value can_write_fn;
static value can_flush_fn;
static value is_rotational_fn;
static value can_trim_fn;

static value dump_plugin_fn;

static value can_zero_fn;
static value can_fua_fn;

static value pread_fn;
static value pwrite_fn;
static value flush_fn;
static value trim_fn;
static value zero_fn;

static value can_multi_conn_fn;

static value can_extents_fn;
static value extents_fn;

/*----------------------------------------------------------------------*/
/* Wrapper functions that translate calls from C (ie. nbdkit) to OCaml. */

static void
load_wrapper (void)
{
  caml_leave_blocking_section ();
  caml_callback (load_fn, Val_unit);
  caml_enter_blocking_section ();
}

/* We always have an unload function, since it also has to free the
 * globals we allocated.
 */
static void
unload_wrapper (void)
{
  if (unload_fn) {
    caml_leave_blocking_section ();
    caml_callback (unload_fn, Val_unit);
    caml_enter_blocking_section ();
  }

  free ((char *) plugin.name);
  free ((char *) plugin.longname);
  free ((char *) plugin.version);
  free ((char *) plugin.description);
  free ((char *) plugin.config_help);

  remove_roots ();
}

static int
config_wrapper (const char *key, const char *val)
{
  CAMLparam0 ();
  CAMLlocal3 (keyv, valv, rv);

  caml_leave_blocking_section ();

  keyv = caml_copy_string (key);
  valv = caml_copy_string (val);

  rv = caml_callback2_exn (config_fn, keyv, valv);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
config_complete_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (config_complete_fn, Val_unit);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static void *
open_wrapper (int readonly)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  value *ret;

  caml_leave_blocking_section ();

  rv = caml_callback_exn (open_fn, Val_bool (readonly));
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (void *, NULL);
  }

  /* Allocate a root on the C heap that points to the OCaml handle. */
  ret = malloc (sizeof *ret);
  if (ret == NULL) abort ();
  *ret = rv;
  caml_register_generational_global_root (ret);

  caml_enter_blocking_section ();
  CAMLreturnT (void *, ret);
}

static void
close_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (close_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    /*FALLTHROUGH*/
  }

  caml_remove_generational_global_root (h);
  free (h);

  caml_enter_blocking_section ();
  CAMLreturn0;
}

static int64_t
get_size_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);
  int64_t r;

  caml_leave_blocking_section ();

  rv = caml_callback_exn (get_size_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int64_t, -1);
  }

  r = Int64_val (rv);

  caml_enter_blocking_section ();
  CAMLreturnT (int64_t, r);
}

static int
can_write_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_write_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
can_flush_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_flush_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
is_rotational_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (is_rotational_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
can_trim_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_trim_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static void
dump_plugin_wrapper (void)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (dump_plugin_fn, Val_unit);
  if (Is_exception_result (rv))
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
  caml_enter_blocking_section ();
  CAMLreturn0;
}

static int
can_zero_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_zero_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
can_fua_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_fua_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Int_val (rv));
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

  caml_leave_blocking_section ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (pread_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  len = caml_string_length (rv);
  if (len < count) {
    nbdkit_error ("buffer returned from pread is too small");
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  memcpy (buf, String_val (rv), count);

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
pwrite_wrapper (void *h, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, strv, offsetv, flagsv);

  caml_leave_blocking_section ();

  strv = caml_alloc_string (count);
  memcpy (String_val (strv), buf, count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, strv, offsetv, flagsv };
  rv = caml_callbackN_exn (pwrite_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
flush_wrapper (void *h, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal2 (rv, flagsv);

  caml_leave_blocking_section ();

  flagsv = Val_flags (flags);

  rv = caml_callback2_exn (flush_fn, *(value *) h, flagsv);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
trim_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);

  caml_leave_blocking_section ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (trim_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
zero_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);

  caml_leave_blocking_section ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (zero_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

static int
can_multi_conn_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_multi_conn_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
can_extents_wrapper (void *h)
{
  CAMLparam0 ();
  CAMLlocal1 (rv);

  caml_leave_blocking_section ();

  rv = caml_callback_exn (can_extents_fn, *(value *) h);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, Bool_val (rv));
}

static int
extents_wrapper (void *h, uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents)
{
  CAMLparam0 ();
  CAMLlocal5 (rv, countv, offsetv, flagsv, v);

  caml_leave_blocking_section ();

  countv = caml_copy_int32 (count);
  offsetv = caml_copy_int32 (offset);
  flagsv = Val_flags (flags);

  value args[] = { *(value *) h, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (extents_fn, sizeof args / sizeof args[0], args);
  if (Is_exception_result (rv)) {
    nbdkit_error ("%s", caml_format_exception (Extract_exception (rv)));
    caml_enter_blocking_section ();
    CAMLreturnT (int, -1);
  }

  /* Convert extents list into calls to nbdkit_add_extent. */
  while (rv != Val_int (0)) {
    uint64_t offset, length;
    uint32_t type = 0;

    v = Field (rv, 0);          /* extent struct */
    offset = Int64_val (Field (v, 0));
    length = Int64_val (Field (v, 1));
    if (Bool_val (Field (v, 2)))
      type |= NBDKIT_EXTENT_HOLE;
    if (Bool_val (Field (v, 3)))
      type |= NBDKIT_EXTENT_ZERO;
    if (nbdkit_add_extent (extents, offset, length, type) == -1) {
      caml_enter_blocking_section ();
      CAMLreturnT (int, -1);
    }

    rv = Field (rv, 1);
  }

  caml_enter_blocking_section ();
  CAMLreturnT (int, 0);
}

/*----------------------------------------------------------------------*/
/* set_* functions called from OCaml code at load time to initialize
 * fields in the plugin struct.
 */

value
ocaml_nbdkit_set_thread_model (value modelv)
{
  plugin._thread_model = Int_val (modelv);
  return Val_unit;
}

value
ocaml_nbdkit_set_name (value namev)
{
  plugin.name = strdup (String_val (namev));
  return Val_unit;
}

value
ocaml_nbdkit_set_longname (value longnamev)
{
  plugin.longname = strdup (String_val (longnamev));
  return Val_unit;
}

value
ocaml_nbdkit_set_version (value versionv)
{
  plugin.version = strdup (String_val (versionv));
  return Val_unit;
}

value
ocaml_nbdkit_set_description (value descriptionv)
{
  plugin.description = strdup (String_val (descriptionv));
  return Val_unit;
}

value
ocaml_nbdkit_set_config_help (value helpv)
{
  plugin.config_help = strdup (String_val (helpv));
  return Val_unit;
}

#define SET(fn)                                         \
  value                                                 \
  ocaml_nbdkit_set_##fn (value fv)                      \
  {                                                     \
    plugin.fn = fn##_wrapper;                           \
    fn##_fn = fv;                                       \
    caml_register_generational_global_root (&fn##_fn);  \
    return Val_unit;                                    \
  }

SET(load)
SET(unload)

SET(config)
SET(config_complete)

SET(open)
SET(close)

SET(get_size)

SET(can_write)
SET(can_flush)
SET(is_rotational)
SET(can_trim)

SET(dump_plugin)

SET(can_zero)
SET(can_fua)

SET(pread)
SET(pwrite)
SET(flush)
SET(trim)
SET(zero)

SET(can_multi_conn)

SET(can_extents)
SET(extents)

#undef SET

static void
remove_roots (void)
{
#define REMOVE(fn) \
  if (fn##_fn) caml_remove_generational_global_root (&fn##_fn)
  REMOVE (load);
  REMOVE (unload);

  REMOVE (config);
  REMOVE (config_complete);

  REMOVE (open);
  REMOVE (close);

  REMOVE (get_size);

  REMOVE (can_write);
  REMOVE (can_flush);
  REMOVE (is_rotational);
  REMOVE (can_trim);

  REMOVE (dump_plugin);

  REMOVE (can_zero);
  REMOVE (can_fua);

  REMOVE (pread);
  REMOVE (pwrite);
  REMOVE (flush);
  REMOVE (trim);
  REMOVE (zero);

  REMOVE (can_multi_conn);

  REMOVE (can_extents);
  REMOVE (extents);

#undef REMOVE
}

/*----------------------------------------------------------------------*/
/* Bindings for miscellaneous nbdkit_* utility functions. */

/* NB: noalloc function. */
value
ocaml_nbdkit_set_error (value nv)
{
  int err;

  switch (Int_val (nv)) {
  case 1: err = EPERM; break;
  case 2: err = EIO; break;
  case 3: err = ENOMEM; break;
  case 4: err = EINVAL; break;
  case 5: err = ENOSPC; break;
  case 6: err = ESHUTDOWN; break;
  default: abort ();
  }

  nbdkit_set_error (err);

  return Val_unit;
}

value
ocaml_nbdkit_parse_size (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  int64_t r;

  r = nbdkit_parse_size (String_val (strv));
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_size");
  rv = caml_copy_int64 (r);

  CAMLreturn (rv);
}

value
ocaml_nbdkit_parse_bool (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  int r;

  r = nbdkit_parse_bool (String_val (strv));
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_bool");
  rv = Val_bool (r);

  CAMLreturn (rv);
}

value
ocaml_nbdkit_read_password (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  char *password;
  int r;

  r = nbdkit_read_password (String_val (strv), &password);
  if (r == -1)
    caml_invalid_argument ("nbdkit_read_password");
  rv = caml_copy_string (password);
  free (password);

  CAMLreturn (rv);
}

/* NB: noalloc function. */
value
ocaml_nbdkit_debug (value strv)
{
  nbdkit_debug ("%s", String_val (strv));

  return Val_unit;
}
