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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_LINUX_VM_SOCKETS_H
#include <linux/vm_sockets.h>
#endif

#include <pthread.h>

#include <dlfcn.h>

#include "ascii-string.h"
#include "exit-with-parent.h"
#include "nbd-protocol.h"
#include "realpath.h"
#include "strndup.h"
#include "syslog.h"

#include "internal.h"
#include "options.h"

#ifdef ENABLE_LIBFUZZER
#define main fuzzer_main
#endif

static char *make_random_fifo (void);
static struct backend *open_plugin_so (size_t i, const char *filename, int short_name);
static struct backend *open_filter_so (struct backend *next, size_t i, const char *filename, int short_name);
static void start_serving (void);
static void write_pidfile (void);
static bool is_config_key (const char *key, size_t len);
static void error_if_stdio_closed (void);
static void switch_stdio (void);
static void winsock_init (void);

struct debug_flag *debug_flags; /* -D */
bool exit_with_parent;          /* --exit-with-parent */
const char *export_name;        /* -e */
bool foreground;                /* -f */
const char *ipaddr;             /* -i */
enum log_to log_to = LOG_TO_DEFAULT; /* --log */
unsigned mask_handshake = ~0U;  /* --mask-handshake */
bool newstyle = true;           /* false = -o, true = -n */
bool no_sr;                     /* --no-sr */
char *pidfile;                  /* -P */
const char *port;               /* -p */
bool read_only;                 /* -r */
const char *run;                /* --run */
bool listen_stdin;              /* -s */
const char *selinux_label;      /* --selinux-label */
bool swap;                      /* --swap */
unsigned threads;               /* -t */
int tls;                        /* --tls : 0=off 1=on 2=require */
const char *tls_certificates_dir; /* --tls-certificates */
const char *tls_psk;            /* --tls-psk */
bool tls_verify_peer;           /* --tls-verify-peer */
char *unixsocket;               /* -U */
const char *user, *group;       /* -u & -g */
bool verbose;                   /* -v */
bool vsock;                     /* --vsock */
unsigned int socket_activation; /* $LISTEN_FDS and $LISTEN_PID set */
bool configured;                /* .config_complete done */
int saved_stdin = -1;           /* dup'd stdin during -s/--run */
int saved_stdout = -1;          /* dup'd stdout during -s/--run */

/* The linked list of zero or more filters, and one plugin. */
struct backend *top;

static char *random_fifo_dir = NULL;
static char *random_fifo = NULL;

static void
usage (void)
{
  /* --{short,long}-options remain undocumented */
  const char *opt_list =
#include "synopsis.c"
	  ;
  printf ("%s\n", opt_list);
  printf ("Please read the nbdkit(1) manual page for full usage.\n");
}

static void
display_version (void)
{
  printf ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static void
dump_config (void)
{
  CLEANUP_FREE char *binary = NULL;

#ifdef __linux__
  binary = realpath ("/proc/self/exe", NULL);
#else
#ifdef WIN32
  /* GetModuleFileNameA has a crappy interface that prevents us from
   * getting the length of the path so we just have to guess at an
   * upper limit here.  It will at least truncate it properly with \0.
   * _get_pgmptr would be a better alternative except that it isn't
   * implemented in MinGW.  XXX
   */
  binary = malloc (256);
  if (!GetModuleFileNameA (NULL, binary, 256)) {
    free (binary);
    binary = NULL;
  }
#endif
#endif

  if (binary != NULL)
    printf ("%s=%s\n", "binary", binary);
  printf ("%s=%s\n", "bindir", bindir);
  printf ("%s=%s\n", "filterdir", filterdir);
  printf ("%s=%s\n", "host_cpu", host_cpu);
  printf ("%s=%s\n", "host_os", host_os);
  printf ("%s=%s\n", "libdir", libdir);
  printf ("%s=%s\n", "mandir", mandir);
  printf ("%s=%s\n", "name", PACKAGE_NAME);
  printf ("%s=%s\n", "plugindir", plugindir);
  printf ("%s=%s\n", "root_tls_certificates_dir", root_tls_certificates_dir);
  printf ("%s=%s\n", "sbindir", sbindir);
#ifdef HAVE_LIBSELINUX
  printf ("selinux=yes\n");
#else
  printf ("selinux=no\n");
#endif
  printf ("%s=%s\n", "sysconfdir", sysconfdir);
#ifdef HAVE_GNUTLS
  printf ("tls=yes\n");
#else
  printf ("tls=no\n");
#endif
  printf ("%s=%s\n", "version", PACKAGE_VERSION);
  printf ("%s=%d\n", "version_major", NBDKIT_VERSION_MAJOR);
  printf ("%s=%d\n", "version_minor", NBDKIT_VERSION_MINOR);
#ifdef HAVE_LIBZSTD
  printf ("zstd=yes\n");
#else
  printf ("zstd=no\n");
#endif
}

int
main (int argc, char *argv[])
{
  int c;
  bool help = false, version = false, dump_plugin = false;
  int tls_set_on_cli = false;
  bool short_name;
  const char *filename;
  char *p;
  static struct filter_filename {
    struct filter_filename *next;
    const char *filename;
  } *filter_filenames = NULL;
  size_t i;
  const char *magic_config_key;

  error_if_stdio_closed ();
  winsock_init ();

#if !ENABLE_LIBFUZZER
  threadlocal_init ();
#else
  static bool main_called = false;
  if (!main_called) {
    threadlocal_init ();
    main_called = true;
  }
#endif

  /* The default setting for TLS depends on whether we were
   * compiled with GnuTLS.
   */
#ifdef HAVE_GNUTLS
  tls = 1;
#else
  tls = 0;
#endif

  /* Returns 0 if no socket activation, or the number of FDs. */
  socket_activation = get_socket_activation ();

  for (;;) {
    c = getopt_long (argc, argv, short_options, long_options, NULL);
    if (c == -1)
      break;

    switch (c) {
    case DUMP_CONFIG_OPTION:
      dump_config ();
      exit (EXIT_SUCCESS);

    case DUMP_PLUGIN_OPTION:
      dump_plugin = true;
      break;

    case EXIT_WITH_PARENT_OPTION:
#ifdef HAVE_EXIT_WITH_PARENT
      exit_with_parent = true;
      foreground = true;
      break;
#else
      fprintf (stderr,
               "%s: --exit-with-parent is not implemented "
               "for this operating system\n",
               program_name);
      exit (EXIT_FAILURE);
#endif

    case FILTER_OPTION:
      {
        struct filter_filename *t;

        t = malloc (sizeof *t);
        if (t == NULL) {
          perror ("malloc");
          exit (EXIT_FAILURE);
        }
        t->next = filter_filenames;
        t->filename = optarg;
        filter_filenames = t;
      }
      break;

    case LOG_OPTION:
      if (strcmp (optarg, "stderr") == 0)
        log_to = LOG_TO_STDERR;
      else if (strcmp (optarg, "syslog") == 0)
        log_to = LOG_TO_SYSLOG;
      else if (strcmp (optarg, "null") == 0)
        log_to = LOG_TO_NULL;
      else {
        fprintf (stderr, "%s: "
                 "--log must be \"stderr\", \"syslog\" or \"null\"\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      break;

    case LONG_OPTIONS_OPTION:
      for (i = 0; long_options[i].name != NULL; ++i) {
        if (strcmp (long_options[i].name, "long-options") != 0 &&
            strcmp (long_options[i].name, "short-options") != 0)
          printf ("--%s\n", long_options[i].name);
      }
      exit (EXIT_SUCCESS);

    case RUN_OPTION:
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with --run flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      run = optarg;
      foreground = true;
      break;

    case SELINUX_LABEL_OPTION:
      selinux_label = optarg;
      break;

    case SHORT_OPTIONS_OPTION:
      for (i = 0; short_options[i]; ++i) {
        if (short_options[i] != ':')
          printf ("-%c\n", short_options[i]);
      }
      exit (EXIT_SUCCESS);

    case SWAP_OPTION:
      swap = 1;
      break;

    case TLS_OPTION:
      tls_set_on_cli = true;
      if (ascii_strcasecmp (optarg, "require") == 0 ||
          ascii_strcasecmp (optarg, "required") == 0 ||
          ascii_strcasecmp (optarg, "force") == 0)
        tls = 2;
      else {
        tls = nbdkit_parse_bool (optarg);
        if (tls == -1)
          exit (EXIT_FAILURE);
      }
      break;

    case TLS_CERTIFICATES_OPTION:
      tls_certificates_dir = optarg;
      break;

    case TLS_PSK_OPTION:
      tls_psk = optarg;
      break;

    case TLS_VERIFY_PEER_OPTION:
      tls_verify_peer = true;
      break;

    case VSOCK_OPTION:
#ifdef AF_VSOCK
      vsock = true;
      break;
#else
      fprintf (stderr, "%s: AF_VSOCK is not supported on this platform\n",
               program_name);
      exit (EXIT_FAILURE);
#endif

    case 'D':
      add_debug_flag (optarg);
      break;

    case 'e':
      export_name = optarg;
      break;

    case 'f':
      foreground = true;
      break;

    case 'g':
      group = optarg;
      break;

    case 'i':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -i flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      ipaddr = optarg;
      break;

    case MASK_HANDSHAKE_OPTION:
      if (nbdkit_parse_unsigned ("mask-handshake",
                                 optarg, &mask_handshake) == -1)
        exit (EXIT_FAILURE);
      break;

    case 'n':
      newstyle = true;
      break;

    case NO_SR_OPTION:
      no_sr = true;
      break;

    case 'o':
      newstyle = false;
      break;

    case 'P':
      pidfile = nbdkit_absolute_path (optarg);
      if (pidfile == NULL)
        exit (EXIT_FAILURE);
      break;

    case 'p':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -p flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      port = optarg;
      break;

    case 'r':
      read_only = true;
      break;

    case 's':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -s flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      listen_stdin = true;
#ifdef WIN32
      /* This could be implemented with a bit of work.  The problem
       * currently is that we try to use recv() on the stdio file
       * descriptor which winsock does not support (nor Linux in
       * fact).  We would need to implement a test to see if the file
       * descriptor is a socket or not and use either read or recv as
       * appropriate.
       */
      NOT_IMPLEMENTED_ON_WINDOWS ("-s");
#endif
      break;

    case 't':
      if (nbdkit_parse_unsigned ("threads", optarg, &threads) == -1)
        exit (EXIT_FAILURE);
      /* XXX Worth a maximimum limit on threads? */
      break;

    case 'U':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -U flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      if (strcmp (optarg, "-") == 0)
        unixsocket = make_random_fifo ();
      else
        unixsocket = nbdkit_absolute_path (optarg);
      if (unixsocket == NULL)
        exit (EXIT_FAILURE);
      break;

    case 'u':
      user = optarg;
      break;

    case 'v':
      verbose = true;
      break;

    case 'V':
      version = true;
      break;

    case HELP_OPTION:
      help = true;
      break;

    default:
      usage ();
      exit (EXIT_FAILURE);
    }
  }

  /* No extra parameters. */
  if (optind >= argc) {
    if (help) {
      usage ();
      exit (EXIT_SUCCESS);
    }
    if (version) {
      display_version ();
      exit (EXIT_SUCCESS);
    }
    if (dump_plugin) {
      /* Incorrect use of --dump-plugin. */
      fprintf (stderr,
               "%s: use 'nbdkit plugin --dump-plugin' or\n"
               "'nbdkit /path/to/plugin." SOEXT " --dump-plugin'\n",
               program_name);
      exit (EXIT_FAILURE);
    }

    /* Otherwise this is an error. */
    fprintf (stderr,
             "%s: no plugins given on the command line.\n"
             "Use '%s --help' or "
             "read the nbdkit(1) manual page for documentation.\n",
             program_name, program_name);
    exit (EXIT_FAILURE);
  }

  /* --tls=require and oldstyle won't work. */
  if (tls == 2 && !newstyle) {
    fprintf (stderr,
             "%s: cannot use oldstyle protocol (-o) and require TLS\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Set the umask to a known value.  This makes the behaviour of
   * plugins when creating files more predictable, and also removes an
   * implicit dependency on umask when calling mkstemp(3).
   */
  umask (0022);

  /* If we will or might use syslog. */
  if (log_to == LOG_TO_SYSLOG || log_to == LOG_TO_DEFAULT)
    openlog (program_name, LOG_PID, 0);

  /* Initialize TLS. */
  crypto_init (tls_set_on_cli);
  assert (tls != -1);

  /* Implement --exit-with-parent early in case plugin initialization
   * takes a long time and the parent exits during that time.
   */
#ifdef HAVE_EXIT_WITH_PARENT
  if (exit_with_parent) {
    if (set_exit_with_parent () == -1) {
      perror ("nbdkit: --exit-with-parent");
      exit (EXIT_FAILURE);
    }
  }
#endif

  /* If the user has mixed up -p/--run/-s/-U/--vsock options, then
   * give an error.
   *
   * XXX Actually the server could easily be extended to handle both
   * TCP/IP and Unix sockets, or even multiple TCP/IP ports.
   */
  if ((port && unixsocket) ||
      (port && listen_stdin) ||
      (unixsocket && listen_stdin) ||
      (listen_stdin && run) ||
      (listen_stdin && dump_plugin) ||
      (vsock && unixsocket) ||
      (vsock && listen_stdin) ||
      (vsock && run)) {
    fprintf (stderr,
             "%s: --dump-plugin, -p, --run, -s, -U or --vsock options "
             "cannot be used in this combination\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* The remaining command line arguments are the plugin name and
   * parameters.  If --help, --version or --dump-plugin were specified
   * then we open the plugin so that we can display the per-plugin
   * help/version/plugin information.
   */
  filename = argv[optind++];
  short_name = is_short_name (filename);

  /* Is there an executable script located in the plugindir?
   * If so we simply execute it with the current command line.
   */
  if (short_name) {
    struct stat statbuf;
    CLEANUP_FREE char *script;

    if (asprintf (&script,
                  "%s/nbdkit-%s-plugin", plugindir, filename) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }

    if (stat (script, &statbuf) == 0 &&
        (statbuf.st_mode & S_IXUSR) != 0) {
      /* We're going to execute the plugin directly.
       * Replace argv[0] with argv[optind-1] and move further arguments
       * down the list.
       */
      argv[0] = argv[optind-1];
      for (i = optind; i <= argc; i++)
        argv[i-1] = argv[i];
      execv (script, argv);
      perror (script);
      exit (EXIT_FAILURE);
    }
  }

  /* Open the plugin (first) and then wrap the plugin with the
   * filters.  The filters are wrapped in reverse order that they
   * appear on the command line so that in the end ‘top’ points to
   * the first filter on the command line.
   */
  top = open_plugin_so (0, filename, short_name);
  i = 1;
  while (filter_filenames) {
    struct filter_filename *t = filter_filenames;

    filename = t->filename;
    short_name = is_short_name (filename);

    top = open_filter_so (top, i++, filename, short_name);

    filter_filenames = t->next;
    free (t);
  }

  /* Apply nbdkit.* flags for the server. */
  apply_debug_flags (NULL, "nbdkit");

  /* Check all debug flags were used, and free them. */
  free_debug_flags ();

  if (help) {
    struct backend *b;

    usage ();
    for_each_backend (b) {
      printf ("\n");
      b->usage (b);
    }
    top->free (top);
    exit (EXIT_SUCCESS);
  }

  if (version) {
    const char *v;
    struct backend *b;

    display_version ();
    for_each_backend (b) {
      printf ("%s", b->name);
      if ((v = b->version (b)) != NULL)
        printf (" %s", v);
      printf ("\n");
    }
    top->free (top);
    exit (EXIT_SUCCESS);
  }

  /* Call config and config_complete to parse the parameters.
   *
   * If the plugin provides magic_config_key then any "bare" values
   * (ones not containing "=") are prefixed with this key.
   *
   * For backwards compatibility with old plugins, and to support
   * scripting languages, if magic_config_key == NULL then if the
   * first parameter is bare it is prefixed with the key "script", and
   * any other bare parameters are errors.
   *
   * Keys must live for the life of nbdkit.  Since we want to avoid
   * modifying argv (so that /proc/PID/cmdline remains sane) but we
   * need to create a key from argv[i] = "key=value" we must save the
   * keys in an array which is freed at the end of main().
   */
  char **keys = calloc (argc, sizeof (char *));
  if (keys == NULL) {
    perror ("calloc");
    exit (EXIT_FAILURE);
  }

  magic_config_key = top->magic_config_key (top);
  for (i = 0; optind < argc; ++i, ++optind) {
    size_t n;

    p = strchr (argv[optind], '=');
    n = p - argv[optind];
    if (p && is_config_key (argv[optind], n)) { /* Is it key=value? */
      keys[optind] = strndup (argv[optind], n);
      if (keys[optind] == NULL) {
        perror ("strndup");
        exit (EXIT_FAILURE);
      }
      top->config (top, keys[optind], p+1);
    }
    else if (magic_config_key == NULL) {
      if (i == 0)               /* magic script parameter */
        top->config (top, "script", argv[optind]);
      else {
        fprintf (stderr,
                 "%s: expecting key=value on the command line but got: %s\n",
                 program_name, argv[optind]);
        exit (EXIT_FAILURE);
      }
    }
    else {                      /* magic config key */
      top->config (top, magic_config_key, argv[optind]);
    }
  }

  /* This must run after parsing the parameters so that the script can
   * be loaded for scripting languages.  But it must be called before
   * config_complete so that the plugin doesn't check for missing
   * parameters.
   */
  if (dump_plugin) {
    top->dump_fields (top);
    top->free (top);
    for (i = 1; i < argc; ++i)
      free (keys[i]);
    free (keys);
    exit (EXIT_SUCCESS);
  }

  top->config_complete (top);

  /* Select the correct thread model based on config. */
  lock_init_thread_model ();

  /* Tell the plugin that we are about to start serving.  This must be
   * called before we change user, fork, or open any sockets.
   */
  top->get_ready (top);

  switch_stdio ();
  configured = true;

  start_serving ();

  top->free (top);
  top = NULL;

  free (unixsocket);
  free (pidfile);

  if (random_fifo) {
    unlink (random_fifo);
    free (random_fifo);
  }

  if (random_fifo_dir) {
    rmdir (random_fifo_dir);
    free (random_fifo_dir);
  }

  crypto_free ();
  close_quit_pipe ();

  for (i = 1; i < argc; ++i)
    free (keys[i]);
  free (keys);

  /* Note: Don't exit here, otherwise this won't work when compiled
   * for libFuzzer.
   */
  return EXIT_SUCCESS;
}

#ifndef WIN32

/* Implementation of '-U -' */
static char *
make_random_fifo (void)
{
  char template[] = "/tmp/nbdkitXXXXXX";
  char *sock;

  if (mkdtemp (template) == NULL) {
    perror ("mkdtemp");
    return NULL;
  }

  random_fifo_dir = strdup (template);
  if (random_fifo_dir == NULL) {
    perror ("strdup");
    return NULL;
  }

  if (asprintf (&random_fifo, "%s/socket", template) == -1) {
    perror ("asprintf");
    return NULL;
  }

  sock = strdup (random_fifo);
  if (sock == NULL) {
    perror ("strdup");
    return NULL;
  }

  return sock;
}

#else /* WIN32 */

static char *
make_random_fifo (void)
{
  NOT_IMPLEMENTED_ON_WINDOWS ("-U -");
}

#endif /* WIN32 */

static struct backend *
open_plugin_so (size_t i, const char *name, int short_name)
{
  struct backend *ret;
  char *filename = (char *) name;
  bool free_filename = false;
  void *dl;
  struct nbdkit_plugin *(*plugin_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the plugindir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-plugin." SOEXT, plugindir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = true;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr,
             "%s: error: cannot open plugin '%s': %s\n"
             "Use '%s --help' or "
             "read the nbdkit(1) manual page for documentation.\n",
             program_name, name, dlerror (),
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Initialize the plugin.  See dlopen(3) to understand C weirdness. */
  dlerror ();
  *(void **) (&plugin_init) = dlsym (dl, "plugin_init");
  if ((error = dlerror ()) != NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, name, error);
    exit (EXIT_FAILURE);
  }
  if (!plugin_init) {
    fprintf (stderr, "%s: %s: invalid plugin_init\n", program_name, name);
    exit (EXIT_FAILURE);
  }

  /* Register the plugin. */
  ret = plugin_register (i, filename, dl, plugin_init);

  if (free_filename)
    free (filename);

  return ret;
}

static struct backend *
open_filter_so (struct backend *next, size_t i,
                const char *name, int short_name)
{
  struct backend *ret;
  char *filename = (char *) name;
  bool free_filename = false;
  void *dl;
  struct nbdkit_filter *(*filter_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the filterdir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-filter." SOEXT, filterdir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = true;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr, "%s: error: cannot open filter '%s': %s\n",
             program_name, name, dlerror ());
    exit (EXIT_FAILURE);
  }

  /* Initialize the filter.  See dlopen(3) to understand C weirdness. */
  dlerror ();
  *(void **) (&filter_init) = dlsym (dl, "filter_init");
  if ((error = dlerror ()) != NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, name, error);
    exit (EXIT_FAILURE);
  }
  if (!filter_init) {
    fprintf (stderr, "%s: %s: invalid filter_init\n", program_name, name);
    exit (EXIT_FAILURE);
  }

  /* Register the filter. */
  ret = filter_register (next, i, filename, dl, filter_init);

  if (free_filename)
    free (filename);

  return ret;
}

static void
start_serving (void)
{
  sockets socks = empty_vector;
  size_t i;

  set_up_quit_pipe ();
#if !ENABLE_LIBFUZZER
  set_up_signals ();
#endif

  /* Lock the process into memory if requested. */
  if (swap) {
#ifdef HAVE_MLOCKALL
    if (mlockall (MCL_CURRENT | MCL_FUTURE) == -1) {
      fprintf (stderr, "%s: --swap: mlockall: %m\n", program_name);
      exit (EXIT_FAILURE);
    }
    debug ("mlockall done");
#else
    fprintf (stderr, "%s: mlockall (--swap option) "
             "is not supported on this platform\n", program_name);
    exit (EXIT_FAILURE);
#endif
  }

  /* Socket activation: the ‘socket_activation’ variable (> 0) is the
   * number of file descriptors from FIRST_SOCKET_ACTIVATION_FD to
   * FIRST_SOCKET_ACTIVATION_FD+socket_activation-1.
   */
  if (socket_activation) {
      if (sockets_reserve (&socks, socket_activation) == -1) {
        perror ("realloc");
        exit (EXIT_FAILURE);
      }
    for (i = 0; i < socket_activation; ++i) {
      int s = FIRST_SOCKET_ACTIVATION_FD + i, r;
      /* This can't fail because of the reservation above. */
      r = sockets_append (&socks, s);
      assert (r == 0);
    }
    debug ("using socket activation, nr_socks = %zu", socks.size);
    change_user ();
    write_pidfile ();
    top->after_fork (top);
    accept_incoming_connections (&socks);
    return;
  }

  /* Handling a single connection on stdin/stdout. */
  if (listen_stdin) {
    change_user ();
    write_pidfile ();
    top->after_fork (top);
    threadlocal_new_server_thread ();
    handle_single_connection (saved_stdin, saved_stdout);
    return;
  }

  /* Handling multiple connections on TCP/IP, Unix domain socket or
   * AF_VSOCK.
   */
  if (unixsocket)
    bind_unix_socket (&socks);
  else if (vsock)
    bind_vsock (&socks);
  else
    bind_tcpip_socket (&socks);

  run_command ();
  change_user ();
  fork_into_background ();
  write_pidfile ();
  top->after_fork (top);
  accept_incoming_connections (&socks);
}

static void
write_pidfile (void)
{
  int fd;
  pid_t pid;
  char pidstr[64];
  size_t len;

  if (!pidfile)
    return;

  pid = getpid ();
  snprintf (pidstr, sizeof pidstr, "%d\n", pid);
  len = strlen (pidstr);

  fd = open (pidfile, O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC|O_NOCTTY, 0644);
  if (fd == -1) {
    perror (pidfile);
    exit (EXIT_FAILURE);
  }

  if (write (fd, pidstr, len) < len ||
      close (fd) == -1) {
    perror (pidfile);
    exit (EXIT_FAILURE);
  }

  debug ("written pidfile %s", pidfile);
}

/* When parsing plugin and filter config key=value from the command
 * line, is the key a simple alphanumeric with period, underscore or
 * dash?
 */
static bool
is_config_key (const char *key, size_t len)
{
  static const char allowed_first[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const char allowed[] =
    "._-"
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (len == 0)
    return false;

  if (strchr (allowed_first, key[0]) == NULL)
    return false;

  /* This works in context of the caller since key[len] == '='. */
  if (strspn (key, allowed) != len)
    return false;

  return true;
}

/* Refuse to run if stdin/out/err are closed, whether or not -s is used. */
static void
error_if_stdio_closed (void)
{
#ifdef F_GETFL
  if (fcntl (STDERR_FILENO, F_GETFL) == -1) {
    /* Nowhere we can report the error. Oh well. */
    exit (EXIT_FAILURE);
  }
  if (fcntl (STDIN_FILENO, F_GETFL) == -1 ||
      fcntl (STDOUT_FILENO, F_GETFL) == -1) {
    perror ("expecting stdin/stdout to be opened");
    exit (EXIT_FAILURE);
  }
#endif
}

/* Sanitize stdin/stdout to /dev/null, after saving the originals
 * when needed.  We are still single-threaded at this point, and
 * already checked that stdin/out were open, so we don't have to
 * worry about other threads accidentally grabbing our intended fds,
 * or races on FD_CLOEXEC.  POSIX says that 'fflush(NULL)' is
 * supposed to reset the underlying offset of seekable stdin, but
 * glibc is buggy and requires an explicit fflush(stdin) as
 * well. https://sourceware.org/bugzilla/show_bug.cgi?id=12799
 */
static void
switch_stdio (void)
{
#if defined(F_DUPFD_CLOEXEC) || defined(F_DUPFD)
  fflush (stdin);
  fflush (NULL);
  if (listen_stdin || run) {
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC F_DUPFD
#endif
    saved_stdin = fcntl (STDIN_FILENO, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    saved_stdout = fcntl (STDOUT_FILENO, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
#if F_DUPFD == F_DUPFD_CLOEXEC
    saved_stdin = set_cloexec (saved_stdin);
    saved_stdout = set_cloexec (saved_stdout);
#endif
    if (saved_stdin == -1 || saved_stdout == -1) {
      perror ("fcntl");
      exit (EXIT_FAILURE);
    }
  }
#endif
#ifndef WIN32
  close (STDIN_FILENO);
  close (STDOUT_FILENO);
  if (open ("/dev/null", O_RDONLY) != STDIN_FILENO ||
      open ("/dev/null", O_WRONLY) != STDOUT_FILENO) {
    perror ("open");
    exit (EXIT_FAILURE);
  }
#endif
}

/* On Windows the Winsock library must be initialized early.
 * https://docs.microsoft.com/en-us/windows/win32/winsock/initializing-winsock
 */
static void
winsock_init (void)
{
#ifdef WIN32
  WSADATA wsaData;
  int result;

  result = WSAStartup (MAKEWORD (2, 2), &wsaData);
  if (result != 0) {
    fprintf (stderr, "WSAStartup failed: %d\n", result);
    exit (EXIT_FAILURE);
  }
#endif
}
