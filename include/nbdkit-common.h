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

#ifndef NBDKIT_COMMON_H
#define NBDKIT_COMMON_H

#if !defined (NBDKIT_PLUGIN_H) && !defined (NBDKIT_FILTER_H)
#error this header file should not be directly included
#endif

#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#if !defined(_WIN32) && !defined(__MINGW32__) && \
    !defined(__CYGWIN__) && !defined(_MSC_VER)
#include <sys/socket.h>
#else
#include <ws2tcpip.h>
#endif

#include <nbdkit-version.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ATTRIBUTE_FORMAT_PRINTF(fmtpos, argpos) \
  __attribute__((__format__ (__printf__, fmtpos, argpos)))
#else
#define ATTRIBUTE_FORMAT_PRINTF(fmtpos, argpos)
#endif

#define NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS     0
#define NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS    1
#define NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS        2
#define NBDKIT_THREAD_MODEL_PARALLEL                  3

#define NBDKIT_FLAG_MAY_TRIM  (1<<0) /* Maps to !NBD_CMD_FLAG_NO_HOLE */
#define NBDKIT_FLAG_FUA       (1<<1) /* Maps to NBD_CMD_FLAG_FUA */
#define NBDKIT_FLAG_REQ_ONE   (1<<2) /* Maps to NBD_CMD_FLAG_REQ_ONE */
#define NBDKIT_FLAG_FAST_ZERO (1<<3) /* Maps to NBD_CMD_FLAG_FAST_ZERO */

#define NBDKIT_FUA_NONE       0
#define NBDKIT_FUA_EMULATE    1
#define NBDKIT_FUA_NATIVE     2

#define NBDKIT_CACHE_NONE     0
#define NBDKIT_CACHE_EMULATE  1
#define NBDKIT_CACHE_NATIVE   2

#define NBDKIT_EXTENT_HOLE    (1<<0) /* Same as NBD_STATE_HOLE */
#define NBDKIT_EXTENT_ZERO    (1<<1) /* Same as NBD_STATE_ZERO */

#ifndef WIN32
#define NBDKIT_EXTERN_DECL(ret, fn, args) extern ret fn args
#else
#define NBDKIT_EXTERN_DECL(ret, fn, args) \
  extern __declspec(dllexport) ret fn args
#endif

NBDKIT_EXTERN_DECL (void, nbdkit_error,
                    (const char *msg, ...) ATTRIBUTE_FORMAT_PRINTF (1, 2));
NBDKIT_EXTERN_DECL (void, nbdkit_verror,
                    (const char *msg, va_list args)
                    ATTRIBUTE_FORMAT_PRINTF (1, 0));
NBDKIT_EXTERN_DECL (void, nbdkit_debug,
                    (const char *msg, ...) ATTRIBUTE_FORMAT_PRINTF (1, 2));
NBDKIT_EXTERN_DECL (void, nbdkit_vdebug,
                    (const char *msg, va_list args)
                    ATTRIBUTE_FORMAT_PRINTF (1, 0));

NBDKIT_EXTERN_DECL (char *, nbdkit_absolute_path, (const char *path));
NBDKIT_EXTERN_DECL (int64_t, nbdkit_parse_size, (const char *str));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_bool, (const char *str));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_int,
                    (const char *what, const char *str, int *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_unsigned,
                    (const char *what, const char *str, unsigned *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_int8_t,
                    (const char *what, const char *str, int8_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_uint8_t,
                    (const char *what, const char *str, uint8_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_int16_t,
                    (const char *what, const char *str, int16_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_uint16_t,
                    (const char *what, const char *str, uint16_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_int32_t,
                    (const char *what, const char *str, int32_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_uint32_t,
                    (const char *what, const char *str, uint32_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_int64_t,
                    (const char *what, const char *str, int64_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_parse_uint64_t,
                    (const char *what, const char *str, uint64_t *r));
NBDKIT_EXTERN_DECL (int, nbdkit_stdio_safe, (void));
NBDKIT_EXTERN_DECL (int, nbdkit_read_password,
                    (const char *value, char **password));
NBDKIT_EXTERN_DECL (char *, nbdkit_realpath, (const char *path));
NBDKIT_EXTERN_DECL (int, nbdkit_nanosleep, (unsigned sec, unsigned nsec));
NBDKIT_EXTERN_DECL (int, nbdkit_peer_name,
                    (struct sockaddr *addr, socklen_t *addrlen));
NBDKIT_EXTERN_DECL (void, nbdkit_shutdown, (void));

struct nbdkit_extents;
NBDKIT_EXTERN_DECL (int, nbdkit_add_extent,
                    (struct nbdkit_extents *,
                     uint64_t offset, uint64_t length, uint32_t type));

struct nbdkit_exports;
NBDKIT_EXTERN_DECL (int, nbdkit_add_export,
                    (struct nbdkit_exports *,
                     const char *name, const char *description));

/* A static non-NULL pointer which can be used when you don't need a
 * per-connection handle.
 */
#define NBDKIT_HANDLE_NOT_NEEDED (&errno)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define NBDKIT_CXX_LANG_C extern "C"
#else
#define NBDKIT_CXX_LANG_C /* nothing */
#endif

#endif /* NBDKIT_COMMON_H */
