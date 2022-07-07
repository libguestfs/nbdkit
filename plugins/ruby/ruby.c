/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <assert.h>
#include <errno.h>

#include <nbdkit-plugin.h>

#include <ruby.h>
#ifdef HAVE_RUBY_VERSION_H
#include <ruby/version.h>
#endif

#include "array-size.h"

static VALUE nbdkit_module = Qnil;
static int last_error;

static VALUE
set_error (VALUE self, VALUE arg)
{
  int err;
  VALUE v;

  if (TYPE(arg) == T_CLASS) {
    v = rb_const_get (arg, rb_intern ("Errno"));
    err = NUM2INT (v);
  } else if (TYPE (arg) == T_OBJECT) {
    v = rb_funcall (arg, rb_intern ("errno"), 0);
    err = NUM2INT (v);
  } else {
    err = NUM2INT (arg);
  }
  last_error = err;
  nbdkit_set_error (err);
  return Qnil;
}

static void
plugin_rb_load (void)
{
  RUBY_INIT_STACK;
  ruby_init ();
  ruby_init_loadpath ();

  nbdkit_module = rb_define_module ("Nbdkit");
  rb_define_module_function (nbdkit_module, "set_error", set_error, 1);
}

/* https://stackoverflow.com/questions/11086549/how-to-rb-protect-everything-in-ruby */
#define MAX_ARGS 16
struct callback_data {
  VALUE receiver;               /* object being called */
  ID method_id;                 /* method on object being called */
  int argc;                     /* number of args */
  VALUE argv[MAX_ARGS];         /* list of args */
};

static VALUE
callback_dispatch (VALUE datav)
{
  struct callback_data *data = (struct callback_data *) datav;
  return rb_funcall2 (data->receiver, data->method_id, data->argc, data->argv);
}

enum exception_class {
  NO_EXCEPTION = 0,
  EXCEPTION_NO_METHOD_ERROR,
  EXCEPTION_OTHER,
};

static VALUE
funcall2 (VALUE receiver, ID method_id, int argc, volatile VALUE *argv,
          enum exception_class *exception_happened)
{
  struct callback_data data;
  size_t i, len;
  int state = 0;
  volatile VALUE ret, exn, message, backtrace, b;

  assert (argc <= MAX_ARGS);

  data.receiver = receiver;
  data.method_id = method_id;
  data.argc = argc;
  for (i = 0; i < argc; ++i)
    data.argv[i] = argv[i];

  ret = rb_protect (callback_dispatch, (VALUE) &data, &state);
  if (state) {
    /* An exception was thrown.  Get the per-thread exception. */
    exn = rb_errinfo ();

    /* We treat NoMethodError specially. */
    if (rb_obj_is_kind_of (exn, rb_eNoMethodError)) {
      if (exception_happened)
        *exception_happened = EXCEPTION_NO_METHOD_ERROR;
    }
    else {
      if (exception_happened)
        *exception_happened = EXCEPTION_OTHER;

      /* Print the exception. */
      message = rb_funcall (exn, rb_intern ("to_s"), 0);
      nbdkit_error ("ruby: %s", StringValueCStr (message));

      /* Try to print the backtrace (a list of strings) if it exists. */
      backtrace = rb_funcall (exn, rb_intern ("backtrace"), 0);
      if (! NIL_P (backtrace)) {
        len = RARRAY_LEN (backtrace);
        for (i = 0; i < len; ++i) {
          b = rb_ary_entry (backtrace, i);
          nbdkit_error ("ruby: frame #%zu %s", i, StringValueCStr (b));
        }
      }
    }

    /* Reset the current thread exception. */
    rb_set_errinfo (Qnil);
    return Qnil;
  }
  else {
    if (exception_happened)
      *exception_happened = NO_EXCEPTION;
    return ret;
  }
}

static const char *script = NULL;
static void *code = NULL;

static void
plugin_rb_unload (void)
{
  if (ruby_cleanup (0) != 0)
    nbdkit_error ("ruby_cleanup failed");
}

static void
plugin_rb_dump_plugin (void)
{
#ifdef RUBY_API_VERSION_MAJOR
  printf ("ruby_api_version=%d", RUBY_API_VERSION_MAJOR);
#ifdef RUBY_API_VERSION_MINOR
  printf (".%d", RUBY_API_VERSION_MINOR);
#ifdef RUBY_API_VERSION_TEENY
  printf (".%d", RUBY_API_VERSION_TEENY);
#endif
#endif
  printf ("\n");
#endif

  if (!script)
    return;

  assert (code != NULL);

  (void) funcall2 (Qnil, rb_intern ("dump_plugin"), 0, NULL, NULL);
}

static int
plugin_rb_config (const char *key, const char *value)
{
  /* The first parameter must be "script". */
  if (!script) {
    int state;

    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be script=/path/to/ruby/script.rb");
      return -1;
    }
    script = value;

    nbdkit_debug ("ruby: loading script %s", script);

    /* Load the Ruby script into the interpreter. */
    const char *options[] = { "--", script };
    code = ruby_options (ARRAY_SIZE (options), (char **) options);

    /* Check if we managed to compile the Ruby script to code. */
    if (!ruby_executable_node (code, &state)) {
      nbdkit_error ("could not compile ruby script (%s, state=%d)",
                    script, state);
      return -1;
    }

    /* Execute the Ruby script. */
    state = ruby_exec_node (code);
    if (state) {
      nbdkit_error ("could not execute ruby script (%s, state=%d)",
                    script, state);
      return -1;
    }

    return 0;
  }
  else {
    volatile VALUE argv[2];
    enum exception_class exception_happened;

    argv[0] = rb_str_new2 (key);
    argv[1] = rb_str_new2 (value);
    (void) funcall2 (Qnil, rb_intern ("config"), 2, argv, &exception_happened);
    if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
      /* No config method, emulate what core nbdkit does if the
       * config callback is NULL.
       */
      nbdkit_error ("%s: this plugin does not need command line configuration",
                    script);
      return -1;
    }
    else if (exception_happened == EXCEPTION_OTHER)
      return -1;

    return 0;
  }
}

static int
plugin_rb_config_complete (void)
{
  enum exception_class exception_happened;

  if (!script) {
    nbdkit_error ("the first parameter must be script=/path/to/ruby/script.rb");
    return -1;
  }

  assert (code != NULL);

  (void) funcall2 (Qnil, rb_intern ("config_complete"), 0, NULL,
                   &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR)
    return 0;          /* no config_complete method defined, ignore */
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return 0;
}

static void *
plugin_rb_open (int readonly)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = readonly ? Qtrue : Qfalse;
  rv = funcall2 (Qnil, rb_intern ("open"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: missing callback: %s", script, "open");
    return NULL;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return NULL;

  return (void *) rv;
}

static void
plugin_rb_close (void *handle)
{
  volatile VALUE argv[1];

  argv[0] = (VALUE) handle;
  (void) funcall2 (Qnil, rb_intern ("close"), 1, argv, NULL);
  /* OK to ignore exceptions here, if they are important then an error
   * was printed already.
   */
}

static int64_t
plugin_rb_get_size (void *handle)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  rv = funcall2 (Qnil, rb_intern ("get_size"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: missing callback: %s", script, "get_size");
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return NUM2ULL (rv);
}

static int
plugin_rb_pread (void *handle, void *buf,
                 uint32_t count, uint64_t offset)
{
  volatile VALUE argv[3];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  argv[1] = ULL2NUM (count);
  argv[2] = ULL2NUM (offset);
  rv = funcall2 (Qnil, rb_intern ("pread"), 3, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: missing callback: %s", script, "pread");
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  if (RSTRING_LEN (rv) < count) {
    nbdkit_error ("%s: byte array returned from pread is too small",
                  script);
    return -1;
  }

  memcpy (buf, RSTRING_PTR (rv), count);
  return 0;
}

static int
plugin_rb_pwrite (void *handle, const void *buf,
                  uint32_t count, uint64_t offset)
{
  volatile VALUE argv[3];
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  argv[1] = rb_str_new (buf, count);
  argv[2] = ULL2NUM (offset);
  (void) funcall2 (Qnil, rb_intern ("pwrite"), 3, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: missing callback: %s", script, "pwrite");
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return 0;
}

static int
plugin_rb_flush (void *handle)
{
  volatile VALUE argv[1];
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  (void) funcall2 (Qnil, rb_intern ("flush"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: not implemented: %s", script, "flush");
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return 0;
}

static int
plugin_rb_trim (void *handle, uint32_t count, uint64_t offset)
{
  volatile VALUE argv[3];
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  argv[1] = ULL2NUM (count);
  argv[2] = ULL2NUM (offset);
  (void) funcall2 (Qnil, rb_intern ("trim"), 3, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_error ("%s: not implemented: %s", script, "trim");
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return 0;
}

static int
plugin_rb_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  volatile VALUE argv[4];
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  argv[1] = ULL2NUM (count);
  argv[2] = ULL2NUM (offset);
  argv[3] = may_trim ? Qtrue : Qfalse;
  last_error = 0;
  (void) funcall2 (Qnil, rb_intern ("zero"), 4, argv, &exception_happened);
  if (last_error == EOPNOTSUPP || last_error == ENOTSUP ||
      exception_happened == EXCEPTION_NO_METHOD_ERROR) {
    nbdkit_debug ("zero falling back to pwrite");
    nbdkit_set_error (EOPNOTSUPP);
    return -1;
  }
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return 0;
}

static int
plugin_rb_can_write (void *handle)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  rv = funcall2 (Qnil, rb_intern ("can_write"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR)
    /* Fall back to checking if the pwrite method exists. */
    rv = rb_funcall (Qnil, rb_intern ("respond_to?"),
                     2, ID2SYM (rb_intern ("pwrite")), Qtrue);
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return RTEST (rv);
}

static int
plugin_rb_can_flush (void *handle)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  rv = funcall2 (Qnil, rb_intern ("can_flush"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR)
    /* Fall back to checking if the flush method exists. */
    rv = rb_funcall (Qnil, rb_intern ("respond_to?"),
                     2, ID2SYM (rb_intern ("flush")), Qtrue);
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return RTEST (rv);
}

static int
plugin_rb_is_rotational (void *handle)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  rv = funcall2 (Qnil, rb_intern ("is_rotational"), 1, argv,
                 &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR)
    return 0;
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return RTEST (rv);
}

static int
plugin_rb_can_trim (void *handle)
{
  volatile VALUE argv[1];
  volatile VALUE rv;
  enum exception_class exception_happened;

  argv[0] = (VALUE) handle;
  rv = funcall2 (Qnil, rb_intern ("can_trim"), 1, argv, &exception_happened);
  if (exception_happened == EXCEPTION_NO_METHOD_ERROR)
    /* Fall back to checking if the trim method exists. */
    rv = rb_funcall (Qnil, rb_intern ("respond_to?"),
                     2, ID2SYM (rb_intern ("trim")), Qtrue);
  else if (exception_happened == EXCEPTION_OTHER)
    return -1;

  return RTEST (rv);
}

#define plugin_rb_config_help \
  "script=<FILENAME>     (required) The Ruby plugin to run.\n" \
  "[other arguments may be used by the plugin that you load]"

/* Ruby is inherently unsafe to call in parallel from multiple
 * threads.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "ruby",
  .version           = PACKAGE_VERSION,

  .load              = plugin_rb_load,
  .unload            = plugin_rb_unload,
  .dump_plugin       = plugin_rb_dump_plugin,

  .config            = plugin_rb_config,
  .config_complete   = plugin_rb_config_complete,
  .config_help       = plugin_rb_config_help,

  .open              = plugin_rb_open,
  .close             = plugin_rb_close,

  .get_size          = plugin_rb_get_size,
  .can_write         = plugin_rb_can_write,
  .can_flush         = plugin_rb_can_flush,
  .is_rotational     = plugin_rb_is_rotational,
  .can_trim          = plugin_rb_can_trim,

  .pread             = plugin_rb_pread,
  .pwrite            = plugin_rb_pwrite,
  .flush             = plugin_rb_flush,
  .trim              = plugin_rb_trim,
  .zero              = plugin_rb_zero,
};

NBDKIT_REGISTER_PLUGIN(plugin)
