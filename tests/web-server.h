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

/* This implements a basic web server supporting range requests.  Note
 * that its only purpose is to answer queries from libcurl over a Unix
 * domain socket.  Any use outside that extremely narrow one will
 * probably not work.
 *
 * It is also not written with security in mind and therefore should
 * only be used for testing over a hidden socket.  In the context of
 * the nbdkit tests this assumption is sound.
 */

#ifndef NBDKIT_WEB_SERVER_H
#define NBDKIT_WEB_SERVER_H

/* Starts a web server in a background thread.  The web server will
 * serve 'filename' (only) - the URL in requests is ignored.
 *
 * Returns the name of the private Unix domain socket which can be
 * used to connect to this web server (using Curl's UNIX_SOCKET_PATH
 * feature).
 *
 * The thread will run until the program exits.  The Unix socket is
 * cleaned up automatically on exit.  Note that the returned string
 * must NOT be freed by the main program.
 *
 * The optional check_request function is called when the request is
 * received (note: not in the main thread) and can be used to perform
 * checks for example that particular headers were sent.
 */
typedef void (*check_request_t) (const char *request);
extern const char *web_server (const char *filename,
                               check_request_t check_request)
  __attribute__ ((__nonnull__ (1)));

#endif /* NBDKIT_WEB_SERVER_H */
