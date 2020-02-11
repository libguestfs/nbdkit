/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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
  struct nbdkit_next_ops *next_ops;
  void *nxdata;
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
  if (data->next_ops->pread (data->nxdata, buf, size, location, 0, &errno) == 0)
    return 0;

  if (channel->read_error)
    retval = (channel->read_error)(channel, block, count, buf,
                 size, actual, errno);
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
  if (data->next_ops->pwrite (data->nxdata, buf, size, location, 0,
                              &errno) == 0)
    return 0;

  if (channel->write_error)
    retval = (channel->write_error)(channel, block, count, buf,
                                    size, actual, errno);
  return retval;
}

char *
nbdkit_io_encode (const struct nbdkit_next *next)
{
  char *ret;

  if (asprintf (&ret, "nbdkit:%p:%p", next->next_ops, next->nxdata) < 0)
    return NULL;
  return ret;
}

int
nbdkit_io_decode (const char *name, struct nbdkit_next *next)
{
  int n;

  if (sscanf (name, "nbdkit:%p:%p%n", &next->next_ops, &next->nxdata,
              &n) != 2 || n != strlen (name))
    return -1;
  return 0;
}

static errcode_t
io_open (const char *name, int flags,
         io_channel *channel)
{
  struct nbdkit_next next;
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
  data->next_ops = next.next_ops;
  data->nxdata = next.nxdata;

  /* Too bad NBD doesn't tell is if next_ops->trim guarantees read as zero. */
  /* if (next_ops-> XXX (...)
     io->flags |= CHANNEL_FLAGS_DISCARD_ZEROES; */

  if (flags & IO_FLAG_RW && next.next_ops->can_write (next.nxdata) != 1) {
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

static errcode_t
io_cache_readahead (io_channel channel,
                    unsigned long long block,
                    unsigned long long count)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *)channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next_ops->can_cache (data->nxdata) == 1) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next_ops->cache (data->nxdata,
                               (ext2_loff_t)count * channel->block_size,
                               ((ext2_loff_t)block * channel->block_size +
                                data->offset),
                               0, &errno) == -1)
      return errno;
    return 0;
  }

  return EXT2_ET_OP_NOT_SUPPORTED;

}

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

  if (data->next_ops->pwrite (data->nxdata, buf, size,
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

  if (data->next_ops->flush (data->nxdata, 0, &errno) == -1)
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

  if (data->next_ops->can_trim (data->nxdata) == 1) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next_ops->trim (data->nxdata,
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

static errcode_t
io_zeroout (io_channel channel, unsigned long long block,
            unsigned long long count)
{
  struct io_private_data *data;

  EXT2_CHECK_MAGIC (channel, EXT2_ET_MAGIC_IO_CHANNEL);
  data = (struct io_private_data *) channel->private_data;
  EXT2_CHECK_MAGIC (data, EXT2_ET_MAGIC_NBDKIT_IO_CHANNEL);

  if (data->next_ops->can_zero (data->nxdata) == 1) {
    /* TODO is 32-bit overflow ever likely to be a problem? */
    if (data->next_ops->zero (data->nxdata,
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
  .cache_readahead = io_cache_readahead,
  .zeroout = io_zeroout,
};

io_manager nbdkit_io_manager = &struct_nbdkit_manager;
