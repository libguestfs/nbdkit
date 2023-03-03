#if 0
exec nbdkit cc "$0" "$@"
#endif
#include <stdint.h>
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

char data[100*1024*1024];

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static void *
my_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int64_t
my_get_size (void *handle)
{
  return (int64_t) sizeof (data);
}

static int
my_pread (void *handle, void *buf,
          uint32_t count, uint64_t offset,
          uint32_t flags)
{
  memcpy (buf, data+offset, count);
  return 0;
}

static int
my_pwrite (void *handle, const void *buf,
           uint32_t count, uint64_t offset,
           uint32_t flags)
{
  memcpy (data+offset, buf, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "myplugin",
  .open              = my_open,
  .get_size          = my_get_size,
  .pread             = my_pread,
  .pwrite            = my_pwrite,
};

NBDKIT_REGISTER_PLUGIN (plugin)
