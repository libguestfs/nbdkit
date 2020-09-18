/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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

#ifndef NBDKIT_METHODS_H
#define NBDKIT_METHODS_H

/* Defined by the plugin, returns the script name.  For sh_dump_plugin
 * and sh_thread_model ONLY it is possible for this function to return
 * NULL.  From all other contexts it must return a script name.
 */
extern const char *get_script (const char *method);

extern void sh_dump_plugin (void);
extern int sh_thread_model (void);
extern int sh_get_ready (void);
extern int sh_after_fork (void);
extern int sh_preconnect (int readonly);
extern int sh_list_exports (int readonly, int default_only,
                            struct nbdkit_exports *exports);
extern const char *sh_default_export (int readonly, int is_tls);
extern void *sh_open (int readonly);
extern void sh_close (void *handle);
extern const char *sh_export_description (void *handle);
extern int64_t sh_get_size (void *handle);
extern int sh_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
                     uint32_t flags);
extern int sh_pwrite (void *handle, const void *buf, uint32_t count,
                      uint64_t offset, uint32_t flags);
extern int sh_can_write (void *handle);
extern int sh_can_flush (void *handle);
extern int sh_is_rotational (void *handle);
extern int sh_can_trim (void *handle);
extern int sh_can_zero (void *handle);
extern int sh_can_extents (void *handle);
extern int sh_can_fua (void *handle);
extern int sh_can_multi_conn (void *handle);
extern int sh_can_cache (void *handle);
extern int sh_can_fast_zero (void *handle);
extern int sh_flush (void *handle, uint32_t flags);
extern int sh_trim (void *handle, uint32_t count, uint64_t offset,
                    uint32_t flags);
extern int sh_zero (void *handle, uint32_t count, uint64_t offset,
                    uint32_t flags);
extern int sh_extents (void *handle, uint32_t count, uint64_t offset,
                       uint32_t flags, struct nbdkit_extents *extents);
extern int sh_cache (void *handle, uint32_t count, uint64_t offset,
                     uint32_t flags);

#endif /* NBDKIT_METHODS_H */
