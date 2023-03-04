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

/* Copies heavily from e2fsprogs lib/ext2fs/unix_io.c: */

/*
 * io.c --- This is an nbdkit filter implementation of the I/O manager.
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
 *      2002 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include <config.h>

#include <ext2_fs.h>
#include <ext2fs.h>

#include "io.h"

/*
 * For checking structure magic numbers...
 */

#define EXT2_CHECK_MAGIC(struct, code) \
  if ((struct)->magic != (code)) return (code)

struct io_private_data {
  int magic;
  nbdkit_next *next;
  ext2_loff_t offset;
  struct struct_io_stats io_stats;
};

static errcode_t
io_get_stats (io_channel channel, io_stats *stats)
{
  errcode_t retval = 0;

  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (stats)
    *stats = &data->io_stats;

  return retval;
}

/*
 * Here are the raw I/O functions
 */
static errcode_t
raw_read_blk (io_channel channel,
              struct io_private_data *data,
              unsigned long long block,
              int count, void *bufv)
{
  errcode_t retval;
  ssize_t size;
  ext2_loff_t location;
  int actual = 0;
  unsigned char *buf = bufv;

  size = (count < 0) ? -count : (ext2_loff_t) count * channel->block_size;
  data->io_stats.bytes_read += size;
  location = ((ext2_loff_t) block * channel->block_size) + data->offset;

  /* TODO is 32-bit overflow ever likely to be a problem? */
  if (data->next->pread (data->next, buf, size, location, 0, &errno) == 0)
    return 0;

  retval = errno;
  if (channel->read_error)
    retval = (channel->read_error)(channel, block, count, buf,
                 size, actual, retval);
  return retval;
}

static errcode_t
raw_write_blk (io_channel channel,
               struct io_private_data *data,
               unsigned long long block,
               int count, const void *bufv)
{
  ssize_t size;
  ext2_loff_t location;
  int actual = 0;
  errcode_t retval;
  const unsigned char *buf = bufv;

  if (count == 1)
    size = channel->block_size;
  else {
    if (count < 0)
      size = -count;
    else
      size = (ext2_loff_t) count * channel->block_size;
  }
  data->io_stats.bytes_written += size;

  location = ((ext2_loff_t) block * channel->block_size) + data->offset;

  /* TODO is 32-bit overflow ever likely to be a problem? */
  if (data->next->pwrite (data->next, buf, size, location, 0,
                          &errno) == 0)
    return 0;

  retval = errno;
  if (channel->write_error)
    retval = (channel->write_error)(channel, block, count, buf,
                                    size, actual, retval);
  return retval;
}

char *
nbdkit_io_encode (const nbdkit_next *next)
{
  char *ret;

  if (asprintf (&ret, "nbdkit:%p", next) < 0)
    return NULL;
  return ret;
}

int
nbdkit_io_decode (const char *name, nbdkit_next **next)
{
  int n;

  if (sscanf (name, "nbdkit:%p%n", next, &n) != 1 || n != strlen (name))
    return -1;
  return 0;
}

static errcode_t
io_open (const char *name, int flags,
         io_channel *channel)
{
  nbdkit_next *next;
  io_channel io = NULL;
  struct io_private_data *data = NULL;
  errcode_t retval;

  if (nbdkit_io_decode (name, &next) == -1)
    return EXT2_ET_BAD_DEVICE_NAME;

  retval = ext2fs_get_mem (sizeof (struct struct_io_channel), &io);
  if (retval)
    goto cleanup;
  memset (io, 0, sizeof (struct struct_io_channel));
  io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
  retval = ext2fs_get_mem (sizeof (struct io_private_data), &data);
  if (retval)
    goto cleanup;

  io->manager = nbdkit_io_manager;
  retval = ext2fs_get_mem (strlen (name)+1, &io->name);
  if (retval)
    goto cleanup;

  strcpy (io->name, name);
  io->private_data = data;
  io->block_size = 1024;
  io->read_error = 0;
  io->write_error = 0;
  io->refcount = 1;

  memset (data, 0, sizeof (struct io_private_data));
  data->magic = EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL;
  data->io_stats.num_fields = 2;
  data->next = next;

  /* Too bad NBD doesn't tell us if next->trim guarantees read as zero. */
  /* if (next-> XXX (...)
     io->flags |= CHANNEL_FLAGS_DISCARD_ZEROES; */

  if (flags & IO_FLAG_RW && next->can_write (next) != 1) {
    retval = EPERM;
    goto cleanup;
  }
  *channel = io;
  return 0;

cleanup:
  if (data)
    ext2fs_free_mem (&data);
  if (io) {
    if (io->name) {
      ext2fs_free_mem (&io->name);
    }
    ext2fs_free_mem (&io);
  }
  return retval;
}

static errcode_t
io_close (io_channel channel)
{
  struct io_private_data *data;
  errcode_t retval = 0;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (--channel->refcount > 0)
    return 0;

  ext2fs_free_mem (&channel->private_data);
  if (channel->name)
    ext2fs_free_mem (&channel->name);
  ext2fs_free_mem (&channel);
  return retval;
}

static errcode_t
io_set_blksize (io_channel channel, int blksize)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  channel->block_size = blksize;
  return 0;
}

static errcode_t
io_read_blk64 (io_channel channel, unsigned long long block,
               int count, void *buf)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  return raw_read_blk (channel, data, block, count, buf);
}

static errcode_t
io_read_blk (io_channel channel, unsigned long block,
             int count, void *buf)
{
  return io_read_blk64 (channel, block, count, buf);
}

static errcode_t
io_write_blk64 (io_channel channel, unsigned long long block,
                int count, const void *buf)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  return raw_write_blk (channel, data, block, count, buf);
}

#ifdef HAVE_STRUCT_STRUCT_IO_MANAGER_CACHE_READAHEAD
static errcode_t
io_cache_readahead (io_channel channel,
                    unsigned long long block,
                    unsigned long long count)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *)channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next->can_cache (data->next) == NBDKIT_CACHE_NATIVE) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next->cache (data->next,
                           (ext2_loff_t)count * channel->block_size,
                           ((ext2_loff_t)block * channel->block_size +
                            data->offset),
                           0, &errno) == -1)
      return errno;
    return 0;
  }

  return EXT2_ET_OP_NOT_SUPPORTED;
}
#endif

static errcode_t
io_write_blk (io_channel channel, unsigned long block,
              int count, const void *buf)
{
  return io_write_blk64 (channel, block, count, buf);
}

static errcode_t
io_write_byte (io_channel channel, unsigned long offset,
               int size, const void *buf)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next->pwrite (data->next, buf, size,
                          offset + data->offset, 0, &errno) == -1)
    return errno;

  return 0;
}

/*
 * Flush data buffers to disk.
 */
static errcode_t
io_flush (io_channel channel)
{
  struct io_private_data *data;
  errcode_t retval = 0;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next->can_flush (data->next) == 1)
    if (data->next->flush (data->next, 0, &errno) == -1)
      return errno;
  return retval;
}

static errcode_t
io_set_option (io_channel channel, const char *option,
               const char *arg)
{
  struct io_private_data *data;
  unsigned long long tmp;
  char *end;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (!strcmp (option, "offset")) {
    if (!arg)
      return EXT2_ET_INVALID_ARGUMENT;

    tmp = strtoull (arg, &end, 0);
    if (*end)
      return EXT2_ET_INVALID_ARGUMENT;
    data->offset = tmp;
    if (data->offset < 0)
      return EXT2_ET_INVALID_ARGUMENT;
    return 0;
  }
  return EXT2_ET_INVALID_ARGUMENT;
}

static errcode_t
io_discard (io_channel channel, unsigned long long block,
            unsigned long long count)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next->can_trim (data->next) == 1) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next->trim (data->next,
                          (off_t)(count) * channel->block_size,
                          ((off_t)(block) * channel->block_size +
                           data->offset),
                          0, &errno) == 0)
      return 0;
    if (errno == EOPNOTSUPP)
      goto unimplemented;
    return errno;
  }

unimplemented:
  return EXT2_ET_UNIMPLEMENTED;
}

#ifdef HAVE_STRUCT_STRUCT_IO_MANAGER_ZEROOUT
static errcode_t
io_zeroout (io_channel channel, unsigned long long block,
            unsigned long long count)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next->can_zero (data->next) > NBDKIT_ZERO_NONE) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next->zero (data->next,
                          (off_t)(count) * channel->block_size,
                          ((off_t)(block) * channel->block_size +
                           data->offset),
                          NBDKIT_FLAG_MAY_TRIM, &errno) == 0)
      return 0;
    if (errno == EOPNOTSUPP)
      goto unimplemented;
    return errno;
  }

unimplemented:
  return EXT2_ET_UNIMPLEMENTED;
}
#endif

static struct struct_io_manager struct_nbdkit_manager = {
  .magic = EXT2_ET_MAGIC_IO_MANAGER,
  .name = "nbdkit I/O Manager",
  .open = io_open,
  .close = io_close,
  .set_blksize = io_set_blksize,
  .read_blk = io_read_blk,
  .write_blk = io_write_blk,
  .flush = io_flush,
  .write_byte = io_write_byte,
  .set_option = io_set_option,
  .get_stats = io_get_stats,
  .read_blk64 = io_read_blk64,
  .write_blk64 = io_write_blk64,
  .discard = io_discard,
#ifdef HAVE_STRUCT_STRUCT_IO_MANAGER_CACHE_READAHEAD
  .cache_readahead = io_cache_readahead,
#endif
#ifdef HAVE_STRUCT_STRUCT_IO_MANAGER_ZEROOUT
  .zeroout = io_zeroout,
#endif
};

io_manager nbdkit_io_manager = &struct_nbdkit_manager;
