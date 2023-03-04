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

/* liblzma is a complex interface, so abstract it here. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <nbdkit-filter.h>

#include <lzma.h>

#include "cleanup.h"

#include "xzfile.h"

#define XZ_HEADER_MAGIC     "\xfd" "7zXZ\0"
#define XZ_HEADER_MAGIC_LEN 6
#define XZ_FOOTER_MAGIC     "YZ"
#define XZ_FOOTER_MAGIC_LEN 2

struct xzfile {
  lzma_index *idx;
  size_t nr_streams;
  size_t nr_blocks;
  uint64_t max_uncompressed_block_size;
};

static bool check_header_magic (nbdkit_next *next);
static lzma_index *parse_indexes (nbdkit_next *next, size_t *);
static int iter_indexes (lzma_index *idx, size_t *, uint64_t *);

xzfile *
xzfile_open (nbdkit_next *next)
{
  xzfile *xz;
  uint64_t size;

  xz = malloc (sizeof *xz);
  if (xz == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  /* Check file magic. */
  if (!check_header_magic (next)) {
    nbdkit_error ("xz: not an xz file");
    goto err1;
  }

  /* Read and parse the indexes. */
  xz->idx = parse_indexes (next, &xz->nr_streams);
  if (xz->idx == NULL)
    goto err1;

  /* Iterate over indexes to find the number of and largest block. */
  if (iter_indexes (xz->idx,
                    &xz->nr_blocks, &xz->max_uncompressed_block_size) == -1)
    goto err1;

  size = lzma_index_uncompressed_size (xz->idx);
  nbdkit_debug ("xz: size %" PRIu64 " bytes (%.1fM)",
                size, size / 1024.0 / 1024.0);
  nbdkit_debug ("xz: %zu streams, %zu blocks", xz->nr_streams, xz->nr_blocks);
  nbdkit_debug ("xz: maximum uncompressed block size %" PRIu64 " bytes (%.1fM)",
                xz->max_uncompressed_block_size,
                xz->max_uncompressed_block_size / 1024.0 / 1024.0);

  return xz;

 err1:
  free (xz);
  return NULL;
}

static bool
check_header_magic (nbdkit_next *next)
{
  char buf[XZ_HEADER_MAGIC_LEN];
  int err;

  if (next->get_size (next) < XZ_HEADER_MAGIC_LEN) {
    nbdkit_error ("xz: file too short");
    return false;
  }
  if (next->pread (next, buf, XZ_HEADER_MAGIC_LEN, 0, 0, &err) == -1) {
    nbdkit_error ("xz: could not read header magic: error %d", err);
    return false;
  }
  if (memcmp (buf, XZ_HEADER_MAGIC, XZ_HEADER_MAGIC_LEN) != 0)
    return false;
  return true;
}

/* For explanation of this function, see src/xz/list.c:parse_indexes
 * in the xz sources.
 */
static lzma_index *
parse_indexes (nbdkit_next *next,
               size_t *nr_streams)
{
  lzma_ret r;
  int64_t size, pos, index_size, offs;
  int err;
  uint8_t footer[LZMA_STREAM_HEADER_SIZE];
  uint8_t header[LZMA_STREAM_HEADER_SIZE];
  lzma_stream_flags footer_flags;
  lzma_stream_flags header_flags;
  lzma_stream strm = LZMA_STREAM_INIT;
  lzma_index *combined_index = NULL;
  lzma_index *this_index = NULL;
  lzma_vli stream_padding = 0;

  *nr_streams = 0;

  /* Check file size is a multiple of 4 bytes. */
  pos = size = next->get_size (next);
  if (pos == -1) {
    nbdkit_error ("xz: get_size: %m");
    goto err;
  }
  if ((pos & 3) != 0) {
    nbdkit_error ("xz: not an xz file: size is not a multiple of 4 bytes");
    goto err;
  }

  /* Jump backwards through the file identifying each stream. */
  while (pos > 0) {
    nbdkit_debug ("looping through streams: pos = %" PRIi64, pos);

    if (pos < LZMA_STREAM_HEADER_SIZE) {
      nbdkit_error ("xz: corrupted file at %" PRIi64, pos);
      goto err;
    }

    if (next->pread (next, footer, LZMA_STREAM_HEADER_SIZE,
                     pos - LZMA_STREAM_HEADER_SIZE, 0, &err) == -1) {
      nbdkit_error ("xz: read stream footer: error %d", err);
      goto err;
    }
    /* Skip stream padding. */
    if (footer[8] == 0 && footer[9] == 0 &&
        footer[10] == 0 && footer[11] == 0) {
      stream_padding += 4;
      pos -= 4;
      continue;
    }

    pos -= LZMA_STREAM_HEADER_SIZE;
    (*nr_streams)++;

    nbdkit_debug ("decode stream footer at pos = %" PRIi64, pos);

    /* Does the stream footer look reasonable? */
    r = lzma_stream_footer_decode (&footer_flags, footer);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: invalid stream footer (error %d)", r);
      goto err;
    }
    nbdkit_debug ("backward_size = %" PRIu64,
                  (uint64_t) footer_flags.backward_size);
    index_size = footer_flags.backward_size;
    if (pos < index_size + LZMA_STREAM_HEADER_SIZE) {
      nbdkit_error ("xz: invalid stream footer");
      goto err;
    }

    pos -= index_size;
    nbdkit_debug ("decode index at pos = %" PRIi64, pos);

    /* Decode the index. */
    r = lzma_index_decoder (&strm, &this_index, UINT64_MAX);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: invalid stream index (error %d)", r);
      goto err;
    }

    offs = pos;
    do {
      uint8_t buf[BUFSIZ];

      strm.avail_in = index_size;
      if (strm.avail_in > BUFSIZ)
        strm.avail_in = BUFSIZ;
      if (pos + strm.avail_in > size)
        strm.avail_in = size - pos;

      if (next->pread (next, buf, strm.avail_in, offs, 0, &err) == -1) {
        nbdkit_error ("xz: read index: error %d", err);
        goto err;
      }
      offs += strm.avail_in;
      index_size -= strm.avail_in;

      strm.next_in = buf;
      r = lzma_code (&strm, LZMA_RUN);
    } while (r == LZMA_OK);

    if (r != LZMA_STREAM_END) {
      nbdkit_error ("xz: could not parse index (error %d)", r);
      goto err;
    }

    pos -= lzma_index_total_size (this_index) + LZMA_STREAM_HEADER_SIZE;

    nbdkit_debug ("decode stream header at pos = %" PRIi64, pos);

    /* Read and decode the stream header. */
    if (next->pread (next, header, LZMA_STREAM_HEADER_SIZE, pos, 0,
                     &err) == -1) {
      nbdkit_error ("xz: read stream header: error %d", err);
      goto err;
    }

    r = lzma_stream_header_decode (&header_flags, header);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: invalid stream header (error %d)", r);
      goto err;
    }

    /* Header and footer of the stream should be equal. */
    r = lzma_stream_flags_compare (&header_flags, &footer_flags);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: header and footer of stream are not equal (error %d)",
                    r);
      goto err;
    }

    /* Store the decoded stream flags in this_index. */
    r = lzma_index_stream_flags (this_index, &footer_flags);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: cannot read stream_flags from index (error %d)", r);
      goto err;
    }

    /* Store the amount of stream padding so far.  Needed to calculate
     * compressed offsets correctly in multi-stream files.
     */
    r = lzma_index_stream_padding (this_index, stream_padding);
    if (r != LZMA_OK) {
      nbdkit_error ("xz: cannot set stream_padding in index (error %d)", r);
      goto err;
    }

    if (combined_index != NULL) {
      r = lzma_index_cat (this_index, combined_index, NULL);
      if (r != LZMA_OK) {
        nbdkit_error ("xz: cannot combine indexes");
        goto err;
      }
    }

    combined_index = this_index;
    this_index = NULL;
  }

  lzma_end (&strm);

  return combined_index;

 err:
  lzma_end (&strm);
  lzma_index_end (this_index, NULL);
  lzma_index_end (combined_index, NULL);
  return NULL;
}

/* Iterate over the indexes to find the number of blocks and
 * the largest block.
 */
static int
iter_indexes (lzma_index *idx,
              size_t *nr_blocks, uint64_t *max_uncompressed_block_size)
{
  lzma_index_iter iter;

  *nr_blocks = 0;
  *max_uncompressed_block_size = 0;

  lzma_index_iter_init (&iter, idx);
  while (!lzma_index_iter_next (&iter, LZMA_INDEX_ITER_NONEMPTY_BLOCK)) {
    if (iter.block.uncompressed_size > *max_uncompressed_block_size)
      *max_uncompressed_block_size = iter.block.uncompressed_size;
    (*nr_blocks)++;
  }

  return 0;
}

void
xzfile_close (xzfile *xz)
{
  if (xz) {
    lzma_index_end (xz->idx, NULL);
    free (xz);
  }
}

uint64_t
xzfile_max_uncompressed_block_size (xzfile *xz)
{
  return xz->max_uncompressed_block_size;
}

uint64_t
xzfile_get_size (xzfile *xz)
{
  return lzma_index_uncompressed_size (xz->idx);
}

char *
xzfile_read_block (xzfile *xz,
                   nbdkit_next *next,
                   uint32_t flags, int *err,
                   uint64_t offset,
                   uint64_t *start_rtn, uint64_t *size_rtn)
{
  int64_t offs, size;
  lzma_index_iter iter;
  uint8_t header[LZMA_BLOCK_HEADER_SIZE_MAX];
  lzma_block block;
  lzma_filter filters[LZMA_FILTERS_MAX + 1];
  lzma_ret r;
  lzma_stream strm = LZMA_STREAM_INIT;
  const size_t bufsize = 1024 * 1024;
  CLEANUP_FREE unsigned char *buf = NULL;
  char *data = NULL;
  size_t i;

  /* Read the total size of the underlying disk, so we don't
   * read over the end.
   */
  size = next->get_size (next);
  if (size == -1) {
    nbdkit_error ("xz: get_size: %m");
    return NULL;
  }

  /* Locate the block containing the uncompressed offset. */
  lzma_index_iter_init (&iter, xz->idx);
  if (lzma_index_iter_locate (&iter, offset)) {
    nbdkit_error ("cannot find offset %" PRIu64 " in the xz file", offset);
    return NULL;
  }

  *start_rtn = iter.block.uncompressed_file_offset;
  *size_rtn = iter.block.uncompressed_size;

  nbdkit_debug ("seek: block number %d at file offset %" PRIu64,
                (int) iter.block.number_in_file,
                (uint64_t) iter.block.compressed_file_offset);

  /* Read the block header.  Start by reading a single byte which
   * tell us how big the block header is.
   */
  offs = iter.block.compressed_file_offset;
  if (next->pread (next, header, 1, offs, 0, err) == -1) {
    nbdkit_error ("xz: read: could not read block header byte: error %d", *err);
    return NULL;
  }
  offs++;

  if (header[0] == '\0') {
    nbdkit_error ("xz: read: unexpected invalid block in file, header[0] = 0");
    return NULL;
  }

  block.version = 0;
  block.check = iter.stream.flags->check;
  block.filters = filters;
  block.header_size = lzma_block_header_size_decode (header[0]);

  /* Now read and decode the block header. */
  if (next->pread (next, &header[1], block.header_size-1, offs,
                   0, err) == -1) {
    nbdkit_error ("xz: read: could not read block of compressed data: "
                  "error %d", *err);
    return NULL;
  }
  offs += block.header_size - 1;

  r = lzma_block_header_decode (&block, NULL, header);
  if (r != LZMA_OK) {
    nbdkit_error ("invalid block header (error %d)", r);
    return NULL;
  }

  /* What this actually does is it checks that the block header
   * matches the index.
   */
  r = lzma_block_compressed_size (&block, iter.block.unpadded_size);
  if (r != LZMA_OK) {
    nbdkit_error ("cannot calculate compressed size (error %d)", r);
    goto err1;
  }

  /* Read the block data. */
  r = lzma_block_decoder (&strm, &block);
  if (r != LZMA_OK) {
    nbdkit_error ("invalid block (error %d)", r);
    goto err1;
  }

  data = malloc (*size_rtn);
  if (data == NULL) {
    nbdkit_error ("malloc (%" PRIu64 " bytes): %m\n"
                  "NOTE: If this error occurs, you need to recompress your "
                  "xz files with a smaller block size.  "
                  "Use: 'xz --block-size=16777216 ...'.",
                  *size_rtn);
    goto err2;
  }

  buf = malloc (bufsize);
  if (buf == NULL) {
    nbdkit_error ("malloc: %m");
    goto err2;
  }

  strm.next_in = NULL;
  strm.avail_in = 0;
  strm.next_out = (uint8_t *) data;
  strm.avail_out = block.uncompressed_size;
  do {
    if (strm.avail_in == 0) {
      strm.avail_in = bufsize;
      if (offs + strm.avail_in > size)
        strm.avail_in = size - offs;
      if (strm.avail_in > 0) {
        strm.next_in = buf;
        if (next->pread (next, buf, strm.avail_in, offs, 0, err) == -1) {
          nbdkit_error ("xz: read: error %d", *err);
          goto err2;
        }
        offs += strm.avail_in;
      }
    }

    r = lzma_code (&strm, LZMA_RUN);
  } while (r == LZMA_OK);

  if (r != LZMA_OK && r != LZMA_STREAM_END) {
    nbdkit_error ("could not parse block data (error %d)", r);
    goto err2;
  }

  lzma_end (&strm);

  for (i = 0; filters[i].id != LZMA_VLI_UNKNOWN; ++i)
    free (filters[i].options);

  return data;

 err2:
  lzma_end (&strm);
 err1:
  for (i = 0; filters[i].id != LZMA_VLI_UNKNOWN; ++i)
    free (filters[i].options);

  free (data);

  return NULL;
}
