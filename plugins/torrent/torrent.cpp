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

#include <config.h>

#include <cstdlib>
#include <iostream>
#include <atomic>

#include <inttypes.h>
#include <assert.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

extern "C" {
#include "cleanup.h"
};

static bool seen_torrent = false;

static char *cache;
static bool clean_cache_on_exit = true;

/* This lock protects all the static fields that might be accessed by
 * the background thread, as well as the condition.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Name, index within the torrent, and size of the file that we are
 * serving.  It's called index_ because of the function from
 * <strings.h>.
 */
static char *file;
static std::atomic_int index_(-1);
static int64_t size = -1;

static libtorrent::session *session;
static libtorrent::torrent_handle handle;

/* Torrent handle settings. */
static libtorrent::add_torrent_params params;
static libtorrent::settings_pack pack;

static libtorrent::alert_category_t alerts =
    libtorrent::alert_category::error
  | libtorrent::alert_category::piece_progress
  | libtorrent::alert_category::status
  | libtorrent::alert_category::storage
  ;

/* This condition is used to signal the plugin when a piece has been
 * downloaded.
 */
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void
torrent_unload (void)
{
  libtorrent::remove_flags_t flags;

  if (session && handle.is_valid()) {
    if (clean_cache_on_exit)
      flags = libtorrent::session_handle::delete_files;
    session->remove_torrent (handle, flags);
  }

  /* Although in theory libtorrent can remove all the files (see flags
   * above), we still need to remove the temporary directory that we
   * created, and we might as well rm -rf it.  Quoting: Since we're
   * only calling this on temporary paths that we generate, we don't
   * need to quote this.  Probably.  If someone specifies a stupid
   * TMPDIR then it could break, but that's controlled by the person
   * running nbdkit.
   */
  if (clean_cache_on_exit) {
    CLEANUP_FREE char *cmd;
    if (asprintf (&cmd, "rm -rf %s", cache) >= 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
      system (cmd);
#pragma GCC diagnostic pop
    }
  }

  free (cache);
  free (file);

  if (session)
    delete session;
}

static int
torrent_config (const char *key, const char *value)
{
  if (strcmp (key, "torrent") == 0) {
    if (seen_torrent) {
      nbdkit_error ("torrent cannot be specified more than once");
      return -1;
    }
    seen_torrent = true;

    /* In future we want to support downloading automatically from
     * URLs using libcurl, so "reserve" a few likely ones here.
     */
    if (strncmp (value, "http:", 5) == 0 ||
        strncmp (value, "https:", 6) == 0 ||
        strncmp (value, "ftp:", 4) == 0 ||
        strncmp (value, "ftps:", 5) == 0) {
      nbdkit_error ("downloading torrent files from URLs not yet implemented");
      return -1;
    }
    else if (strncmp (value, "file:", 5) == 0) {
      value += 5;
      goto is_file;
    }
    else if (strncmp (value, "magnet:", 7) == 0) {
      libtorrent::error_code err;
      parse_magnet_uri (value, params, err);
      if (err) {
        nbdkit_error ("parsing magnet uri failed: %s",
                      err.message().c_str());
        return -1;
      }
    }
    else {
    is_file:
      CLEANUP_FREE char *torrent_file = nbdkit_realpath (value);
      libtorrent::error_code err;
      if (torrent_file == NULL)
        return -1;
      params.ti = std::make_shared<libtorrent::torrent_info> (torrent_file,
                                                              std::ref (err));
      if (err) {
        nbdkit_error ("parsing torrent metadata failed: %s",
                      err.message().c_str());
        return -1;
      }
    }
  }

  else if (strcmp (key, "file") == 0) {
    file = strdup (value);
    if (file == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
  }

  else if (strcmp (key, "cache") == 0) {
    free (cache);
    cache = nbdkit_realpath (value);
    if (cache == NULL)
      return -1;
    clean_cache_on_exit = false;
  }

  /* Settings. */
  else if (strcmp (key, "download-rate-limit") == 0 ||
           strcmp (key, "download_rate_limit") == 0) {
    int64_t v = nbdkit_parse_size (value);
    if (v == -1)
      return -1;
    pack.set_int (pack.download_rate_limit, int (v / 8));
  }

  else if (strcmp (key, "upload-rate-limit") == 0 ||
           strcmp (key, "upload_rate_limit") == 0) {
    int64_t v = nbdkit_parse_size (value);
    if (v == -1)
      return -1;
    pack.set_int (pack.upload_rate_limit, int (v / 8));
  }

  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
torrent_config_complete (void)
{
  if (!seen_torrent) {
    nbdkit_error ("you must specify a torrent or magnet link");
    return -1;
  }

  /* If no cache was given, create a temporary directory under $TMPDIR. */
  if (!cache) {
    const char *tmpdir = getenv ("TMPDIR") ? : "/var/tmp";

    if (asprintf (&cache, "%s/torrentXXXXXX", tmpdir) == -1) {
      nbdkit_error ("asprintf: %m");
      return -1;
    }

    if (mkdtemp (cache) == NULL) {
      nbdkit_error ("mkdtemp: %m");
      return -1;
    }
  }
  nbdkit_debug ("torrent: cache directory: %s%s",
                cache, clean_cache_on_exit ? " (cleaned up on exit)" : "");
  params.save_path = cache;

  pack.set_str (pack.dht_bootstrap_nodes,
                "router.bittorrent.com:6881,"
		"router.utorrent.com:6881,"
		"dht.transmissionbt.com:6881");
  pack.set_bool (pack.auto_sequential, true);
  pack.set_bool (pack.strict_end_game_mode, false);
  pack.set_bool (pack.announce_to_all_trackers, true);
  pack.set_bool (pack.announce_to_all_tiers, true);
  pack.set_int (pack.alert_mask, alerts);

  return 0;
}

#define torrent_config_help \
  "torrent=<TORRENT>   (required) Torrent or magnet link.\n" \
  "file=DISK.iso                  File to serve within torrent.\n" \
  "cache=DIR                      Set directory to store partial downloads."

/* We got the metadata. */
static void
got_metadata (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  auto ti = handle.torrent_file();
  int i, num_files = ti->num_files();

  if (num_files == 0) {
    nbdkit_error ("torrent: no files in the torrent");
    exit (EXIT_FAILURE);
  }

  /* If the file parameter was not set, pick the largest file. */
  if (!file) {
    int64_t largest = 0;

    nbdkit_debug ("torrent: number of files: %d", num_files);

    for (i = 0; i < num_files; ++i) {
      std::string path = ti->files().file_path(i);
      int64_t sz = ti->files().file_size(i);

      nbdkit_debug ("torrent: file[%d]: %s (size %" PRIi64 ")",
                    i, path.c_str(), sz);
      if (sz > largest) {
        file = strdup (path.c_str());
        largest = sz;
      }
    }
  }
  if (!file) {
    nbdkit_debug ("torrent: no file could be found to serve");
    exit (EXIT_FAILURE);
  }

  /* We should have a file to serve now, so find its index. */
  for (i = 0; i < num_files; ++i) {
    if (ti->files().file_path(i) == file) {
      index_ = i;
      size = ti->files().file_size(i);
      break;
    }
  }

  if (index_ == -1) {
    nbdkit_error ("cannot find file ‘%s’ in the torrent", file);
    exit (EXIT_FAILURE);
  }

  nbdkit_debug ("torrent: serving file index %d: %s",
                index_.load(), file);
}

static void
handle_alert (libtorrent::alert *alert)
{
  using namespace libtorrent;

  nbdkit_debug ("torrent: %s", alert->message().c_str());

  if (metadata_received_alert *p = alert_cast<metadata_received_alert>(alert)) {
    handle = p->handle;
    got_metadata ();
  }

  else if (add_torrent_alert *p = alert_cast<add_torrent_alert>(alert)) {
    handle = p->handle;
    if (handle.status().has_metadata)
      got_metadata ();
  }

  else if (/*piece_finished_alert *p = */
           alert_cast<piece_finished_alert>(alert)) {
    pthread_cond_broadcast (&cond);
  }

  /* We just ignore any other alerts we don't know about, but
   * they are all logged above.
   */
}

static void *
alerts_thread (void *arg)
{
  for (;;) {
    if (!session->wait_for_alert (libtorrent::seconds (5)))
      continue;

    std::vector<libtorrent::alert*> alerts;
    session->pop_alerts (&alerts);
    for (std::vector<libtorrent::alert*>::iterator i = alerts.begin();
         i != alerts.end();
         ++i) {
      handle_alert (*i);
    }
  }
}

/* Create the libtorrent session (which creates an implicit thread).
 * Also start our own background thread to handle libtorrent alerts.
 *
 * We must do all of this after any forking because otherwise the
 * threads will be stranded by fork.
 */
static int
torrent_after_fork (void)
{
  int err;
  pthread_t thread;

  /* Create the session. */
  session = new libtorrent::session (pack);
  if (!session) {
    /* for what reason? XXX */
    nbdkit_error ("could not create libtorrent session");
    return -1;
  }
  session->async_add_torrent (params);

  err = pthread_create (&thread, NULL, alerts_thread, NULL);
  if (err) {
    errno = err;
    nbdkit_error ("pthread_create: %m");
    return -1;
  }

  return 0;
}

static int
torrent_preconnect (int readonly)
{
  if (index_ == -1) {
    /* Wait for a piece to be downloaded, which implicitly waits for
     * metadata.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    pthread_cond_wait (&cond, &lock);
  }

  /* I believe this should never happen, but can't prove it.  If it
   * does happen let's investigate further.
   */
  assert (index_ >= 0);

  return 0;
}

struct handle {
  int fd;
};

static void *
torrent_open (int readonly)
{
  CLEANUP_FREE char *path = NULL;
  int fd = -1;
  struct handle *h;

  if (asprintf (&path, "%s/%s", cache, file) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  /* The file may not exist until at least one piece has been
   * downloaded, so we may need to loop here.
   */
  while ((fd = open (path, O_RDONLY | O_CLOEXEC)) == -1) {
    if (errno != ENOENT) {
      nbdkit_error ("open: %s: %m", path);
      return NULL;
    }

    /* Wait for a piece to be downloaded. */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    pthread_cond_wait (&cond, &lock);
  }

  h = (struct handle *) calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  h->fd = fd;

  return h;
}

static void
torrent_close (void *hv)
{
  struct handle *h = (struct handle *) hv;

  close (h->fd);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
torrent_get_size (void *hv)
{
  return size;
}

/* Read data from the file. */
static int
torrent_pread (void *hv, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  struct handle *h = (struct handle *) hv;
  auto ti = handle.torrent_file();

  while (count > 0) {
    libtorrent::peer_request part =
      ti->map_file (index_.load(), offset, (int) count);

    part.length = std::min (ti->piece_size (part.piece) - part.start,
                            part.length);

    while (! handle.have_piece (part.piece)) {
      /* Tell the picker that we want this piece sooner. */
      handle.piece_priority (part.piece, libtorrent::top_priority);

      /* Wait for a piece to be downloaded. */
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
      pthread_cond_wait (&cond, &lock);
    }

    /* We've got this piece in full (on disk), so we can copy it to
     * the buffer.
     */
    if (pread (h->fd, buf, part.length, offset) == -1) {
      nbdkit_error ("pread: %m");
      return -1;
    }

    count -= part.length;
    offset += part.length;
    buf = (int8_t *)buf + part.length;
  }

  return 0;
}

/* https://bugzilla.redhat.com/show_bug.cgi?id=1418328#c9 */
namespace {
  nbdkit_plugin create_plugin() {
    nbdkit_plugin plugin = nbdkit_plugin ();
    plugin.name              = "torrent";
    plugin.longname          = "nbdkit bittorrent plugin";
    plugin.version           = PACKAGE_VERSION;
    plugin.unload            = torrent_unload;
    plugin.config            = torrent_config;
    plugin.config_complete   = torrent_config_complete;
    plugin.config_help       = torrent_config_help;
    plugin.magic_config_key  = "torrent";
    plugin.after_fork        = torrent_after_fork;
    plugin.preconnect        = torrent_preconnect;
    plugin.open              = torrent_open;
    plugin.close             = torrent_close;
    plugin.get_size          = torrent_get_size;
    plugin.pread             = torrent_pread;
    return plugin;
  }
}
static struct nbdkit_plugin plugin = create_plugin ();

NBDKIT_REGISTER_PLUGIN(plugin)
