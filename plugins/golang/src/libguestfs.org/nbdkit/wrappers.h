/* cgo wrappers.
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

extern void    wrapper_load ();
extern void    wrapper_unload ();
extern void    wrapper_dump_plugin ();
extern int     wrapper_config (const char *key, const char *value);
extern int     wrapper_config_complete (void);
extern int     wrapper_get_ready (void);
extern int     wrapper_preconnect (int readonly);
extern void *  wrapper_open (int readonly);
extern void    wrapper_close (void *handle);
extern int64_t wrapper_get_size (void *handle);
extern int     wrapper_can_write (void *handle);
extern int     wrapper_can_flush (void *handle);
extern int     wrapper_is_rotational (void *handle);
extern int     wrapper_can_trim (void *handle);
extern int     wrapper_can_zero (void *handle);
extern int     wrapper_can_multi_conn (void *handle);
extern int     wrapper_pread (void *handle, void *buf,
                              uint32_t count, uint64_t offset, uint32_t flags);
extern int     wrapper_pwrite (void *handle, const void *buf,
                               uint32_t count, uint64_t offset, uint32_t flags);
extern int     wrapper_flush (void *handle, uint32_t flags);
extern int     wrapper_trim (void *handle,
                             uint32_t count, uint64_t offset, uint32_t flags);
extern int     wrapper_zero (void *handle,
                             uint32_t count, uint64_t offset, uint32_t flags);
