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

/* This header file defines the file format used by LUKSv1.  See also:
 * https://gitlab.com/cryptsetup/cryptsetup/-/wikis/LUKS-standard/on-disk-format.pdf
 * Note we do not yet support LUKSv2.
 */

#ifndef NBDKIT_LUKS_ENCRYPTION_H
#define NBDKIT_LUKS_ENCRYPTION_H

#include <stdint.h>
#include <gnutls/crypto.h>

#define LUKS_SECTOR_SIZE 512

/* Per-connection data. */
struct luks_data;

/* Load the LUKS header, parse the algorithms, unlock the masterkey
 * using the passphrase, initialize all the fields in the handle.
 *
 * This function may call next->pread (many times).
 */
extern struct luks_data *load_header (nbdkit_next *next,
                                      const char *passphrase);

/* Free the handle and all fields inside it. */
extern void free_luks_data (struct luks_data *h);

/* Get the offset where the encrypted data starts (in sectors). */
extern uint64_t get_payload_offset (struct luks_data *h);

/* Create an GnuTLS cipher, initialized with the master key.  Must be
 * freed by the caller using gnutls_cipher_deinit.
 */
extern gnutls_cipher_hd_t create_cipher (struct luks_data *h);

/* Perform decryption/encryption of a block of memory in-place.
 *
 * 'sector' is the sector number on disk, used to calculate IVs.  (The
 * keyslots also use these functions, but sector must be 0).
 */
extern int do_decrypt (struct luks_data *h, gnutls_cipher_hd_t cipher,
                       uint64_t sector, uint8_t *buf, size_t nr_sectors);
extern int do_encrypt (struct luks_data *h, gnutls_cipher_hd_t cipher,
                       uint64_t sector, uint8_t *buf, size_t nr_sectors);

#endif /* NBDKIT_LUKS_ENCRYPTION_H */
