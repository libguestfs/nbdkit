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
#include <stdbool.h>
#include <string.h>

#include <nbdkit-plugin.h>

#include "allocator.h"
#include "allocator-internal.h"
#include "strndup.h"
#include "vector.h"

/* The list of registered allocators. */
DEFINE_VECTOR_TYPE (allocator_list, const struct allocator_functions *);
static allocator_list allocators = empty_vector;

void
register_allocator (const struct allocator_functions *f)
{
  if (allocator_list_append (&allocators, f) == -1) {
    perror ("realloc");
    exit (EXIT_FAILURE);
  }
}

static void
free_key_value (struct key_value kv)
{
  free (kv.key);
  free (kv.value);
}

static void
free_allocator_parameters (allocator_parameters *params)
{
  allocator_parameters_iter (params, free_key_value);
  free (params->ptr);
}

/* The type may be followed by parameters "type,key=value[,...]" */
static int
parse_parameters (const char *type, size_t *type_len,
                  allocator_parameters *params)
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
      free_allocator_parameters (params);
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
      free_allocator_parameters (params);
      return -1;
    }

    nbdkit_debug ("allocator parameter: %s=%s", kv.key, kv.value);
    if (allocator_parameters_append (params, kv) == -1) {
      nbdkit_error ("realloc: %m");
      free_allocator_parameters (params);
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
  allocator_parameters params = empty_vector;
  size_t i, type_len;

  if (parse_parameters (type, &type_len, &params) == -1)
    return NULL;

  /* See if we can find the allocator. */
  for (i = 0; i < allocators.len; ++i) {
    if (strncmp (type, allocators.ptr[i]->type, type_len) == 0) {
      ret = allocators.ptr[i]->create (&params);
      break;
    }
  }

  if (ret == NULL)
    nbdkit_error ("unknown allocator \"%s\"", type);

  /* Free the parameters allocated above. */
  free_allocator_parameters (&params);

  if (ret) {
    ret->debug = debug;
    ret->f = allocators.ptr[i];
  }
  return ret;
}

void
cleanup_free_allocator (struct allocator **ap)
{
  struct allocator *a = *ap;

  if (a && a->f)
    a->f->free (a);
}
