/* nbdkit
 * Copyright (C) 2018-2022 Red Hat Inc.
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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <pthread.h>

#include <gnutls/crypto.h>

#include <nbdkit-filter.h>

#include "luks-encryption.h"

#include "cleanup.h"
#include "isaligned.h"
#include "minmax.h"

static char *passphrase = NULL;

static void
luks_unload (void)
{
  /* XXX We should really store the passphrase (and master key)
   * in mlock-ed memory.
   */
  if (passphrase) {
    memset (passphrase, 0, strlen (passphrase));
    free (passphrase);
  }
}

static int
luks_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static int
luks_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
             const char *key, const char *value)
{
  if (strcmp (key, "passphrase") == 0) {
    if (nbdkit_read_password (value, &passphrase) == -1)
      return -1;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
luks_config_complete (nbdkit_next_config_complete *next, nbdkit_backend *nxdata)
{
  if (passphrase == NULL) {
    nbdkit_error ("LUKS \"passphrase\" parameter is missing");
    return -1;
  }
  return next (nxdata);
}

#define luks_config_help \
  "passphrase=<SECRET>      Secret passphrase."

/* Per-connection handle. */
struct handle {
  struct luks_data *h;
};

static void *
luks_open (nbdkit_next_open *next, nbdkit_context *nxdata,
           int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  return h;
}

static void
luks_close (void *handle)
{
  struct handle *h = handle;

  free_luks_data (h->h);
  free (h);
}

static int
luks_prepare (nbdkit_next *next, void *handle, int readonly)
{
  struct handle *h = handle;

  /* Check we haven't been called before, this should never happen. */
  assert (h->h == NULL);

  h->h = load_header (next, passphrase);
  if (h->h == NULL)
    return -1;

  return 0;
}

static int64_t
luks_get_size (nbdkit_next *next, void *handle)
{
  struct handle *h = handle;
  int64_t size;

  /* Check that prepare has been called already. */
  assert (h->h != NULL);

  const uint64_t payload_offset = get_payload_offset (h->h) * LUKS_SECTOR_SIZE;

  size = next->get_size (next);
  if (size == -1)
    return -1;

  if (size < payload_offset) {
    nbdkit_error ("disk too small, or contains an incomplete LUKS partition");
    return -1;
  }

  return size - payload_offset;
}

/* Whatever the plugin says, several operations are not supported by
 * this filter:
 *
 * - extents
 * - trim
 * - zero
 */
static int
luks_can_extents (nbdkit_next *next, void *handle)
{
  return 0;
}

static int
luks_can_trim (nbdkit_next *next, void *handle)
{
  return 0;
}

static int
luks_can_zero (nbdkit_next *next, void *handle)
{
  return NBDKIT_ZERO_EMULATE;
}

static int
luks_can_fast_zero (nbdkit_next *next, void *handle)
{
  return 0;
}

/* Rely on nbdkit to call .pread to emulate .cache calls.  We will
 * respond by decrypting the block which could be stored by the cache
 * filter or similar on top.
 */
static int
luks_can_cache (nbdkit_next *next, void *handle)
{
  return NBDKIT_CACHE_EMULATE;
}

/* Advertise minimum/preferred sector-sized blocks, although we can in
 * fact handle any read or write.
 */
static int
luks_block_size (nbdkit_next *next, void *handle,
                 uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  if (next->block_size (next, minimum, preferred, maximum) == -1)
    return -1;

  if (*minimum == 0) {         /* No constraints set by the plugin. */
    *minimum = LUKS_SECTOR_SIZE;
    *preferred = LUKS_SECTOR_SIZE;
    *maximum = 0xffffffff;
  }
  else {
    *minimum = MAX (*minimum, LUKS_SECTOR_SIZE);
    *preferred = MAX (*minimum, MAX (*preferred, LUKS_SECTOR_SIZE));
  }
  return 0;
}

/* Decrypt data. */
static int
luks_pread (nbdkit_next *next, void *handle,
            void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  struct handle *h = handle;
  const uint64_t payload_offset = get_payload_offset (h->h) * LUKS_SECTOR_SIZE;
  CLEANUP_FREE uint8_t *sector = NULL;
  uint64_t sectnum, sectoffs;
  gnutls_cipher_hd_t cipher;

  if (!h->h) {
    *err = EIO;
    return -1;
  }

  if (!IS_ALIGNED (count | offset, LUKS_SECTOR_SIZE)) {
    sector = malloc (LUKS_SECTOR_SIZE);
    if (sector == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  sectnum = offset / LUKS_SECTOR_SIZE;  /* sector number */
  sectoffs = offset % LUKS_SECTOR_SIZE; /* offset within the sector */

  cipher = create_cipher (h->h);
  if (!cipher)
    return -1;

  /* Unaligned head */
  if (sectoffs) {
    uint64_t n = MIN (LUKS_SECTOR_SIZE - sectoffs, count);

    assert (sector);
    if (next->pread (next, sector, LUKS_SECTOR_SIZE,
                     sectnum * LUKS_SECTOR_SIZE + payload_offset,
                     flags, err) == -1)
      goto err;

    if (do_decrypt (h->h, cipher, sectnum, sector, 1) == -1)
      goto err;

    memcpy (buf, &sector[sectoffs], n);

    buf += n;
    count -= n;
    sectnum++;
  }

  /* Aligned body */
  while (count >= LUKS_SECTOR_SIZE) {
    if (next->pread (next, buf, LUKS_SECTOR_SIZE,
                     sectnum * LUKS_SECTOR_SIZE + payload_offset,
                     flags, err) == -1)
      goto err;

    if (do_decrypt (h->h, cipher, sectnum, buf, 1) == -1)
      goto err;

    buf += LUKS_SECTOR_SIZE;
    count -= LUKS_SECTOR_SIZE;
    sectnum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (sector);
    if (next->pread (next, sector, LUKS_SECTOR_SIZE,
                     sectnum * LUKS_SECTOR_SIZE + payload_offset,
                     flags, err) == -1)
      goto err;

    if (do_decrypt (h->h, cipher, sectnum, sector, 1) == -1)
      goto err;

    memcpy (buf, sector, count);
  }

  gnutls_cipher_deinit (cipher);
  return 0;

 err:
  gnutls_cipher_deinit (cipher);
  goto err;
}

/* Lock preventing read-modify-write cycles from overlapping. */
static pthread_mutex_t read_modify_write_lock = PTHREAD_MUTEX_INITIALIZER;

/* Encrypt data. */
static int
luks_pwrite (nbdkit_next *next, void *handle,
             const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  struct handle *h = handle;
  const uint64_t payload_offset = get_payload_offset (h->h) * LUKS_SECTOR_SIZE;
  CLEANUP_FREE uint8_t *sector = NULL;
  uint64_t sectnum, sectoffs;
  gnutls_cipher_hd_t cipher;

  if (!h->h) {
    *err = EIO;
    return -1;
  }

  sector = malloc (LUKS_SECTOR_SIZE);
  if (sector == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  sectnum = offset / LUKS_SECTOR_SIZE;  /* sector number */
  sectoffs = offset % LUKS_SECTOR_SIZE; /* offset within the sector */

  cipher = create_cipher (h->h);
  if (!cipher)
    return -1;

  /* Unaligned head */
  if (sectoffs) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&read_modify_write_lock);

    uint64_t n = MIN (LUKS_SECTOR_SIZE - sectoffs, count);

    if (next->pread (next, sector, LUKS_SECTOR_SIZE,
                     sectnum * LUKS_SECTOR_SIZE + payload_offset,
                     flags, err) == -1)
      goto err;

    memcpy (&sector[sectoffs], buf, n);

    if (do_encrypt (h->h, cipher, sectnum, sector, 1) == -1)
      goto err;

    if (next->pwrite (next, sector, LUKS_SECTOR_SIZE,
                      sectnum * LUKS_SECTOR_SIZE + payload_offset,
                      flags, err) == -1)
      goto err;

    buf += n;
    count -= n;
    sectnum++;
  }

  /* Aligned body */
  while (count >= LUKS_SECTOR_SIZE) {
    memcpy (sector, buf, LUKS_SECTOR_SIZE);

    if (do_encrypt (h->h, cipher, sectnum, sector, 1) == -1)
      goto err;

    if (next->pwrite (next, sector, LUKS_SECTOR_SIZE,
                      sectnum * LUKS_SECTOR_SIZE + payload_offset,
                      flags, err) == -1)
      goto err;

    buf += LUKS_SECTOR_SIZE;
    count -= LUKS_SECTOR_SIZE;
    sectnum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&read_modify_write_lock);

    if (next->pread (next, sector, LUKS_SECTOR_SIZE,
                     sectnum * LUKS_SECTOR_SIZE + payload_offset,
                     flags, err) == -1)
      goto err;

    memcpy (sector, buf, count);

    if (do_encrypt (h->h, cipher, sectnum, sector, 1) == -1)
      goto err;

    if (next->pwrite (next, sector, LUKS_SECTOR_SIZE,
                      sectnum * LUKS_SECTOR_SIZE + payload_offset,
                      flags, err) == -1)
      goto err;
  }

  gnutls_cipher_deinit (cipher);
  return 0;

 err:
  gnutls_cipher_deinit (cipher);
  return -1;
}

static struct nbdkit_filter filter = {
  .name               = "luks",
  .longname           = "nbdkit luks filter",
  .unload             = luks_unload,
  .thread_model       = luks_thread_model,
  .config             = luks_config,
  .config_complete    = luks_config_complete,
  .config_help        = luks_config_help,
  .open               = luks_open,
  .close              = luks_close,
  .prepare            = luks_prepare,
  .get_size           = luks_get_size,
  .can_extents        = luks_can_extents,
  .can_trim           = luks_can_trim,
  .can_zero           = luks_can_zero,
  .can_fast_zero      = luks_can_fast_zero,
  .can_cache          = luks_can_cache,
  .block_size         = luks_block_size,
  .pread              = luks_pread,
  .pwrite             = luks_pwrite,
};

NBDKIT_REGISTER_FILTER (filter)
