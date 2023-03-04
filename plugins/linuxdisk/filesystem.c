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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "minmax.h"
#include "rounding.h"
#include "utils.h"

#include "virtual-disk.h"

static int64_t estimate_size (void);
static int mke2fs (const char *filename);

int
create_filesystem (struct virtual_disk *disk)
{
  const char *tmpdir;
  CLEANUP_FREE char *filename = NULL;
  int fd = -1;

  /* Estimate the filesystem size and compute the final virtual size
   * of the disk.  We only need to do this if the user didn't specify
   * the exact size on the command line.
   */
  if (size == 0 || size_add_estimate) {
    int64_t estimate;

    estimate = estimate_size ();
    if (estimate == -1)
      goto error;

    nbdkit_debug ("filesystem size estimate: %" PRIi64, estimate);

    /* Add 20% to the estimate to account for the overhead of
     * filesystem metadata.  Also set a minimum size.  Note we are
     * only wasting virtual space (since this will be stored sparsely
     * under $TMPDIR) so we can be generous here.
     */
    estimate = estimate * 6 / 5;
    estimate = MAX (estimate, 1024*1024);

    /* For ext3 and ext4, add something for the journal. */
    if (strncmp (type, "ext", 3) == 0 && type[3] > '2')
      estimate += 32*1024*1024;

    if (size_add_estimate)
      size += estimate;
    else
      size = estimate;
  }

  /* Round the final size up to a whole number of sectors. */
  size = ROUND_UP (size, SECTOR_SIZE);

  nbdkit_debug ("filesystem virtual size: %" PRIi64, size);

  /* Create the filesystem file. */
  tmpdir = getenv ("TMPDIR");
  if (tmpdir == NULL)
    tmpdir = LARGE_TMPDIR;
  if (asprintf (&filename, "%s/linuxdiskXXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    goto error;
  }

  fd = mkstemp (filename);
  if (fd == -1) {
    nbdkit_error ("mkstemp: %s: %m", filename);
    goto error;
  }
  if (ftruncate (fd, size) == -1) {
    nbdkit_error ("ftruncate: %s: %m", filename);
    goto error;
  }

  /* Create the filesystem. */
  if (mke2fs (filename) == -1)
    goto error;

  unlink (filename);
  disk->filesystem_size = size;
  disk->fd = fd;
  return 0;

 error:
  if (fd >= 0)
    close (fd);
  if (filename)
    unlink (filename);
  return -1;
}

/* Use ‘du’ to estimate the size of the filesystem quickly.  We use
 * the -c option to allow the possibility of supporting multiple
 * directories in future.
 *
 * Typical output from ‘du -cs dir1 dir2’ is:
 *
 * 12345   dir1
 * 34567   dir2
 * 46912   total
 *
 * We ignore everything except the first number on the last line.
 */
static int64_t
estimate_size (void)
{
  CLEANUP_FREE char *command = NULL, *line = NULL;
  size_t len = 0;
  FILE *fp;
  int64_t ret;
  int r;

  /* Create the du command. */
  fp = open_memstream (&command, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }
  fprintf (fp, "du -c -k -s ");
  shell_quote (dir, fp);
  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Run the command. */
  nbdkit_debug ("%s", command);
  fp = popen (command, "r");
  if (fp == NULL) {
    nbdkit_error ("du command failed: %m");
    return -1;
  }

  /* Ignore everything up to the last line. */
  len = 0;
  while (getline (&line, &len, fp) != -1)
    /* empty */;
  if (ferror (fp)) {
    nbdkit_error ("getline failed: %m");
    pclose (fp);
    return -1;
  }

  r = pclose (fp);
  if (r == -1) {
    nbdkit_error ("pclose: %m");
    return -1;
  }
  if (exit_status_to_nbd_error (r, "pclose: du") == -1)
    return -1;

  /* Parse the last line. */
  if (sscanf (line, "%" SCNi64, &ret) != 1 || ret < 0) {
    nbdkit_error ("could not parse last line of output: %s", line);
    return -1;
  }

  /* Result is in 1K blocks, convert it to bytes. */
  ret *= 1024;
  return ret;
}

static int
mke2fs (const char *filename)
{
  CLEANUP_FREE char *command = NULL;
  size_t len = 0;
  FILE *fp;
  int r;

  /* Create the mke2fs command. */
  fp = open_memstream (&command, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }

  fprintf (fp, "mke2fs -q -F -t %s ", type);
  if (label) {
    fprintf (fp, "-L ");
    shell_quote (label, fp);
    fprintf (fp, " ");
  }
  fprintf (fp, "-d ");
  shell_quote (dir, fp);
  fprintf (fp, " ");
  shell_quote (filename, fp);

  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Run the command. */
  nbdkit_debug ("%s", command);
  r = system (command);
  if (exit_status_to_nbd_error (r, "mke2fs") == -1)
    return -1;

  return 0;
}
