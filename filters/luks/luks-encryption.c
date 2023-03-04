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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <gnutls/crypto.h>

#include <nbdkit-filter.h>

#include "luks-encryption.h"

#include "byte-swapping.h"
#include "cleanup.h"
#include "isaligned.h"
#include "rounding.h"

/* LUKSv1 constants. */
#define LUKS_MAGIC { 'L', 'U', 'K', 'S', 0xBA, 0xBE }
#define LUKS_MAGIC_LEN 6
#define LUKS_DIGESTSIZE 20
#define LUKS_SALTSIZE 32
#define LUKS_NUMKEYS 8
#define LUKS_KEY_DISABLED 0x0000DEAD
#define LUKS_KEY_ENABLED  0x00AC71F3
#define LUKS_STRIPES 4000
#define LUKS_ALIGN_KEYSLOTS 4096

/* Key slot. */
struct luks_keyslot {
  uint32_t active;              /* LUKS_KEY_DISABLED|LUKS_KEY_ENABLED */
  uint32_t password_iterations;
  char password_salt[LUKS_SALTSIZE];
  uint32_t key_material_offset;
  uint32_t stripes;
} __attribute__ ((__packed__));

/* LUKS superblock. */
struct luks_phdr {
  char magic[LUKS_MAGIC_LEN];   /* LUKS_MAGIC */
  uint16_t version;             /* Only 1 is supported. */
  char cipher_name[32];
  char cipher_mode[32];
  char hash_spec[32];
  uint32_t payload_offset;
  uint32_t master_key_len;
  uint8_t master_key_digest[LUKS_DIGESTSIZE];
  uint8_t master_key_salt[LUKS_SALTSIZE];
  uint32_t master_key_digest_iterations;
  uint8_t uuid[40];

  struct luks_keyslot keyslot[LUKS_NUMKEYS]; /* Key slots. */
} __attribute__ ((__packed__));

/* Block cipher mode of operation.
 * https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation
 */
enum cipher_mode {
  CIPHER_MODE_ECB, CIPHER_MODE_CBC, CIPHER_MODE_XTS, CIPHER_MODE_CTR,
};

static enum cipher_mode
lookup_cipher_mode (const char *str)
{
  if (strcmp (str, "ecb") == 0)
    return CIPHER_MODE_ECB;
  if (strcmp (str, "cbc") == 0)
    return CIPHER_MODE_CBC;
  if (strcmp (str, "xts") == 0)
    return CIPHER_MODE_XTS;
  if (strcmp (str, "ctr") == 0)
    return CIPHER_MODE_CTR;
  nbdkit_error ("unknown cipher mode: %s "
                "(expecting \"ecb\", \"cbc\", \"xts\" or \"ctr\")", str);
  return -1;
}

static const char *
cipher_mode_to_string (enum cipher_mode v)
{
  switch (v) {
  case CIPHER_MODE_ECB: return "ecb";
  case CIPHER_MODE_CBC: return "cbc";
  case CIPHER_MODE_XTS: return "xts";
  case CIPHER_MODE_CTR: return "ctr";
  default: abort ();
  }
}

/* Methods used by LUKS to generate initial vectors.
 *
 * ESSIV is a bit more complicated to implement.  It is supported by
 * qemu but not by us.
 */
enum ivgen {
  IVGEN_PLAIN, IVGEN_PLAIN64, /* IVGEN_ESSIV, */
};

static enum ivgen
lookup_ivgen (const char *str)
{
  if (strcmp (str, "plain") == 0)
    return IVGEN_PLAIN;
  if (strcmp (str, "plain64") == 0)
    return IVGEN_PLAIN64;
/*
  if (strcmp (str, "essiv") == 0)
    return IVGEN_ESSIV;
*/
  nbdkit_error ("unknown IV generation algorithm: %s "
                "(expecting \"plain\", \"plain64\" etc)", str);
  return -1;
}

static const char *
ivgen_to_string (enum ivgen v)
{
  switch (v) {
  case IVGEN_PLAIN: return "plain";
  case IVGEN_PLAIN64: return "plain64";
  /*case IVGEN_ESSIV: return "essiv";*/
  default: abort ();
  }
}

static void
calculate_iv (enum ivgen v, uint8_t *iv, size_t ivlen, uint64_t sector)
{
  size_t prefixlen;
  uint32_t sector32;

  switch (v) {
  case IVGEN_PLAIN:
    prefixlen = 4; /* 32 bits */
    if (prefixlen > ivlen)
      prefixlen = ivlen;
    sector32 = (uint32_t) sector; /* truncate to only lower bits */
    sector32 = htole32 (sector32);
    memcpy (iv, &sector32, prefixlen);
    memset (iv + prefixlen, 0, ivlen - prefixlen);
    break;

  case IVGEN_PLAIN64:
    prefixlen = 8; /* 64 bits */
    if (prefixlen > ivlen)
      prefixlen = ivlen;
    sector = htole64 (sector);
    memcpy (iv, &sector, prefixlen);
    memset (iv + prefixlen, 0, ivlen - prefixlen);
    break;

  /*case IVGEN_ESSIV:*/
  default: abort ();
  }
}

/* Cipher algorithm.
 *
 * qemu in theory supports many more, but with the GnuTLS backend only
 * AES is supported.  The kernel seems to only support AES for LUKSv1.
 */
enum cipher_alg {
  CIPHER_ALG_AES_128, CIPHER_ALG_AES_192, CIPHER_ALG_AES_256,
};

static enum cipher_alg
lookup_cipher_alg (const char *str, enum cipher_mode mode, int key_bytes)
{
  if (mode == CIPHER_MODE_XTS)
    key_bytes /= 2;

  if (strcmp (str, "aes") == 0) {
    if (key_bytes == 16)
      return CIPHER_ALG_AES_128;
    if (key_bytes == 24)
      return CIPHER_ALG_AES_192;
    if (key_bytes == 32)
      return CIPHER_ALG_AES_256;
  }
  nbdkit_error ("unknown cipher algorithm: %s (expecting \"aes\", etc)", str);
  return -1;
}

static const char *
cipher_alg_to_string (enum cipher_alg v)
{
  switch (v) {
  case CIPHER_ALG_AES_128: return "aes-128";
  case CIPHER_ALG_AES_192: return "aes-192";
  case CIPHER_ALG_AES_256: return "aes-256";
  default: abort ();
  }
}

#if 0
static int
cipher_alg_key_bytes (enum cipher_alg v)
{
  switch (v) {
  case CIPHER_ALG_AES_128: return 16;
  case CIPHER_ALG_AES_192: return 24;
  case CIPHER_ALG_AES_256: return 32;
  default: abort ();
  }
}
#endif

static int
cipher_alg_iv_len (enum cipher_alg v, enum cipher_mode mode)
{
  if (CIPHER_MODE_ECB)
    return 0;                   /* Don't need an IV in this mode. */

  switch (v) {
  case CIPHER_ALG_AES_128:
  case CIPHER_ALG_AES_192:
  case CIPHER_ALG_AES_256:
    return 16;
  default: abort ();
  }
}

/* Hash, eg.MD5, SHA1 etc.
 *
 * We reuse the GnuTLS digest algorithm enum here since it supports at
 * least all the ones that LUKSv1 does.
 */
static gnutls_digest_algorithm_t
lookup_hash (const char *str)
{
  if (strcmp (str, "md5") == 0)
    return GNUTLS_DIG_MD5;
  if (strcmp (str, "sha1") == 0)
    return GNUTLS_DIG_SHA1;
  if (strcmp (str, "sha224") == 0)
    return GNUTLS_DIG_SHA224;
  if (strcmp (str, "sha256") == 0)
    return GNUTLS_DIG_SHA256;
  if (strcmp (str, "sha384") == 0)
    return GNUTLS_DIG_SHA384;
  if (strcmp (str, "sha512") == 0)
    return GNUTLS_DIG_SHA512;
  if (strcmp (str, "ripemd160") == 0)
    return GNUTLS_DIG_RMD160;
  nbdkit_error ("unknown hash algorithm: %s "
                "(expecting \"md5\", \"sha1\", \"sha224\", etc)", str);
  return -1;
}

static const char *
hash_to_string (gnutls_digest_algorithm_t v)
{
  switch (v) {
  case GNUTLS_DIG_UNKNOWN: return "unknown";
  case GNUTLS_DIG_MD5: return "md5";
  case GNUTLS_DIG_SHA1: return "sha1";
  case GNUTLS_DIG_SHA224: return "sha224";
  case GNUTLS_DIG_SHA256: return "sha256";
  case GNUTLS_DIG_SHA384: return "sha384";
  case GNUTLS_DIG_SHA512: return "sha512";
  case GNUTLS_DIG_RMD160: return "ripemd160";
  default: abort ();
  }
}

#if 0
/* See qemu & dm-crypt implementations for an explanation of what's
 * going on here.
 */
enum cipher_alg
lookup_essiv_cipher (enum cipher_alg cipher_alg,
                     gnutls_digest_algorithm_t ivgen_hash_alg)
{
  int digest_bytes = gnutls_hash_get_len (ivgen_hash_alg);
  int key_bytes = cipher_alg_key_bytes (cipher_alg);

  if (digest_bytes == key_bytes)
    return cipher_alg;

  switch (cipher_alg) {
  case CIPHER_ALG_AES_128:
  case CIPHER_ALG_AES_192:
  case CIPHER_ALG_AES_256:
    if (digest_bytes == 16) return CIPHER_ALG_AES_128;
    if (digest_bytes == 24) return CIPHER_ALG_AES_192;
    if (digest_bytes == 32) return CIPHER_ALG_AES_256;
    nbdkit_error ("no %s cipher available with key size %d",
                  "AES", digest_bytes);
    return -1;
  default:
    nbdkit_error ("ESSIV does not support cipher %s",
                  cipher_alg_to_string (cipher_alg));
    return -1;
  }
}
#endif

/* Per-connection data. */
struct luks_data {
  /* LUKS header, if necessary byte-swapped into host order. */
  struct luks_phdr phdr;

  /* Decoded algorithm etc. */
  enum cipher_alg cipher_alg;
  enum cipher_mode cipher_mode;
  gnutls_digest_algorithm_t hash_alg;
  enum ivgen ivgen_alg;
  gnutls_digest_algorithm_t ivgen_hash_alg;
  enum cipher_alg ivgen_cipher_alg;

  /* GnuTLS algorithm. */
  gnutls_cipher_algorithm_t gnutls_cipher;

  /* If we managed to decrypt one of the keyslots using the passphrase
   * then this contains the master key, otherwise NULL.
   */
  uint8_t *masterkey;
};

/* Parse the header fields containing cipher algorithm, mode, etc. */
static int
parse_cipher_strings (struct luks_data *h)
{
  char cipher_name[33], cipher_mode[33], hash_spec[33];
  char *ivgen, *ivhash;

  /* Copy the header fields locally and ensure they are \0 terminated. */
  memcpy (cipher_name, h->phdr.cipher_name, 32);
  cipher_name[32] = 0;
  memcpy (cipher_mode, h->phdr.cipher_mode, 32);
  cipher_mode[32] = 0;
  memcpy (hash_spec, h->phdr.hash_spec, 32);
  hash_spec[32] = 0;

  nbdkit_debug ("LUKS v%" PRIu16 " cipher: %s mode: %s hash: %s "
                "master key: %" PRIu32 " bits",
                h->phdr.version, cipher_name, cipher_mode, hash_spec,
                h->phdr.master_key_len * 8);

  /* The cipher_mode header has the form: "ciphermode-ivgen[:ivhash]"
   * QEmu writes: "xts-plain64"
   */
  ivgen = strchr (cipher_mode, '-');
  if (!ivgen) {
    nbdkit_error ("incorrect cipher_mode header, "
                  "expecting mode-ivgenerator but got \"%s\"", cipher_mode);
    return -1;
  }
  *ivgen = '\0';
  ivgen++;

  ivhash = strchr (ivgen, ':');
  if (!ivhash)
    h->ivgen_hash_alg = GNUTLS_DIG_UNKNOWN;
  else {
    *ivhash = '\0';
    ivhash++;

    h->ivgen_hash_alg = lookup_hash (ivhash);
    if (h->ivgen_hash_alg == -1)
      return -1;
  }

  h->cipher_mode = lookup_cipher_mode (cipher_mode);
  if (h->cipher_mode == -1)
    return -1;

  h->cipher_alg = lookup_cipher_alg (cipher_name, h->cipher_mode,
                                     h->phdr.master_key_len);
  if (h->cipher_alg == -1)
    return -1;

  h->hash_alg = lookup_hash (hash_spec);
  if (h->hash_alg == -1)
    return -1;

  h->ivgen_alg = lookup_ivgen (ivgen);
  if (h->ivgen_alg == -1)
    return -1;

#if 0
  if (h->ivgen_alg == IVGEN_ESSIV) {
    if (!ivhash) {
      nbdkit_error ("incorrect IV generator hash specification");
      return -1;
    }
    h->ivgen_cipher_alg = lookup_essiv_cipher (h->cipher_alg,
                                               h->ivgen_hash_alg);
    if (h->ivgen_cipher_alg == -1)
      return -1;
  }
  else
#endif
  h->ivgen_cipher_alg = h->cipher_alg;

  nbdkit_debug ("LUKS parsed ciphers: %s %s %s %s %s %s",
                cipher_alg_to_string (h->cipher_alg),
                cipher_mode_to_string (h->cipher_mode),
                hash_to_string (h->hash_alg),
                ivgen_to_string (h->ivgen_alg),
                hash_to_string (h->ivgen_hash_alg),
                cipher_alg_to_string (h->ivgen_cipher_alg));

  /* GnuTLS combines cipher and block mode into a single value.  Not
   * all possible combinations are available in GnuTLS.  See:
   * https://www.gnutls.org/manual/html_node/Supported-ciphersuites.html
   */
  h->gnutls_cipher = GNUTLS_CIPHER_NULL;
  switch (h->cipher_mode) {
  case CIPHER_MODE_XTS:
    switch (h->cipher_alg) {
    case CIPHER_ALG_AES_128:
      h->gnutls_cipher = GNUTLS_CIPHER_AES_128_XTS;
      break;
    case CIPHER_ALG_AES_256:
      h->gnutls_cipher = GNUTLS_CIPHER_AES_256_XTS;
      break;
    default: break;
    }
    break;
  case CIPHER_MODE_CBC:
    switch (h->cipher_alg) {
    case CIPHER_ALG_AES_128:
      h->gnutls_cipher = GNUTLS_CIPHER_AES_128_CBC;
      break;
    case CIPHER_ALG_AES_192:
      h->gnutls_cipher = GNUTLS_CIPHER_AES_192_CBC;
      break;
    case CIPHER_ALG_AES_256:
      h->gnutls_cipher = GNUTLS_CIPHER_AES_256_CBC;
      break;
    default: break;
    }
  default: break;
  }
  if (h->gnutls_cipher == GNUTLS_CIPHER_NULL) {
    nbdkit_error ("cipher algorithm %s in mode %s is not supported by GnuTLS",
                  cipher_alg_to_string (h->cipher_alg),
                  cipher_mode_to_string (h->cipher_mode));
    return -1;
  }

  return 0;
}

/* Anti-Forensic merge operation. */
static void
xor (const uint8_t *in1, const uint8_t *in2, uint8_t *out, size_t len)
{
  size_t i;

  for (i = 0; i < len; ++i)
    out[i] = in1[i] ^ in2[i];
}

static int
af_hash (gnutls_digest_algorithm_t hash_alg, uint8_t *block, size_t len)
{
  size_t digest_bytes = gnutls_hash_get_len (hash_alg);
  size_t nr_blocks, last_block_len;
  size_t i;
  int r;
  gnutls_hash_hd_t hash;
  CLEANUP_FREE uint8_t *temp;

  temp = malloc (digest_bytes);
  if (!temp) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  nr_blocks = len / digest_bytes;
  last_block_len = len % digest_bytes;
  if (last_block_len != 0)
    nr_blocks++;
  else
    last_block_len = digest_bytes;

  for (i = 0; i < nr_blocks; ++i) {
    const uint32_t iv = htobe32 (i);
    const size_t blen = i < nr_blocks - 1 ? digest_bytes : last_block_len;

    /* Hash iv + i'th block into temp. */
    r = gnutls_hash_init (&hash, hash_alg);
    if (r != 0) {
      nbdkit_error ("gnutls_hash_init: %s", gnutls_strerror (r));
      return -1;
    }
    gnutls_hash (hash, &iv, sizeof iv);
    gnutls_hash (hash, &block[i*digest_bytes], blen);
    gnutls_hash_deinit (hash, temp);

    memcpy (&block[i*digest_bytes], temp, blen);
  }

  return 0;
}

static int
afmerge (gnutls_digest_algorithm_t hash_alg, uint32_t stripes,
         const uint8_t *in, uint8_t *out, size_t outlen)
{
  CLEANUP_FREE uint8_t *block = calloc (1, outlen);
  size_t i;

  /* NB: input size is stripes * master_key_len where
   * master_key_len == outlen
   */
  for (i = 0; i < stripes-1; ++i) {
    xor (&in[i*outlen], block, block, outlen);
    if (af_hash (hash_alg, block, outlen) == -1)
      return -1;
  }
  xor (&in[i*outlen], block, out, outlen);
  return 0;
}

/* Length of key material in key slot i (sectors).
 *
 * This is basically copied from qemu because the spec description is
 * unintelligible and apparently doesn't match reality.
 */
static uint64_t
key_material_length_in_sectors (struct luks_data *h, size_t i)
{
  uint64_t len, r;

  len = (uint64_t) h->phdr.master_key_len * h->phdr.keyslot[i].stripes;
  r = DIV_ROUND_UP (len, LUKS_SECTOR_SIZE);
  r = ROUND_UP (r, LUKS_ALIGN_KEYSLOTS / LUKS_SECTOR_SIZE);
  return r;
}

/* Try the passphrase in key slot i.  If this returns true then the
 * passphrase was able to decrypt the master key, and the master key
 * has been stored in h->masterkey.
 */
static int
try_passphrase_in_keyslot (nbdkit_next *next, struct luks_data *h,
                           size_t i, const char *passphrase)
{
  /* I believe this is supposed to be safe, looking at the GnuTLS
   * header file.
   */
  const gnutls_mac_algorithm_t mac = (gnutls_mac_algorithm_t) h->hash_alg;
  struct luks_keyslot *ks = &h->phdr.keyslot[i];
  size_t split_key_len;
  CLEANUP_FREE uint8_t *split_key = NULL;
  CLEANUP_FREE uint8_t *masterkey = NULL;
  const gnutls_datum_t key =
    { (unsigned char *) passphrase, strlen (passphrase) };
  const gnutls_datum_t salt =
    { (unsigned char *) ks->password_salt, LUKS_SALTSIZE };
  const gnutls_datum_t msalt =
    { (unsigned char *) h->phdr.master_key_salt, LUKS_SALTSIZE };
  gnutls_datum_t mkey;
  gnutls_cipher_hd_t cipher;
  int r, err = 0;
  uint64_t start;
  uint8_t key_digest[LUKS_DIGESTSIZE];

  if (ks->active != LUKS_KEY_ENABLED)
    return 0;

  split_key_len = h->phdr.master_key_len * ks->stripes;
  split_key = malloc (split_key_len);
  if (split_key == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }
  masterkey = malloc (h->phdr.master_key_len);
  if (masterkey == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  /* Hash the passphrase to make a possible masterkey. */
  r = gnutls_pbkdf2 (mac, &key, &salt, ks->password_iterations,
                     masterkey, h->phdr.master_key_len);
  if (r != 0) {
    nbdkit_error ("gnutls_pbkdf2: %s", gnutls_strerror (r));
    return -1;
  }

  /* Read master key material from plugin. */
  start = (uint64_t) ks->key_material_offset * LUKS_SECTOR_SIZE;
  if (next->pread (next, split_key, split_key_len, start, 0, &err) == -1) {
    errno = err;
    return -1;
  }

  /* Decrypt the (still AFsplit) master key material. */
  mkey.data = (unsigned char *) masterkey;
  mkey.size = h->phdr.master_key_len;
  r = gnutls_cipher_init (&cipher, h->gnutls_cipher, &mkey, NULL);
  if (r != 0) {
    nbdkit_error ("gnutls_cipher_init: %s", gnutls_strerror (r));
    return -1;
  }

  r = do_decrypt (h, cipher, 0, split_key, split_key_len / LUKS_SECTOR_SIZE);
  gnutls_cipher_deinit (cipher);
  if (r == -1)
    return -1;

  /* Decode AFsplit key to a possible masterkey. */
  if (afmerge (h->hash_alg, ks->stripes, split_key,
               masterkey, h->phdr.master_key_len) == -1)
    return -1;

  /* Check if the masterkey is correct by comparing hash of the
   * masterkey with LUKS header.
   */
  r = gnutls_pbkdf2 (mac, &mkey, &msalt,
                     h->phdr.master_key_digest_iterations,
                     key_digest, LUKS_DIGESTSIZE);
  if (r != 0) {
    nbdkit_error ("gnutls_pbkdf2: %s", gnutls_strerror (r));
    return -1;
  }

  if (memcmp (key_digest, h->phdr.master_key_digest, LUKS_DIGESTSIZE) == 0) {
    /* The passphrase is correct so save the master key in the handle. */
    h->masterkey = malloc (h->phdr.master_key_len);
    if (h->masterkey == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
    memcpy (h->masterkey, masterkey, h->phdr.master_key_len);
    return 1;
  }

  return 0;
}

struct luks_data *
load_header (nbdkit_next *next, const char *passphrase)
{
  static const char expected_magic[] = LUKS_MAGIC;
  struct luks_data *h;
  int64_t size;
  int err = 0, r;
  size_t i;
  struct luks_keyslot *ks;
  char uuid[41];

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  /* Check the struct size matches the documentation. */
  assert (sizeof (struct luks_phdr) == 592);

  /* Check this is a LUKSv1 disk. */
  size = next->get_size (next);
  if (size == -1) {
    free (h);
    return NULL;
  }
  if (size < 16384) {
    nbdkit_error ("disk is too small to be LUKS-encrypted");
    free (h);
    return NULL;
  }

  /* Read the phdr. */
  if (next->pread (next, &h->phdr, sizeof h->phdr, 0, 0, &err) == -1) {
    free (h);
    errno = err;
    return NULL;
  }

  if (memcmp (h->phdr.magic, expected_magic, LUKS_MAGIC_LEN) != 0) {
    nbdkit_error ("this disk does not contain a LUKS header");
    free (h);
    return NULL;
  }
  h->phdr.version = be16toh (h->phdr.version);
  if (h->phdr.version != 1) {
    nbdkit_error ("this disk contains a LUKS version %" PRIu16 " header, "
                  "but this filter only supports LUKSv1",
                  h->phdr.version);
    free (h);
    return NULL;
  }

  /* Byte-swap the rest of the header. */
  h->phdr.payload_offset = be32toh (h->phdr.payload_offset);
  h->phdr.master_key_len = be32toh (h->phdr.master_key_len);
  h->phdr.master_key_digest_iterations =
    be32toh (h->phdr.master_key_digest_iterations);

  for (i = 0; i < LUKS_NUMKEYS; ++i) {
    ks = &h->phdr.keyslot[i];
    ks->active = be32toh (ks->active);
    ks->password_iterations = be32toh (ks->password_iterations);
    ks->key_material_offset = be32toh (ks->key_material_offset);
    ks->stripes = be32toh (ks->stripes);
  }

  /* Sanity check some fields. */
  if (h->phdr.payload_offset >= size / LUKS_SECTOR_SIZE) {
    nbdkit_error ("bad LUKSv1 header: payload offset points beyond "
                  "the end of the disk");
    free (h);
    return NULL;
  }

  if (h->phdr.master_key_digest_iterations == 0) {
    nbdkit_error ("bad LUKSv1 header: master key iterations is 0");
    free (h);
    return NULL;
  }

  /* We derive several allocations from master_key_len so make sure
   * it's not insane.
   */
  if (h->phdr.master_key_len > 1024) {
    nbdkit_error ("bad LUKSv1 header: master key is too long");
    free (h);
    return NULL;
  }

  for (i = 0; i < LUKS_NUMKEYS; ++i) {
    uint64_t start, len;

    ks = &h->phdr.keyslot[i];
    switch (ks->active) {
    case LUKS_KEY_ENABLED:
      if (!ks->stripes) {
        nbdkit_error ("bad LUKSv1 header: key slot %zu is corrupted", i);
        free (h);
        return NULL;
      }
      if (ks->stripes >= 10000) {
        nbdkit_error ("bad LUKSv1 header: key slot %zu stripes too large", i);
        free (h);
        return NULL;
      }
      if (ks->password_iterations == 0) {
        nbdkit_error ("bad LUKSv1 header: key slot %zu iterations is 0", i);
        free (h);
        return NULL;
      }
      start = ks->key_material_offset;
      len = key_material_length_in_sectors (h, i);
      if (len > 4096) /* bound it at something reasonable */ {
        nbdkit_error ("bad LUKSv1 header: key slot %zu key material length "
                      "is too large", i);
        free (h);
        return NULL;
      }
      if (start * LUKS_SECTOR_SIZE >= size ||
          (start + len) * LUKS_SECTOR_SIZE >= size) {
        nbdkit_error ("bad LUKSv1 header: key slot %zu key material offset "
                      "points beyond the end of the disk", i);
        free (h);
        return NULL;
      }
      /*FALLTHROUGH*/
    case LUKS_KEY_DISABLED:
      break;

    default:
      nbdkit_error ("bad LUKSv1 header: key slot %zu has "
                    "an invalid active flag", i);
      return NULL;
    }
  }

  /* Decode the ciphers. */
  if (parse_cipher_strings (h) == -1) {
    free (h);
    return NULL;
  }

  /* Dump some information about the header. */
  memcpy (uuid, h->phdr.uuid, 40);
  uuid[40] = 0;
  nbdkit_debug ("LUKS UUID: %s", uuid);

  for (i = 0; i < LUKS_NUMKEYS; ++i) {
    uint64_t start, len;

    ks = &h->phdr.keyslot[i];
    if (ks->active == LUKS_KEY_ENABLED) {
      start = ks->key_material_offset;
      len = key_material_length_in_sectors (h, i);
      nbdkit_debug ("LUKS key slot %zu: key material in sectors %" PRIu64
                    "..%" PRIu64,
                    i, start, start+len-1);
    }
  }

  /* Now try to unlock the master key. */
  for (i = 0; i < LUKS_NUMKEYS; ++i) {
    r = try_passphrase_in_keyslot (next, h, i, passphrase);
    if (r == -1) {
      free (h);
      return NULL;
    }
    if (r > 0)
      goto unlocked;
  }
  nbdkit_error ("LUKS passphrase is not correct, "
                "no key slot could be unlocked");
  free (h);
  return NULL;

 unlocked:
  assert (h->masterkey != NULL);
  nbdkit_debug ("LUKS unlocked block device with passphrase");

  return h;
}

/* Free fields in the handle that might have been set by load_header. */
void
free_luks_data (struct luks_data *h)
{
  if (h) {
    if (h->masterkey) {
      memset (h->masterkey, 0, h->phdr.master_key_len);
      free (h->masterkey);
    }
    free (h);
  }
}

uint64_t
get_payload_offset (struct luks_data *h)
{
  return h->phdr.payload_offset;
}

gnutls_cipher_hd_t
create_cipher (struct luks_data *h)
{
  gnutls_datum_t mkey;
  gnutls_cipher_hd_t cipher;
  int r;

  assert (h->masterkey != NULL);

  mkey.data = (unsigned char *) h->masterkey;
  mkey.size = h->phdr.master_key_len;
  r = gnutls_cipher_init (&cipher, h->gnutls_cipher, &mkey, NULL);
  if (r != 0) {
    nbdkit_error ("gnutls_cipher_init: %s", gnutls_strerror (r));
    return NULL;
  }
  return cipher;
}

/* Perform decryption of a block of data in memory. */
int
do_decrypt (struct luks_data *h, gnutls_cipher_hd_t cipher,
            uint64_t sector, uint8_t *buf, size_t nr_sectors)
{
  int r;
  const size_t ivlen = cipher_alg_iv_len (h->cipher_alg, h->cipher_mode);
  CLEANUP_FREE uint8_t *iv;

  iv = malloc (ivlen);
  if (!iv) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (nr_sectors) {
    calculate_iv (h->ivgen_alg, iv, ivlen, sector);
    gnutls_cipher_set_iv (cipher, iv, ivlen);
    r = gnutls_cipher_decrypt2 (cipher,
                                buf, LUKS_SECTOR_SIZE, /* ciphertext */
                                buf, LUKS_SECTOR_SIZE  /* plaintext */);
    if (r != 0) {
      nbdkit_error ("gnutls_cipher_decrypt2: %s", gnutls_strerror (r));
      return -1;
    }

    buf += LUKS_SECTOR_SIZE;
    nr_sectors--;
    sector++;
  }

  return 0;
}

/* Perform encryption of a block of data in memory. */
int
do_encrypt (struct luks_data *h, gnutls_cipher_hd_t cipher,
            uint64_t sector, uint8_t *buf, size_t nr_sectors)
{
  int r;
  const size_t ivlen = cipher_alg_iv_len (h->cipher_alg, h->cipher_mode);
  CLEANUP_FREE uint8_t *iv;

  iv = malloc (ivlen);
  if (!iv) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (nr_sectors) {
    calculate_iv (h->ivgen_alg, iv, ivlen, sector);
    gnutls_cipher_set_iv (cipher, iv, ivlen);
    r = gnutls_cipher_encrypt2 (cipher,
                                buf, LUKS_SECTOR_SIZE, /* plaintext */
                                buf, LUKS_SECTOR_SIZE  /* ciphertext */);
    if (r != 0) {
      nbdkit_error ("gnutls_cipher_decrypt2: %s", gnutls_strerror (r));
      return -1;
    }

    buf += LUKS_SECTOR_SIZE;
    nr_sectors--;
    sector++;
  }

  return 0;
}
