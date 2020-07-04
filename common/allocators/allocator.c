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
#include <stdbool.h>
#include <string.h>

#include <nbdkit-plugin.h>

#include "allocator.h"
#include "allocator-internal.h"

static void
free_key_value (struct key_value kv)
{
  free (kv.key);
  free (kv.value);
}

static void
free_parameters (parameters *params)
{
  parameters_iter (params, free_key_value);
  free (params->ptr);
}

/* The type may be followed by parameters "type,key=value[,...]" */
static int
parse_parameters (const char *type, size_t *type_len, parameters *params)
{
  size_t i, j, len;

  *type_len = strcspn (type, ",");

  nbdkit_debug ("allocator: %*s", (int) *type_len, type);

  /* Split the parameters. */
  for (i = *type_len; type[i] == ',';) {
    struct key_value kv;

    i++;
    len = strcspn (&type[i], ",");
    if (len == 0) {
      i++;
      continue;
    }

    j = strcspn (&type[i], "=");
    if (j == 0) {
      nbdkit_error ("invalid allocator parameter");
      free_parameters (params);
      return -1;
    }
    if (j < len) {
      kv.key = strndup (&type[i], j);
      kv.value = strndup (&type[i+j+1], len-j-1);
    }
    else {
      kv.key = strndup (&type[i], len);
      kv.value = strdup ("1");
    }
    if (kv.key == NULL || kv.value == NULL) {
      nbdkit_error ("strdup: %m");
      free (kv.key);
      free (kv.value);
      free_parameters (params);
      return -1;
    }

    nbdkit_debug ("allocator parameter: %s=%s", kv.key, kv.value);
    if (parameters_append (params, kv) == -1) {
      nbdkit_error ("realloc: %m");
      free_parameters (params);
      return -1;
    }
    i += len;
  }

  return 0;
}

struct allocator *
create_allocator (const char *type, bool debug)
{
  struct allocator *ret = NULL;
  parameters params = empty_vector;
  size_t type_len;

  if (parse_parameters (type, &type_len, &params) == -1)
    return NULL;

  if (strncmp (type, "sparse", type_len) == 0) {
    ret = create_sparse_array (&params);
    if (ret) ret->type = "sparse";
  }

  else if (strncmp (type, "malloc", type_len) == 0) {
    ret = create_malloc (&params);
    if (ret) ret->type = "malloc";
  }

  else if (strncmp (type, "zstd", type_len) == 0) {
#ifdef HAVE_LIBZSTD
    ret = create_zstd_array (&params);
    if (ret) ret->type = "zstd";
#else
    nbdkit_error ("allocator=zstd is not supported in this build of nbdkit");
#endif
  }

  else
    nbdkit_error ("unknown allocator \"%s\"", type);

  /* Free the parameters allocated above. */
  free_parameters (&params);

  if (ret) ret->debug = debug;
  return ret;
}

void
cleanup_free_allocator (struct allocator **ap)
{
  struct allocator *a = *ap;

  if (a)
    a->free (a);
}
