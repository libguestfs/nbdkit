/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <nbdkit-plugin.h>

/* Backwards compatibility function for Lua 5.1.
 *
 * This is taken from https://github.com/keplerproject/lua-compat-5.3
 * where it is distributed under a compatible license to nbdkit.
 */
#ifndef HAVE_LUA_ISINTEGER
static int
lua_isinteger (lua_State *L, int index)
{
  if (lua_type (L, index) == LUA_TNUMBER) {
    lua_Number n = lua_tonumber (L, index);
    lua_Integer i = lua_tointeger (L, index);

    if (i == n)
      return 1;
  }
  return 0;
}
#endif /* HAVE_LUA_ISINTEGER */

static lua_State *L;
static const char *script;

static void
lua_plugin_load (void)
{
  L = luaL_newstate ();
  if (L == NULL) {
    nbdkit_error ("could not create Lua interpreter: %m");
    exit (EXIT_FAILURE);
  }
  luaL_openlibs (L);
}

static void
lua_plugin_unload (void)
{
  if (L)
    lua_close (L);
}

/* Test if a function was defined by the Lua code. */
static int
function_defined (const char *name)
{
  int r;

  lua_getglobal (L, name);
  r = lua_isfunction (L, -1) == 1;
  lua_pop (L, 1);
  return r;
}

static void
lua_plugin_dump_plugin (void)
{
#ifdef LUA_VERSION_MAJOR
  printf ("lua_version=%s", LUA_VERSION_MAJOR);
#ifdef LUA_VERSION_MINOR
  printf (".%s", LUA_VERSION_MINOR);
#ifdef LUA_VERSION_RELEASE
  printf (".%s", LUA_VERSION_RELEASE);
#endif
#endif
  printf ("\n");
#endif

  if (script && function_defined ("dump_plugin")) {
    lua_getglobal (L, "dump_plugin");
    if (lua_pcall (L, 0, 0, 0) != 0) {
      nbdkit_error ("dump_plugin: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
    }
  }
}

static int
lua_plugin_config (const char *key, const char *value)
{
  if (!script) {
    /* The first parameter MUST be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be script=/path/to/script.lua");
      return -1;
    }
    script = value;

    assert (L);

    /* Load the Lua file. */
    if (luaL_loadfile (L, script) != 0) {
      /* We don't need to print the script name because it's
       * contained in the error message (as well as the line number).
       */
      nbdkit_error ("could not parse Lua script %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    if (lua_pcall (L, 0, 0, 0) != 0) {
      nbdkit_error ("could not run Lua script: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }

    /* Minimal set of callbacks which are required (by nbdkit itself). */
    if (!function_defined ("open") ||
        !function_defined ("get_size") ||
        !function_defined ("pread")) {
      nbdkit_error ("%s: one of the required callbacks "
                    "'open', 'get_size' or 'pread' "
                    "is not defined by this Lua script.  "
                    "nbdkit requires these callbacks.", script);
      return -1;
    }
  }
  else if (function_defined ("config")) {
    lua_getglobal (L, "config");
    lua_pushstring (L, key);
    lua_pushstring (L, value);
    if (lua_pcall (L, 2, 0, 0) != 0) {
      nbdkit_error ("config: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
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
lua_plugin_config_complete (void)
{
  if (function_defined ("config_complete")) {
    lua_getglobal (L, "config_complete");
    if (lua_pcall (L, 0, 0, 0) != 0) {
      nbdkit_error ("config_complete: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
  }

  return 0;
}

static void *
lua_plugin_open (int readonly)
{
  int *h;

  /* We store a Lua reference (an integer) in the handle. */
  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  lua_getglobal (L, "open");
  lua_pushboolean (L, readonly);
  if (lua_pcall (L, 1, 1, 0) != 0) {
    nbdkit_error ("open: %s", lua_tostring (L, -1));
    lua_pop (L, 1);
    free (h);
    return NULL;
  }

  /* Create a reference to the Lua handle returned by open(). */
  *h = luaL_ref (L, LUA_REGISTRYINDEX);

  return h;
}

static void
lua_plugin_close (void *handle)
{
  int *h = handle;

  if (function_defined ("close")) {
    lua_getglobal (L, "close");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 0, 0) != 0) {
      nbdkit_error ("close: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
    }
  }

  /* Ensure that the Lua handle is freed. */
  luaL_unref (L, LUA_REGISTRYINDEX, *h);
  /* Free C handle. */
  free (handle);
}

static int64_t
lua_plugin_get_size (void *handle)
{
  int *h = handle;
  int64_t r;

  lua_getglobal (L, "get_size");
  lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
  if (lua_pcall (L, 1, 1, 0) != 0) {
    nbdkit_error ("get_size: %s", lua_tostring (L, -1));
    lua_pop (L, 1);
    return -1;
  }
  if (lua_isinteger (L, -1))
    r = lua_tointeger (L, -1);
  else if (lua_isnumber (L, -1))
    r = (int64_t) lua_tonumber (L, -1);
  else {
    nbdkit_error ("get_size: cannot convert returned value to an integer");
    r = -1;
  }
  lua_pop (L, 1);
  return r;
}

static int
lua_plugin_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  int *h = handle;
  size_t len;
  const char *str;

  lua_getglobal (L, "pread");
  lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
  lua_pushinteger (L, count);
  lua_pushinteger (L, offset);
  if (lua_pcall (L, 3, 1, 0) != 0) {
    nbdkit_error ("pread: %s", lua_tostring (L, -1));
    lua_pop (L, 1);
    return -1;
  }
  str = lua_tolstring (L, -1, &len);
  if (str == NULL) {
    nbdkit_error ("pread: return value is not a string");
    lua_pop (L, 1);
    return -1;
  }
  if (len < count) {
    nbdkit_error ("pread: returned string length < count bytes");
    lua_pop (L, 1);
    return -1;
  }
  memcpy (buf, str, count);
  lua_pop (L, 1);
  return 0;
}

static int
lua_plugin_pwrite (void *handle, const void *buf,
                   uint32_t count, uint64_t offset)
{
  int *h = handle;

  if (function_defined ("pwrite")) {
    lua_getglobal (L, "pwrite");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    lua_pushlstring (L, buf, count);
    lua_pushinteger (L, offset);
    if (lua_pcall (L, 3, 0, 0) != 0) {
      nbdkit_error ("pwrite: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
  }

  nbdkit_error ("pwrite not implemented");
  return -1;
}

static int
lua_plugin_can_write (void *handle)
{
  int *h = handle;
  int r;

  if (function_defined ("can_write")) {
    lua_getglobal (L, "can_write");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 1, 0) != 0) {
      nbdkit_error ("can_write: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    if (!lua_isboolean (L, -1)) {
      nbdkit_error ("can_write: return value is not a boolean");
      lua_pop (L, 1);
      return -1;
    }
    r = lua_toboolean (L, -1);
    lua_pop (L, 1);
    return r;
  }
  /* No can_write callback, but there's a pwrite callback defined, so
   * return 1.  (In C modules, nbdkit would do this).
   */
  else if (function_defined ("pwrite"))
    return 1;
  else
    return 0;
}

static int
lua_plugin_can_flush (void *handle)
{
  int *h = handle;
  int r;

  if (function_defined ("can_flush")) {
    lua_getglobal (L, "can_flush");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 1, 0) != 0) {
      nbdkit_error ("can_flush: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    if (!lua_isboolean (L, -1)) {
      nbdkit_error ("can_flush: return value is not a boolean");
      lua_pop (L, 1);
      return -1;
    }
    r = lua_toboolean (L, -1);
    lua_pop (L, 1);
    return r;
  }
  /* No can_flush callback, but there's a plugin_flush callback
   * defined, so return 1.  (In C modules, nbdkit would do this).
   */
  else if (function_defined ("plugin_flush"))
    return 1;
  else
    return 0;
}

static int
lua_plugin_can_trim (void *handle)
{
  int *h = handle;
  int r;

  if (function_defined ("can_trim")) {
    lua_getglobal (L, "can_trim");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 1, 0) != 0) {
      nbdkit_error ("can_trim: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    if (!lua_isboolean (L, -1)) {
      nbdkit_error ("can_trim: return value is not a boolean");
      lua_pop (L, 1);
      return -1;
    }
    r = lua_toboolean (L, -1);
    lua_pop (L, 1);
    return r;
  }
  /* No can_trim callback, but there's a trim callback defined, so
   * return 1.  (In C modules, nbdkit would do this).
   */
  else if (function_defined ("trim"))
    return 1;
  else
    return 0;
}

static int
lua_plugin_is_rotational (void *handle)
{
  int *h = handle;
  int r;

  if (function_defined ("is_rotational")) {
    lua_getglobal (L, "is_rotational");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 1, 0) != 0) {
      nbdkit_error ("is_rotational: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    if (!lua_isboolean (L, -1)) {
      nbdkit_error ("is_rotational: return value is not a boolean");
      lua_pop (L, 1);
      return -1;
    }
    r = lua_toboolean (L, -1);
    lua_pop (L, 1);
    return r;
  }
  else
    return 0;
}

static int
lua_plugin_flush (void *handle)
{
  int *h = handle;

  if (function_defined ("flush")) {
    lua_getglobal (L, "flush");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    if (lua_pcall (L, 1, 0, 0) != 0) {
      nbdkit_error ("flush: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
  }

  /* Ignore lack of flush callback, although probably nbdkit will
   * never call this since .can_flush returns false.
   */
  return 0;
}

static int
lua_plugin_trim (void *handle, uint32_t count, uint64_t offset)
{
  int *h = handle;

  if (function_defined ("trim")) {
    lua_getglobal (L, "trim");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    lua_pushinteger (L, count);
    lua_pushinteger (L, offset);
    if (lua_pcall (L, 3, 0, 0) != 0) {
      nbdkit_error ("trim: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
  }

  /* Ignore lack of trim callback, although probably nbdkit will never
   * call this since .can_trim returns false.
   */
  return 0;
}

static int
lua_plugin_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  int *h = handle;

  if (function_defined ("zero")) {
    lua_getglobal (L, "zero");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *h);
    lua_pushinteger (L, count);
    lua_pushinteger (L, offset);
    lua_pushboolean (L, may_trim);
    if (lua_pcall (L, 4, 0, 0) != 0) {
      nbdkit_error ("zero: %s", lua_tostring (L, -1));
      lua_pop (L, 1);
      return -1;
    }
    return 0;
  }

  nbdkit_debug ("zero falling back to pwrite");
  nbdkit_set_error (EOPNOTSUPP);
  return -1;
}

#define lua_plugin_config_help \
  "script=<FILENAME>     (required) The Lua script to run.\n" \
  "[other arguments may be used by the plugin that you load]"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "lua",
  .version           = PACKAGE_VERSION,

  .load              = lua_plugin_load,
  .unload            = lua_plugin_unload,
  .dump_plugin       = lua_plugin_dump_plugin,

  .config            = lua_plugin_config,
  .config_complete   = lua_plugin_config_complete,
  .config_help       = lua_plugin_config_help,

  .open              = lua_plugin_open,
  .close             = lua_plugin_close,

  .get_size          = lua_plugin_get_size,
  .can_write         = lua_plugin_can_write,
  .can_flush         = lua_plugin_can_flush,
  .is_rotational     = lua_plugin_is_rotational,
  .can_trim          = lua_plugin_can_trim,

  .pread             = lua_plugin_pread,
  .pwrite            = lua_plugin_pwrite,
  .flush             = lua_plugin_flush,
  .trim              = lua_plugin_trim,
  .zero              = lua_plugin_zero,
};

NBDKIT_REGISTER_PLUGIN(plugin)
