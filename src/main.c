/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
 * All rights reserved.
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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <syslog.h>

#include <pthread.h>

#include <dlfcn.h>

#include "internal.h"
#include "exit-with-parent.h"

#define FIRST_SOCKET_ACTIVATION_FD 3 /* defined by systemd ABI */

static int is_short_name (const char *);
static char *make_random_fifo (void);
static struct backend *open_plugin_so (size_t i, const char *filename, int short_name);
static struct backend *open_filter_so (struct backend *next, size_t i, const char *filename, int short_name);
static void start_serving (void);
static void set_up_signals (void);
static void run_command (void);
static void change_user (void);
static void write_pidfile (void);
static void fork_into_background (void);
static uid_t parseuser (const char *);
static gid_t parsegroup (const char *);
static unsigned int get_socket_activation (void);

int exit_with_parent;           /* --exit-with-parent */
const char *exportname;         /* -e */
int foreground;                 /* -f */
const char *ipaddr;             /* -i */
enum log_to log_to = LOG_TO_UNKNOWN; /* --log */
int newstyle = 1;               /* 0 = -o, 1 = -n */
char *pidfile;                  /* -P */
const char *port;               /* -p */
int readonly;                   /* -r */
char *run;                      /* --run */
int listen_stdin;               /* -s */
const char *selinux_label;      /* --selinux-label */
int threads;                    /* -t */
int tls;                        /* --tls : 0=off 1=on 2=require */
const char *tls_certificates_dir; /* --tls-certificates */
const char *tls_psk;            /* --tls-psk */
int tls_verify_peer;            /* --tls-verify-peer */
char *unixsocket;               /* -U */
const char *user, *group;       /* -u & -g */
int verbose;                    /* -v */
unsigned int socket_activation  /* $LISTEN_FDS and $LISTEN_PID set */;

/* Detection of request to exit via signal.  Most places in the code
 * can just poll quit at opportune moments, while sockets.c needs a
 * pipe-to-self through quit_fd in order to break a poll loop without
 * a race. */
volatile int quit;
int quit_fd;
static int write_quit_fd;

/* The currently loaded plugin. */
struct backend *backend;

static char *random_fifo_dir = NULL;
static char *random_fifo = NULL;

enum {
  HELP_OPTION = CHAR_MAX + 1,
  DUMP_CONFIG_OPTION,
  DUMP_PLUGIN_OPTION,
  EXIT_WITH_PARENT_OPTION,
  FILTER_OPTION,
  LOG_OPTION,
  LONG_OPTIONS_OPTION,
  RUN_OPTION,
  SELINUX_LABEL_OPTION,
  SHORT_OPTIONS_OPTION,
  TLS_OPTION,
  TLS_CERTIFICATES_OPTION,
  TLS_PSK_OPTION,
  TLS_VERIFY_PEER_OPTION,
};

static const char *short_options = "e:fg:i:nop:P:rst:u:U:vV";
static const struct option long_options[] = {
  { "dump-config",      no_argument,       NULL, DUMP_CONFIG_OPTION },
  { "dump-plugin",      no_argument,       NULL, DUMP_PLUGIN_OPTION },
  { "exit-with-parent", no_argument,       NULL, EXIT_WITH_PARENT_OPTION },
  { "export",           required_argument, NULL, 'e' },
  { "export-name",      required_argument, NULL, 'e' },
  { "exportname",       required_argument, NULL, 'e' },
  { "filter",           required_argument, NULL, FILTER_OPTION },
  { "foreground",       no_argument,       NULL, 'f' },
  { "no-fork",          no_argument,       NULL, 'f' },
  { "group",            required_argument, NULL, 'g' },
  { "help",             no_argument,       NULL, HELP_OPTION },
  { "ip-addr",          required_argument, NULL, 'i' },
  { "ipaddr",           required_argument, NULL, 'i' },
  { "log",              required_argument, NULL, LOG_OPTION },
  { "long-options",     no_argument,       NULL, LONG_OPTIONS_OPTION },
  { "new-style",        no_argument,       NULL, 'n' },
  { "newstyle",         no_argument,       NULL, 'n' },
  { "old-style",        no_argument,       NULL, 'o' },
  { "oldstyle",         no_argument,       NULL, 'o' },
  { "pid-file",         required_argument, NULL, 'P' },
  { "pidfile",          required_argument, NULL, 'P' },
  { "port",             required_argument, NULL, 'p' },
  { "read-only",        no_argument,       NULL, 'r' },
  { "readonly",         no_argument,       NULL, 'r' },
  { "run",              required_argument, NULL, RUN_OPTION },
  { "selinux-label",    required_argument, NULL, SELINUX_LABEL_OPTION },
  { "short-options",    no_argument,       NULL, SHORT_OPTIONS_OPTION },
  { "single",           no_argument,       NULL, 's' },
  { "stdin",            no_argument,       NULL, 's' },
  { "threads",          required_argument, NULL, 't' },
  { "tls",              required_argument, NULL, TLS_OPTION },
  { "tls-certificates", required_argument, NULL, TLS_CERTIFICATES_OPTION },
  { "tls-psk",          required_argument, NULL, TLS_PSK_OPTION },
  { "tls-verify-peer",  no_argument,       NULL, TLS_VERIFY_PEER_OPTION },
  { "unix",             required_argument, NULL, 'U' },
  { "user",             required_argument, NULL, 'u' },
  { "verbose",          no_argument,       NULL, 'v' },
  { "version",          no_argument,       NULL, 'V' },
  { NULL },
};

static void
usage (void)
{
  /* --{short,long}-options remain undocumented */
  printf ("nbdkit [--dump-config] [--dump-plugin]\n"
          "       [-e EXPORTNAME] [--exit-with-parent] [-f]\n"
          "       [--filter=FILTER ...] [-g GROUP] [-i IPADDR]\n"
          "       [--log=stderr|syslog]\n"
          "       [--newstyle] [--oldstyle] [-P PIDFILE] [-p PORT] [-r]\n"
          "       [--run CMD] [-s] [--selinux-label LABEL] [-t THREADS]\n"
          "       [--tls=off|on|require] [--tls-certificates /path/to/certificates]\n"
          "       [--tls-psk /path/to/pskfile] [--tls-verify-peer]\n"
          "       [-U SOCKET] [-u USER] [-v] [-V]\n"
          "       PLUGIN [key=value [key=value [...]]]\n"
          "\n"
          "Please read the nbdkit(1) manual page for full usage.\n");
}

static void
display_version (void)
{
  printf ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static void
dump_config (void)
{
  printf ("%s=%s\n", "bindir", bindir);
  printf ("%s=%s\n", "filterdir", filterdir);
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
}

int
main (int argc, char *argv[])
{
  int c;
  int option_index;
  int help = 0, version = 0, dump_plugin = 0;
  int tls_set_on_cli = 0;
  int short_name;
  const char *filename;
  char *p;
  static struct filter_filename {
    struct filter_filename *next;
    const char *filename;
  } *filter_filenames = NULL;
  size_t i;

  threadlocal_init ();

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
    c = getopt_long (argc, argv, short_options, long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case DUMP_CONFIG_OPTION:
      dump_config ();
      exit (EXIT_SUCCESS);

    case DUMP_PLUGIN_OPTION:
      dump_plugin = 1;
      break;

    case EXIT_WITH_PARENT_OPTION:
#ifdef HAVE_EXIT_WITH_PARENT
      exit_with_parent = 1;
      foreground = 1;
      break;
#else
      fprintf (stderr, "%s: --exit-with-parent is not implemented for this operating system\n",
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
      else {
        fprintf (stderr, "%s: --log must be \"stderr\" or \"syslog\"\n",
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
      foreground = 1;
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

    case TLS_OPTION:
      tls_set_on_cli = 1;
      if (strcmp (optarg, "off") == 0 || strcmp (optarg, "0") == 0)
        tls = 0;
      else if (strcmp (optarg, "on") == 0 || strcmp (optarg, "1") == 0)
        tls = 1;
      else if (strcmp (optarg, "require") == 0 ||
               strcmp (optarg, "required") == 0 ||
               strcmp (optarg, "force") == 0)
        tls = 2;
      else {
        fprintf (stderr, "%s: --tls flag must be off|on|require\n",
                 program_name);
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
      tls_verify_peer = 1;
      break;

    case 'e':
      exportname = optarg;
      newstyle = 1;
      break;

    case 'f':
      foreground = 1;
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

    case 'n':
      newstyle = 1;
      break;

    case 'o':
      newstyle = 0;
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
      readonly = 1;
      break;

    case 's':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -s flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      listen_stdin = 1;
      break;

    case 't':
      {
        char *end;

        errno = 0;
        threads = strtoul (optarg, &end, 0);
        if (errno || *end) {
          fprintf (stderr, "%s: cannot parse '%s' into threads\n",
                   program_name, optarg);
          exit (EXIT_FAILURE);
        }
        /* XXX Worth a maximimum limit on threads? */
      }
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
      verbose = 1;
      break;

    case 'V':
      version = 1;
      break;

    case HELP_OPTION:
      help = 1;
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
               "'nbdkit /path/to/plugin.so --dump-plugin'\n",
               program_name);
      exit (EXIT_FAILURE);
    }

    /* Otherwise this is an error. */
    fprintf (stderr,
             "%s: no plugins given on the command line.\nRead nbdkit(1) for documentation.\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Oldstyle protocol + exportname not allowed. */
  if (newstyle == 0 && exportname != NULL) {
    fprintf (stderr,
             "%s: cannot use oldstyle protocol (-o) and exportname (-e)\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* If exportname was not set on the command line, use "". */
  if (exportname == NULL)
    exportname = "";

  /* --tls=require and oldstyle won't work. */
  if (tls == 2 && newstyle == 0) {
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

  /* Choose where to log error messages, if not set using --log. */
  if (log_to == LOG_TO_UNKNOWN) {
    log_to = LOG_TO_STDERR;
    /* The next line tests if we will fork into the background. */
    if (!socket_activation && !listen_stdin && !foreground)
      log_to = LOG_TO_SYSLOG;
  }
  if (log_to == LOG_TO_SYSLOG)
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
    size_t i;
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
   * appear on the command line so that in the end ‘backend’ points to
   * the first filter on the command line.
   */
  backend = open_plugin_so (0, filename, short_name);
  i = 1;
  while (filter_filenames) {
    struct filter_filename *t = filter_filenames;
    const char *filename = t->filename;
    int short_name = is_short_name (filename);

    backend = open_filter_so (backend, i++, filename, short_name);

    filter_filenames = t->next;
    free (t);
  }
  lock_init_thread_model ();

  if (help) {
    struct backend *b;

    usage ();
    for_each_backend (b) {
      printf ("\n");
      b->usage (b);
    }
    exit (EXIT_SUCCESS);
  }

  if (version) {
    const char *v;
    struct backend *b;

    display_version ();
    for_each_backend (b) {
      printf ("%s", b->name (b));
      if ((v = b->version (b)) != NULL)
        printf (" %s", v);
      printf ("\n");
    }
    exit (EXIT_SUCCESS);
  }

  /* Find key=value configuration parameters for this plugin.
   * The first one is magical in that if it doesn't contain '=' then
   * we assume it is 'script=...'.
   */
  if (optind < argc && (p = strchr (argv[optind], '=')) == NULL) {
    backend->config (backend, "script", argv[optind]);
    ++optind;
  }

  /* This must run after parsing the possible script parameter so that
   * the script can be loaded for scripting languages.  Note that all
   * scripting languages load the script as soon as they see the
   * script=... parameter (and do not wait for config_complete).
   */
  if (dump_plugin) {
    backend->dump_fields (backend);
    exit (EXIT_SUCCESS);
  }

  while (optind < argc) {
    if ((p = strchr (argv[optind], '=')) != NULL) {
      *p = '\0';
      backend->config (backend, argv[optind], p+1);
      ++optind;
    }
    else {
      fprintf (stderr,
               "%s: expecting key=value on the command line but got: %s\n",
               program_name, argv[optind]);
      exit (EXIT_FAILURE);
    }
  }

  backend->config_complete (backend);

  start_serving ();

  backend->free (backend);
  backend = NULL;

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

  exit (EXIT_SUCCESS);
}

/* Is it a plugin or filter name relative to the plugindir/filterdir? */
static int
is_short_name (const char *filename)
{
  return strchr (filename, '.') == NULL && strchr (filename, '/') == NULL;
}

/* Implementation of '-U -' */
static char *
make_random_fifo (void)
{
  char template[] = "/tmp/nbdkitXXXXXX";
  char *unixsocket;

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

  unixsocket = strdup (random_fifo);
  if (unixsocket == NULL) {
    perror ("strdup");
    return NULL;
  }

  return unixsocket;
}

static struct backend *
open_plugin_so (size_t i, const char *name, int short_name)
{
  struct backend *ret;
  char *filename = (char *) name;
  int free_filename = 0;
  void *dl;
  struct nbdkit_plugin *(*plugin_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the plugindir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-plugin.so", plugindir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = 1;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, filename, dlerror ());
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
  int free_filename = 0;
  void *dl;
  struct nbdkit_filter *(*filter_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the filterdir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-filter.so", filterdir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = 1;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, filename, dlerror ());
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
  int *socks;
  size_t nr_socks;
  size_t i;

  /* If the user has mixed up -p/-U/-s options, then give an error.
   *
   * XXX Actually the server could easily be extended to handle both
   * TCP/IP and Unix sockets, or even multiple TCP/IP ports.
   */
  if ((port && unixsocket) || (port && listen_stdin) ||
      (unixsocket && listen_stdin) || (listen_stdin && run)) {
    fprintf (stderr,
             "%s: -p, -U and -s options cannot appear at the same time\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  set_up_signals ();

  /* Socket activation -- we are handling connections on pre-opened
   * file descriptors [FIRST_SOCKET_ACTIVATION_FD ..
   * FIRST_SOCKET_ACTIVATION_FD+nr_socks-1].
   */
  if (socket_activation) {
    nr_socks = socket_activation;
    debug ("using socket activation, nr_socks = %zu", nr_socks);
    socks = malloc (sizeof (int) * nr_socks);
    if (socks == NULL) {
      perror ("malloc");
      exit (EXIT_FAILURE);
    }
    for (i = 0; i < nr_socks; ++i)
      socks[i] = FIRST_SOCKET_ACTIVATION_FD + i;
    change_user ();
    write_pidfile ();
    accept_incoming_connections (socks, nr_socks);
    free_listening_sockets (socks, nr_socks); /* also closes them */
    return;
  }

  /* Handling a single connection on stdin/stdout. */
  if (listen_stdin) {
    change_user ();
    write_pidfile ();
    threadlocal_new_server_thread ();
    if (handle_single_connection (0, 1) == -1)
      exit (EXIT_FAILURE);
    return;
  }

  /* Handling multiple connections on TCP/IP or a Unix domain socket. */
  if (unixsocket)
    socks = bind_unix_socket (&nr_socks);
  else
    socks = bind_tcpip_socket (&nr_socks);

  run_command ();
  change_user ();
  fork_into_background ();
  write_pidfile ();
  accept_incoming_connections (socks, nr_socks);
  free_listening_sockets (socks, nr_socks);
}

static void
handle_quit (int sig)
{
  char c = sig;

  quit = 1;
  write (write_quit_fd, &c, 1);
}

static void
set_up_signals (void)
{
  struct sigaction sa;
  int fds[2];

  if (pipe (fds) < 0) {
    perror ("pipe");
    exit (EXIT_FAILURE);
  }
  quit_fd = fds[0];
  write_quit_fd = fds[1];

  memset (&sa, 0, sizeof sa);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = handle_quit;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGHUP, &sa, NULL);

  memset (&sa, 0, sizeof sa);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction (SIGPIPE, &sa, NULL);
}

static void
change_user (void)
{
  if (group) {
    gid_t gid = parsegroup (group);

    if (setgid (gid) == -1) {
      perror ("setgid");
      exit (EXIT_FAILURE);
    }

    /* Kill supplemental groups from parent process. */
    if (setgroups (1, &gid) == -1) {
      perror ("setgroups");
      exit (EXIT_FAILURE);
    }

    debug ("changed group to %s", group);
  }

  if (user) {
    uid_t uid = parseuser (user);

    if (setuid (uid) == -1) {
      perror ("setuid");
      exit (EXIT_FAILURE);
    }

    debug ("changed user to %s", user);
  }
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

static void
fork_into_background (void)
{
  pid_t pid;

  if (foreground)
    return;

  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0)                  /* Parent process exits. */
    exit (EXIT_SUCCESS);

  chdir ("/");

  /* Close stdin/stdout and redirect to /dev/null. */
  close (0);
  close (1);
  open ("/dev/null", O_RDONLY);
  open ("/dev/null", O_WRONLY);

  /* If not verbose, set stderr to the same as stdout as well. */
  if (!verbose)
    dup2 (1, 2);

  debug ("forked into background (new pid = %d)", getpid ());
}

static void
run_command (void)
{
  char *url;
  char *cmd;
  int r;
  pid_t pid;

  if (!run)
    return;

  /* Construct an nbd "URL".  Unfortunately guestfish and qemu take
   * different syntax, so try to guess which one we need.
   */
  if (strstr (run, "guestfish")) {
    if (port)
      r = asprintf (&url, "nbd://localhost:%s", port);
    else if (unixsocket)
      /* XXX escaping? */
      r = asprintf (&url, "nbd://?socket=%s", unixsocket);
    else
      abort ();
  }
  else /* qemu */ {
    if (port)
      r = asprintf (&url, "nbd:localhost:%s", port);
    else if (unixsocket)
      r = asprintf (&url, "nbd:unix:%s", unixsocket);
    else
      abort ();
  }
  if (r == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  /* Construct the final command including shell variables. */
  /* XXX Escaping again. */
  r = asprintf (&cmd,
                "nbd='%s'\n"
                "port='%s'\n"
                "unixsocket='%s'\n"
                "%s",
                url, port ? port : "", unixsocket ? unixsocket : "", run);
  if (r == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  free (url);

  /* Fork.  Captive nbdkit runs as the child process. */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0) {              /* Parent process is the run command. */
    r = system (cmd);
    if (WIFEXITED (r))
      r = WEXITSTATUS (r);
    else if (WIFSIGNALED (r)) {
      fprintf (stderr, "%s: external command was killed by signal %d\n",
               program_name, WTERMSIG (r));
      r = 1;
    }
    else if (WIFSTOPPED (r)) {
      fprintf (stderr, "%s: external command was stopped by signal %d\n",
               program_name, WSTOPSIG (r));
      r = 1;
    }

    kill (pid, SIGTERM);        /* Kill captive nbdkit. */

    _exit (r);
  }

  free (cmd);

  debug ("forked into background (new pid = %d)", getpid ());
}

static uid_t
parseuser (const char *id)
{
  struct passwd *pwd;
  int saved_errno;

  errno = 0;
  pwd = getpwnam (id);

  if (NULL == pwd) {
    int val;

    saved_errno = errno;

    if (sscanf (id, "%d", &val) == 1)
      return val;

    fprintf (stderr, "%s: -u option: %s is not a valid user name or uid",
             program_name, id);
    if (saved_errno != 0)
      fprintf (stderr, " (getpwnam error: %s)", strerror (saved_errno));
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
  }

  return pwd->pw_uid;
}

static gid_t
parsegroup (const char *id)
{
  struct group *grp;
  int saved_errno;

  errno = 0;
  grp = getgrnam (id);

  if (NULL == grp) {
    int val;

    saved_errno = errno;

    if (sscanf (id, "%d", &val) == 1)
      return val;

    fprintf (stderr, "%s: -g option: %s is not a valid group name or gid",
             program_name, id);
    if (saved_errno != 0)
      fprintf (stderr, " (getgrnam error: %s)", strerror (saved_errno));
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
  }

  return grp->gr_gid;
}

/* Returns 0 if no socket activation, or the number of FDs.
 * See also virGetListenFDs in libvirt.org:src/util/virutil.c
 */
static unsigned int
get_socket_activation (void)
{
  const char *s;
  unsigned int pid;
  unsigned int nr_fds;
  unsigned int i;
  int fd;

  s = getenv ("LISTEN_PID");
  if (s == NULL)
    return 0;
  if (sscanf (s, "%u", &pid) != 1) {
    fprintf (stderr, "%s: malformed %s environment variable (ignored)\n",
             program_name, "LISTEN_PID");
    return 0;
  }
  if (pid != getpid ()) {
    fprintf (stderr, "%s: %s was not for us (ignored)\n",
             program_name, "LISTEN_PID");
    return 0;
  }

  s = getenv ("LISTEN_FDS");
  if (s == NULL)
    return 0;
  if (sscanf (s, "%u", &nr_fds) != 1) {
    fprintf (stderr, "%s: malformed %s environment variable (ignored)\n",
             program_name, "LISTEN_FDS");
    return 0;
  }

  /* So these are not passed to any child processes we might start. */
  unsetenv ("LISTEN_FDS");
  unsetenv ("LISTEN_PID");

  /* So the file descriptors don't leak into child processes. */
  for (i = 0; i < nr_fds; ++i) {
    fd = FIRST_SOCKET_ACTIVATION_FD + i;
    if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
      /* If we cannot set FD_CLOEXEC then it probably means the file
       * descriptor is invalid, so socket activation has gone wrong
       * and we should exit.
       */
      fprintf (stderr, "%s: socket activation: "
               "invalid file descriptor fd = %d: %m\n",
               program_name, fd);
      exit (EXIT_FAILURE);
    }
  }

  return nr_fds;
}
