/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

/* This file deals only with directories and long file names (LFNs).
 * Turns out to be the most complicated part of the FAT format.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <iconv.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

#include "byte-swapping.h"

#include "virtual-floppy.h"

/* Used for dealing with VFAT LFNs when creating a directory. */
struct lfn {
  const char *name;             /* Original Unix filename. */
  char short_base[8];           /* Short basename. */
  char short_ext[3];            /* Short file extension. */
  char *lfn;                    /* Long filename for MS-DOS as UTF16-LE. */
  size_t lfn_size;              /* Size *in bytes* of lfn. */
};

static int add_volume_label (const char *label, size_t di, struct virtual_floppy *floppy);
static int add_dot_entries (size_t di, struct virtual_floppy *floppy);
static int add_directory_entry (const struct lfn *lfn, uint8_t attributes, uint32_t file_size, struct stat *statbuf, size_t di, struct virtual_floppy *floppy);
static uint8_t lfn_checksum (const struct lfn *lfn);
static void set_times (const struct stat *statbuf, struct dir_entry *entry);
static int convert_long_file_names (struct lfn *lfns, size_t n);
static int convert_to_utf16le (const char *name, char **out, size_t *output_len);
static void free_lfns (struct lfn *lfns, size_t n);
static ssize_t append_dir_table (size_t di, const struct dir_entry *entry, struct virtual_floppy *floppy);

/* Create the on disk directory table for dirs[di]. */
int
create_directory (size_t di, const char *label,
                  struct virtual_floppy *floppy)
{
  size_t i;
  const size_t nr_subdirs = floppy->dirs.ptr[di].subdirs.len;
  const size_t nr_files = floppy->dirs.ptr[di].fileidxs.len;
  struct lfn *lfns, *lfn;
  const char *name;
  uint8_t attributes;
  uint32_t file_size;
  struct stat *statbuf;

  if (di == 0) {
    /* For root directory, add the volume label entry first. */
    if (add_volume_label (label, di, floppy) == -1)
      return -1;
  }
  else {
    /* For subdirectories, add "." and ".." entries. */
    if (add_dot_entries (di, floppy) == -1)
      return -1;
  }

  /* Convert all the filenames in the directory into short and long
   * names.  This has to be done for the whole directory because
   * conflicting short names must be renamed.
   */
  lfns = calloc (nr_subdirs + nr_files, sizeof (struct lfn));
  if (lfns == NULL) {
    nbdkit_error ("calloc: %m");
    return -1;
  }
  for (i = 0; i < nr_subdirs; ++i) {
    const size_t sdi = floppy->dirs.ptr[di].subdirs.ptr[i];
    assert (sdi < floppy->dirs.len);

    name = floppy->dirs.ptr[sdi].name;
    lfns[i].name = name;
  }
  for (i = 0; i < nr_files; ++i) {
    const size_t fi = floppy->dirs.ptr[di].fileidxs.ptr[i];
    assert (fi < floppy->files.len);

    name = floppy->files.ptr[fi].name;
    lfns[nr_subdirs+i].name = name;
  }

  if (convert_long_file_names (lfns, nr_subdirs + nr_files) == -1) {
    free_lfns (lfns, nr_subdirs + nr_files);
    return -1;
  }

  /* Add subdirectories. */
  attributes = DIR_ENTRY_SUBDIRECTORY; /* Same as set by Linux kernel. */
  file_size = 0;
  for (i = 0; i < nr_subdirs; ++i) {
    const size_t sdi = floppy->dirs.ptr[di].subdirs.ptr[i];
    assert (sdi < floppy->dirs.len);

    lfn = &lfns[i];
    statbuf = &floppy->dirs.ptr[sdi].statbuf;

    if (add_directory_entry (lfn, attributes, file_size,
                             statbuf, di, floppy) == -1) {
      free_lfns (lfns, nr_subdirs + nr_files);
      return -1;
    }
  }

  /* Add files. */
  attributes = DIR_ENTRY_ARCHIVE; /* Same as set by Linux kernel. */
  for (i = 0; i < nr_files; ++i) {
    const size_t fi = floppy->dirs.ptr[di].fileidxs.ptr[i];
    assert (fi < floppy->files.len);

    lfn = &lfns[nr_subdirs+i];
    statbuf = &floppy->files.ptr[fi].statbuf;
    file_size = statbuf->st_size;

    if (add_directory_entry (lfn, attributes, file_size,
                             statbuf, di, floppy) == -1) {
      free_lfns (lfns, nr_subdirs + nr_files);
      return -1;
    }
  }

  free_lfns (lfns, nr_subdirs + nr_files);
  return 0;
}

/* Add the volume label to dirs[0].table. */
static int
add_volume_label (const char *label, size_t di, struct virtual_floppy *floppy)
{
  ssize_t i;
  struct dir_entry entry;

  assert (di == 0);

  memset (&entry, 0, sizeof entry);
  pad_string (label, 11, entry.name);
  entry.attributes = DIR_ENTRY_VOLUME_LABEL; /* Same as dosfstools. */

  i = append_dir_table (di, &entry, floppy);
  if (i == -1)
    return -1;
  return 0;
}

/* Add "." and ".." entries for subdirectories. */
static int
add_dot_entries (size_t di, struct virtual_floppy *floppy)
{
  ssize_t i, pdi;
  struct dir_entry entry;

  assert (di != 0);

  memset (&entry, 0, sizeof entry);
  pad_string (".", 11, entry.name);
  entry.attributes = DIR_ENTRY_SUBDIRECTORY;
  set_times (&floppy->dirs.ptr[di].statbuf, &entry);

  i = append_dir_table (di, &entry, floppy);
  if (i == -1)
    return -1;

  memset (&entry, 0, sizeof entry);
  pad_string ("..", 11, entry.name);
  entry.attributes = DIR_ENTRY_SUBDIRECTORY;
  pdi = floppy->dirs.ptr[di].pdi;
  set_times (&floppy->dirs.ptr[pdi].statbuf, &entry);

  i = append_dir_table (di, &entry, floppy);
  if (i == -1)
    return -1;

  return 0;
}

/* Either truncate or pad a string (with spaces). */
void
pad_string (const char *label, size_t n, uint8_t *out)
{
  const size_t len = strlen (label);

  memcpy (out, label, len <= n ? len : n);
  if (len < n)
    memset (out+len, ' ', n-len);
}

/* Add a directory entry to dirs[di].table. */
static int
add_directory_entry (const struct lfn *lfn,
                     uint8_t attributes, uint32_t file_size,
                     struct stat *statbuf,
                     size_t di, struct virtual_floppy *floppy)
{
  uint8_t seq, checksum;
  ssize_t i;
  size_t j;
  struct lfn_entry lfn_entry;
  struct dir_entry entry;
  int last_seq = 1;

  /* LFN support.
   *
   * Iterate in reverse over the sequence numbers.  If the filename is:
   *
   *   "ABCDEFGHIJKLMNO"
   *
   * assuming those are UCS-2 codepoints, so lfn_size = 15*2 = 30,
   * then we generate these LFN sequences:
   *
   *   seq   byte_offset   s[13]
   *   0x42  26            "NO<--zeroes->"
   *   0x01  0             "ABCDEFGHIJKLM"
   */
  checksum = lfn_checksum (lfn);
  for (seq = 1 + lfn->lfn_size/2/13; seq >= 1; --seq) {
    size_t byte_offset = (seq-1)*2*13, r;
    uint16_t s[13];

    /* Copy the portion of the LFN into s.
     * r = Number of bytes from the original string to copy.
     */
    r = lfn->lfn_size - byte_offset;
    if (r > 26)
      memcpy (s, &lfn->lfn[byte_offset], 26);
    else {
      memcpy (s, &lfn->lfn[byte_offset], r);
      /* Pad remaining filename with 0. */
      for (j = r/2; j < 13; ++j)
        s[j] = htole16 (0);
    }

    memset (&lfn_entry, 0, sizeof lfn_entry);
    lfn_entry.seq = seq;
    if (last_seq) {
      lfn_entry.seq |= 0x40;
      last_seq = 0;
    }
    lfn_entry.attributes = 0xf;
    lfn_entry.checksum = checksum;

    /* Copy the name portion to the fields in the LFN entry. */
    memcpy (lfn_entry.name1, &s[0], 5*2);
    memcpy (lfn_entry.name2, &s[5], 6*2);
    memcpy (lfn_entry.name3, &s[11], 2*2);

    i = append_dir_table (di, (const struct dir_entry *) &lfn_entry, floppy);
    if (i == -1)
      return -1;
  }

  /* Create the 8.3 (short name / DOS-compatible) entry. */
  memset (&entry, 0, sizeof entry);
  memcpy (entry.name, lfn->short_base, 8);
  memcpy (entry.name+8, lfn->short_ext, 3);
  entry.attributes = attributes;
  set_times (statbuf, &entry);
  entry.size = htole32 (file_size);
  /* Note that entry.cluster_hi and .cluster_lo are set later on in
   * update_directory_first_cluster.
   */

  i = append_dir_table (di, &entry, floppy);
  if (i == -1)
    return -1;

  return 0;
}

/* Compute a checksum for the shortname.  In writable LFN filesystems
 * this is used to check whether a non-LFN-aware operating system
 * (ie. MS-DOS) has edited the directory.  It would ignore the hidden
 * LFN entries and so the checksum would be wrong.
 */
static uint8_t
lfn_checksum (const struct lfn *lfn)
{
  uint8_t checksum;
  size_t i;

  checksum = 0;
  for (i = 0; i < 8; ++i)
    checksum = ((checksum & 1) << 7) + (checksum >> 1) + lfn->short_base[i];
  for (i = 0; i < 3; ++i)
    checksum = ((checksum & 1) << 7) + (checksum >> 1) + lfn->short_ext[i];

  return checksum;
}

/* Set the {c,m,a}date and {c,m}time fields in the entry structure
 * based on metadata found in the statbuf.
 */
#define MAKE_ENTRY_TIME(tm) \
  ((tm).tm_hour << 11 | (tm).tm_min << 5 | ((tm).tm_sec / 2))
#define MAKE_ENTRY_DATE(tm) \
  (((tm).tm_year - 80) << 9 | ((tm).tm_mon + 1) << 5 | (tm).tm_mday)

static void
set_times (const struct stat *statbuf, struct dir_entry *entry)
{
  struct tm ctime_tm, mtime_tm, atime_tm;

  localtime_r (&statbuf->st_ctime, &ctime_tm);
  entry->ctime = MAKE_ENTRY_TIME (ctime_tm);
  entry->ctime_10ms = 100 * (ctime_tm.tm_sec % 2);
  entry->cdate = MAKE_ENTRY_DATE (ctime_tm);

  localtime_r (&statbuf->st_mtime, &mtime_tm);
  entry->mtime = MAKE_ENTRY_TIME (mtime_tm);
  entry->mdate = MAKE_ENTRY_DATE (mtime_tm);

  localtime_r (&statbuf->st_atime, &atime_tm);
  entry->adate = MAKE_ENTRY_DATE (atime_tm);
}

#define UPPER_ASCII "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define LOWER_ASCII "abcdefghijklmnopqrstuvwxyz"
#define NUMBERS_ASCII "0123456789"

/* Characters which are valid in short names.
 *
 * Lowercase is not actually valid, but it makes the
 * implementation below simpler and we toupper the final string.
 *
 * ~ is also valid but don't include it here because we want to
 * keep it as a special character for renaming duplicates below.
 */
static const char short_name_ok[] =
  UPPER_ASCII LOWER_ASCII NUMBERS_ASCII "!#$%&'()-@^_`{}";

static int
convert_long_file_names (struct lfn *lfns, size_t n)
{
  size_t i, j, len;
  struct lfn *lfn;

  /* Split the filenames to generate a list of short basenames + extensions. */
  for (i = 0; i < n; ++i) {
    const char *p;

    lfn = &lfns[i];

    len = strspn (lfn->name, short_name_ok);
    memcpy (lfns[i].short_base, lfn->name, len <= 8 ? len : 8);
    if (len < 8)
      memset (lfn->short_base+len, ' ', 8-len);
    /* Look for the extension. */
    p = strrchr (lfn->name, '.');
    if (p) {
      p++;
      len = strspn (p, short_name_ok);
      memcpy (lfn->short_ext, p, len <= 3 ? len : 3);
      if (len < 3)
        memset (lfn->short_ext+len, ' ', 3-len);
    }
    else
      memset (lfn->short_ext, ' ', sizeof lfn->short_ext);

    /* Convert short name to upper case (ASCII only). */
    for (j = 0; j < 8; ++j) {
      if (strchr (LOWER_ASCII, lfn->short_base[j]))
        lfn->short_base[j] -= 32;
    }
    for (j = 0; j < 3; ++j) {
      if (strchr (LOWER_ASCII, lfn->short_ext[j]))
        lfn->short_ext[j] -= 32;
    }

    /* Convert the original filename to UTF16-LE.  Maximum LFN length
     * is 0x3f * 13 = 819 UCS-2 characters.
     */
    if (convert_to_utf16le (lfn->name, &lfn->lfn, &lfn->lfn_size) == -1)
      return -1;
    if (lfn->lfn_size > 2*819) {
      nbdkit_error ("%s: filename is too long", lfn->name);
      return -1;
    }
  }

  /* Now we must see if some short filenames are duplicates and
   * rename them.  XXX Unfortunately O(n^2).
   */
  for (i = 1; i < n; ++i) {
    for (j = 0; j < i; ++j) {
      if (memcmp (lfns[i].short_base, lfns[j].short_base, 8) == 0 &&
          memcmp (lfns[i].short_ext, lfns[j].short_ext, 3) == 0) {
        char s[9];
        ssize_t k;

        /* Entry i is a duplicate of j (j < i).  So we will rename i. */
        lfn = &lfns[i];

        len = snprintf (s, sizeof s, "~%zu", i);
        assert (len >= 2 && len <= 8);

        k = 8-len;
        while (k > 0 && lfn->short_base[k] == ' ')
          k--;
        memcpy (&lfn->short_base[k], s, len);
      }
    }
  }

  return 0;
}

static const char lfn_encoding[] =
  "UTF-16LE"
#ifdef __GNU_LIBRARY__
  "//TRANSLIT"
#endif
  ;

static int
convert_to_utf16le (const char *name, char **out, size_t *output_len)
{
  iconv_t ic;
  const size_t input_len = strlen (name);
  size_t outalloc, inlen, outlen, prev, r;
  const char *inp;
  char *outp;

  /* XXX Assumes current locale is UTF-8. */
  ic = iconv_open (lfn_encoding, "UTF-8");
  if (ic == (iconv_t)-1) {
    nbdkit_error ("iconv: %m");
    return -1;
  }
  outalloc = input_len;

 again:
  inlen = input_len;
  outlen = outalloc;
  *out = malloc (outlen + 1);
  if (*out == NULL) {
    nbdkit_error ("malloc: %m");
    iconv_close (ic);
    return -1;
  }
  inp = name;
  outp = *out;

  r = iconv (ic, (char **) &inp, &inlen, &outp, &outlen);
  if (r == (size_t)-1) {
    if (errno == E2BIG) {
      prev = outalloc;
      /* Try again with a larger buffer. */
      free (*out);
      *out = NULL;
      outalloc *= 2;
      if (outalloc < prev) {
        nbdkit_error ("iconv: %m");
        iconv_close (ic);
        return -1;
      }
      /* Erase errno so we don't return it to the caller by accident. */
      errno = 0;
      goto again;
    }
    else {
      /* EILSEQ etc. */
      nbdkit_error ("iconv: %s: %m", name);
      free (*out);
      *out = NULL;
      iconv_close (ic);
      return -1;
    }
  }
  *outp = '\0';
  iconv_close (ic);
  if (output_len != NULL)
    *output_len = outp - *out;

  return 0;
}

static void
free_lfns (struct lfn *lfns, size_t n)
{
  size_t i;

  for (i = 0; i < n; ++i)
    free (lfns[i].lfn);
  free (lfns);
}

/* Append entry to dirs[di].table.  Returns the index of the new entry. */
static ssize_t
append_dir_table (size_t di, const struct dir_entry *entry,
                  struct virtual_floppy *floppy)
{
  size_t i;

  i = floppy->dirs.ptr[di].table.len;
  if (dir_entries_append (&floppy->dirs.ptr[di].table, *entry) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  return i;
}

/* In create_directory / add_directory_entry above we run before we
 * have finalised the .first_cluster fields (because that cannot be
 * done until we have sized all the directories).  Here we fix the
 * directory entries with the final cluster number.  Note we must only
 * touch plain directory entries (not the volume label or LFN).
 */
int
update_directory_first_cluster (size_t di, struct virtual_floppy *floppy)
{
  size_t i, j, pdi;
  const size_t nr_subdirs = floppy->dirs.ptr[di].subdirs.len;
  const size_t nr_files = floppy->dirs.ptr[di].fileidxs.len;
  uint32_t first_cluster;
  struct dir_entry *entry;

  /* NB: This function makes assumptions about the order in which
   * subdirectories and files are added to the table so that we can
   * avoid having to maintain another mapping from subdirs/files to
   * table entries.
   */
  i = 0;
  for (j = 0; j < floppy->dirs.ptr[di].table.len; ++j) {
    entry = &floppy->dirs.ptr[di].table.ptr[j];

    /* Skip LFN entries. */
    if (entry->attributes == 0xf)
      continue; /* don't increment i */

    /* Skip the volume label in the root directory. */
    if (entry->attributes == DIR_ENTRY_VOLUME_LABEL)
      continue; /* don't increment i */

    /* Set the first cluster of the "." entry to point to self. */
    if (entry->attributes == DIR_ENTRY_SUBDIRECTORY &&
        memcmp (entry->name, ".          ", 11) == 0) {
      first_cluster = floppy->dirs.ptr[di].first_cluster;
      entry->cluster_hi = htole16 (first_cluster >> 16);
      entry->cluster_lo = htole16 (first_cluster & 0xffff);
      continue; /* don't increment i */
    }

    /* Set the first cluster of the ".." entry to point to parent. */
    if (entry->attributes == DIR_ENTRY_SUBDIRECTORY &&
        memcmp (entry->name, "..         ", 11) == 0) {
      pdi = floppy->dirs.ptr[di].pdi;
      first_cluster = floppy->dirs.ptr[pdi].first_cluster;
      entry->cluster_hi = htole16 (first_cluster >> 16);
      entry->cluster_lo = htole16 (first_cluster & 0xffff);
      continue; /* don't increment i */
    }

    /* Otherwise it's a short name entry so we must now update the
     * first cluster.
     */
    if (i < nr_subdirs) {
      const size_t sdi = floppy->dirs.ptr[di].subdirs.ptr[i];
      assert (sdi < floppy->dirs.len);
      first_cluster = floppy->dirs.ptr[sdi].first_cluster;
    }
    else if (i < nr_subdirs + nr_files) {
      const size_t fi = floppy->dirs.ptr[di].fileidxs.ptr[i-nr_subdirs];
      assert (fi < floppy->files.len);
      first_cluster = floppy->files.ptr[fi].first_cluster;
    }
    else
      abort ();

    entry->cluster_hi = htole16 (first_cluster >> 16);
    entry->cluster_lo = htole16 (first_cluster & 0xffff);
    ++i;
  }

  return 0;
}
