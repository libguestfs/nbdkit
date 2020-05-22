# nbdkit
# Copyright (C) 2013-2020 Red Hat Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

m4_define([NBDKIT_VERSION_MAJOR], [1])
m4_define([NBDKIT_VERSION_MINOR], [21])
m4_define([NBDKIT_VERSION_MICRO], [4])
AC_INIT([nbdkit],
        NBDKIT_VERSION_MAJOR.NBDKIT_VERSION_MINOR.NBDKIT_VERSION_MICRO)
AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AC_USE_SYSTEM_EXTENSIONS],[],
         [m4_define([AC_USE_SYSTEM_EXTENSIONS],[])])

dnl This must be used very early else you will get
dnl "warning: AC_RUN_IFELSE was called before AC_USE_SYSTEM_EXTENSIONS"
AC_USE_SYSTEM_EXTENSIONS

dnl NB: Do not [quote] this parameter.
AM_INIT_AUTOMAKE(foreign)
AC_PROG_LIBTOOL
LT_INIT

dnl List of plugins and filters.
lang_plugins="\
        golang \
        lua \
        ocaml \
        perl \
        python \
        ruby \
        rust \
        sh \
        tcl \
        "
non_lang_plugins="\
        curl \
        data \
        eval \
        example1 \
        example2 \
        example3 \
        example4 \
        ext2 \
        file \
        floppy \
        full \
        guestfs \
        gzip \
        info \
        iso \
        libvirt \
        linuxdisk \
        memory \
        nbd \
        null \
        partitioning \
        pattern \
        random \
        split \
        ssh \
        streaming \
        tar \
        tmpdisk \
        vddk \
        zero \
        "
plugins="$(echo $(printf %s\\n $lang_plugins $non_lang_plugins | sort -u))"
filters="\
        blocksize \
        cache \
        cacheextents \
        cow \
        ddrescue \
        delay \
        error \
        exitlast \
        ext2 \
        extentlist \
        fua \
        ip \
        limit \
        log \
        nocache \
        noextents \
        nofilter \
        noparallel \
        nozero \
        offset \
        partition \
        rate \
        readahead \
        retry \
        stats \
        truncate \
        xz \
        "
AC_SUBST([plugins])
AC_SUBST([lang_plugins])
AC_SUBST([non_lang_plugins])
AC_SUBST([filters])

dnl Some very basic tools.
AC_PROG_SED

dnl Check for basic C environment.
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_CPP
AC_CANONICAL_HOST
AC_SYS_LARGEFILE

AC_C_PROTOTYPES
test "x$U" != "x" && AC_MSG_ERROR([Compiler not ANSI compliant])

AM_PROG_CC_C_O

dnl Check for C++ (optional, we just use this to test the header
dnl can be included from C++ code).
AC_PROG_CXX

dnl The C++ compiler test is pretty useless because even if it fails
dnl it sets CXX=g++.  So test the compiler actually works.
AC_MSG_CHECKING([if the C++ compiler really really works])
AS_IF([$CXX --version >&AS_MESSAGE_LOG_FD 2>&1],[have_cxx=yes],[have_cxx=no])
AC_MSG_RESULT([$have_cxx])
AM_CONDITIONAL([HAVE_CXX], [test "$have_cxx" = "yes"])

AC_ARG_ENABLE([gcc-warnings],
    [AS_HELP_STRING([--enable-gcc-warnings],
                    [turn on lots of GCC warnings (for developers)])],
     [case $enableval in
      yes|no) ;;
      *)      AC_MSG_ERROR([bad value $enableval for gcc-warnings option]) ;;
      esac
      gcc_warnings=$enableval],
      [gcc_warnings=no]
)
if test "x$gcc_warnings" = "xyes"; then
    WARNINGS_CFLAGS="-Wall -Wshadow -Wvla -Werror"
    AC_SUBST([WARNINGS_CFLAGS])
fi

dnl Check if the compiler supports -std=c90 flag.  This is only used
dnl during a test.  OpenBSD GCC does not support this flag so we skip
dnl that test.
AC_MSG_CHECKING([if the compiler supports -std=c90 for ANSI C test])
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -std=c90"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
    [supports_std_c90=yes],
    [supports_std_c90=no])
CFLAGS="$old_CFLAGS"
AC_MSG_RESULT([$supports_std_c90])
AM_CONDITIONAL([CAN_TEST_ANSI_C], [test "x$supports_std_c90" = "xyes"])

dnl On Haiku we must use BSD-compatibility headers to get the endian
dnl macros we use.
AC_MSG_CHECKING(whether OS-dependent include paths are required)
AS_CASE([$host_os],
  [haiku*], [CFLAGS="$CFLAGS -I`findpaths -p /system B_FIND_PATH_HEADERS_DIRECTORY`/bsd"; AC_MSG_RESULT(yes)],
  [AC_MSG_RESULT(no)]
)

dnl On Linux /tmp is often tmpfs which is not large enough, so use /var/tmp.
dnl But Haiku only has /tmp.
AC_MSG_CHECKING([for temporary directory for large files])
AS_CASE([$host_os],
  [haiku*], [LARGE_TMPDIR=/tmp],
  [LARGE_TMPDIR=/var/tmp]
)
AC_MSG_RESULT([$LARGE_TMPDIR])
AC_DEFINE_UNQUOTED([LARGE_TMPDIR],["$LARGE_TMPDIR"],
                   [Temporary directory for large files])

dnl Check if libc has program_invocation_short_name.
AC_CHECK_DECLS([program_invocation_short_name], [], [], [#include <errno.h>])

AX_PTHREAD

dnl Check if __attribute__((cleanup(...))) works.
dnl Set -Werror, otherwise gcc will only emit a warning for attributes
dnl that it doesn't understand.
acx_nbdkit_save_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -Werror"
AC_MSG_CHECKING([if __attribute__((cleanup(...))) works with this compiler])
AC_COMPILE_IFELSE([
AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>

void
freep (void *ptr)
{
  exit (EXIT_SUCCESS);
}

void
test (void)
{
  __attribute__((cleanup(freep))) char *ptr = malloc (100);
}

int
main (int argc, char *argv[])
{
  test ();
  exit (EXIT_FAILURE);
}
]])
    ],[
    AC_MSG_RESULT([yes])
    ],[
    AC_MSG_RESULT([no])
    AC_MSG_ERROR(
['__attribute__((cleanup(...)))' does not work.

You may not be using a sufficiently recent version of GCC or CLANG, or
you may be using a C compiler which does not support this attribute,
or the configure test may be wrong.

This code requires the attribute to work for proper locking between threads.])])
CFLAGS="${acx_nbdkit_save_CFLAGS}"

dnl Check for __auto_type (GCC extension).
AC_MSG_CHECKING([if __auto_type is available in this compiler])
AC_COMPILE_IFELSE([
AC_LANG_SOURCE([[
static int
test (int a)
{
  __auto_type at = a;
  return at;
}
]])
    ],[
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_AUTO_TYPE],[1],[__auto_type is available])
    ],[
    AC_MSG_RESULT([no])
    ]
)

dnl 'environ' is not always declared in public header files:
dnl Linux => <unistd.h>  Haiku => <stdlib.h>
dnl FreeBSD & OpenBSD => not declared
dnl On platforms where it's not declared we must add an extern decl.
AC_MSG_CHECKING([if environ is declared in header files])
AC_COMPILE_IFELSE([
AC_LANG_SOURCE([[
#include <stdlib.h>
#include <unistd.h>
static int
test (void)
{
  const char **env = environ;
  return env ? 1 : 0; // this just forces env to be used
}
]])
    ],[
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_ENVIRON_DECL],[1],[environ is declared in headers])
    ],[
    AC_MSG_RESULT([no])
    ]
)

dnl Check for other headers, all optional.
AC_CHECK_HEADERS([\
	alloca.h \
	byteswap.h \
	endian.h \
	stdatomic.h \
	sys/endian.h \
	sys/mman.h \
	sys/prctl.h \
	sys/procctl.h])

AC_CHECK_HEADERS([linux/vm_sockets.h], [], [], [#include <sys/socket.h>])

dnl Check for functions in libc, all optional.
AC_CHECK_FUNCS([\
	accept4 \
	fdatasync \
	get_current_dir_name \
	mkostemp \
	mlockall \
	pipe2 \
	ppoll \
	posix_fadvise])

dnl Check whether printf("%m") works
AC_CACHE_CHECK([whether the printf family supports %m],
  [nbdkit_cv_func_printf_percent_m],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <stdio.h>
#include <string.h>
#include <errno.h>
    ]], [[
    char buf[200] = "";
    errno = EINVAL;
    snprintf(buf, sizeof buf, "%m");
    return !!strcmp (buf, strerror (EINVAL));
    ]])],
    [nbdkit_cv_func_printf_percent_m=yes],
    [nbdkit_cv_func_printf_percent_m=no],
    [[
    case "$host_os" in
      *-gnu* | gnu*) nbdkit_cv_func_printf_percent_m=yes;;
      *) nbdkit_cv_func_printf_percent_m="guessing no";;
    esac
    ]])])
AS_IF([test "x$nbdkit_cv_func_printf_percent_m" = xyes],
  [AC_DEFINE([HAVE_VFPRINTF_PERCENT_M],[1],
    [Define to 1 if vfprintf supports %m.])])

old_LIBS="$LIBS"
AC_SEARCH_LIBS([dlsym], [dl dld], [
        AS_IF([test "x$ac_cv_search_dlsym" != "xnone required"],
	    [DL_LIBS="$ac_cv_search_dlsym"], [DL_LIBS=])
        AC_SUBST([DL_LIBS])
    ], [AC_MSG_ERROR([unable to find the dlsym() function])
])
LIBS="$old_LIBS"

dnl Test if <iconv.h> header can build working binaries.
dnl
dnl On FreeBSD: iconv and libiconv both exist, both can be installed
dnl simultaneously, <iconv.h> can exist in two separate places, and
dnl if you get the wrong header/library mix everything breaks.
dnl
dnl On Haiku: libiconv is required to link to iconv_* functions.
AC_ARG_WITH([iconv],
    [AS_HELP_STRING([--without-iconv],
                    [don't try to link against iconv @<:@default=check@:>@])],
    [],
    [with_iconv=check])
AS_IF([test "x$with_iconv" != "xno"],[
    AC_CHECK_HEADER([iconv.h],[
        AC_MSG_CHECKING([if <iconv.h> can be used to link a program])
        AC_LINK_IFELSE([
AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#include <iconv.h>
int
main (int argc, char *argv[])
{
  iconv_t ic = iconv_open ("", "");
  iconv_close (ic);
  exit (0);
}
]])
            ],[
            AC_MSG_RESULT([yes])
            iconv_working=yes
            ],[
            AC_MSG_RESULT([no])
            ])
        ])
    ])
AM_CONDITIONAL([HAVE_ICONV], [test "x$iconv_working" = "xyes"])

dnl Don't use linker script for the server on FreeBSD because
dnl FreeBSD's linker is broken.  See eg:
dnl https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=190851
AC_MSG_CHECKING([if we should use a linker script for the server])
AS_CASE([$host_os],
  [freebsd*], [use_linker_script_for_server=no],
  [use_linker_script_for_server=yes]
)
AC_MSG_RESULT([$use_linker_script_for_server])
AM_CONDITIONAL([USE_LINKER_SCRIPT_FOR_SERVER],
               [test "x$use_linker_script_for_server" = "xyes"])

dnl Check if -rdynamic linker flag works.
acx_nbdkit_save_LDFLAGS="${LDFLAGS}"
LDFLAGS="${LDFLAGS} -rdynamic"
AC_MSG_CHECKING([if linker supports -rdynamic])
AC_LINK_IFELSE([
AC_LANG_SOURCE([[
#include <stdlib.h>
int
main (int argc, char *argv[])
{
  exit (EXIT_SUCCESS);
}
]])
    ],[
    AC_MSG_RESULT([yes])
    DL_LDFLAGS=-rdynamic
    ],[
    AC_MSG_RESULT([no])
])
dnl restore CFLAGS
LDFLAGS="${acx_nbdkit_save_LDFLAGS}"
AC_SUBST([DL_LDFLAGS])

dnl Check for dladdr in -ldl, optional.  This is a glibc extension.
old_LIBS="$LIBS"
LIBS="$DL_LIBS $LIBS"
AC_CHECK_FUNCS([dladdr])
LIBS="$old_LIBS"

dnl On mingw we should enable the -no-undefined flag.
AC_MSG_CHECKING([SHARED_LDFLAGS])
AS_CASE([$host_os],
    [mingw*|msys*|cygwin*], [SHARED_LDFLAGS="$SHARED_LDFLAGS -no-undefined"],
    []
)
AC_MSG_RESULT([$SHARED_LDFLAGS])
AC_SUBST([SHARED_LDFLAGS])

AC_SEARCH_LIBS([getaddrinfo], [network socket])

dnl Check for SELinux socket labelling (optional).
PKG_CHECK_MODULES([LIBSELINUX], [libselinux], [
    AC_SUBST([LIBSELINUX_CFLAGS])
    AC_SUBST([LIBSELINUX_LIBS])
    AC_DEFINE([HAVE_LIBSELINUX],[1],[libselinux found at compile time.])
], [
    AC_MSG_WARN([libselinux not found, SELinux socket labelling support will be disabled.])
])

dnl Check for GnuTLS (optional, for TLS support).
PKG_CHECK_MODULES([GNUTLS], [gnutls >= 3.3.0], [
    AC_SUBST([GNUTLS_CFLAGS])
    AC_SUBST([GNUTLS_LIBS])
    AC_DEFINE([HAVE_GNUTLS],[1],[gnutls found at compile time.])
], [
    AC_MSG_WARN([gnutls not found or < 3.3.0, TLS support will be disabled.])
])

AS_IF([test "$GNUTLS_LIBS" != ""],[
    AC_MSG_CHECKING([for default TLS session priority string])
    AC_ARG_WITH([tls-priority],
        [AS_HELP_STRING([--with-tls-priority],
                        [default TLS session priority string @<:@default=NORMAL@:>@])],
        [tls_priority=$withval],
        [tls_priority=NORMAL])
    AC_MSG_RESULT([$tls_priority])
    AC_DEFINE_UNQUOTED([TLS_PRIORITY],["$tls_priority"],
                       [Default TLS session priority string])

    # Check for APIs which may not be present.
    old_LIBS="$LIBS"
    LIBS="$GNUTLS_LIBS $LIBS"
    AC_CHECK_FUNCS([\
	gnutls_base64_decode2 \
	gnutls_certificate_set_known_dh_params \
	gnutls_session_set_verify_cert])
    LIBS="$old_LIBS"
])

dnl Check for mke2fs -d (used by linuxdisk plugin).  There are two
dnl possible outcomes that we care about: (1) We have mke2fs and
dnl it supports the -d option.  (2) We either don't have mke2fs
dnl or it's too old to support the -d option (eg. on RHEL 7).
mke2fs_with_d=no
AC_MSG_CHECKING([for mke2fs supporting the -d option])
AS_IF([mke2fs -V >/dev/null 2>&1], [
    AS_IF([LANG=C mke2fs -d 2>&1 | grep -sq "option requires an argument"], [
        mke2fs_with_d=yes
    ])
])
AC_MSG_RESULT([$mke2fs_with_d])
AM_CONDITIONAL([HAVE_MKE2FS_WITH_D],[test "x$mke2fs_with_d" = "xyes"])

dnl Check for valgrind.
AC_CHECK_PROG([VALGRIND],[valgrind],[valgrind],[no])

dnl If valgrind headers are available (optional).
dnl Since this is only useful for developers, you have to enable
dnl it explicitly using --enable-valgrind.
AC_ARG_ENABLE([valgrind],
    [AS_HELP_STRING([--enable-valgrind],
                    [enable Valgrind extensions (for developers)])],
    [enable_valgrind=yes],
    [enable_valgrind=no])
AS_IF([test "x$enable_valgrind" = "xyes"],[
    PKG_CHECK_MODULES([VALGRIND], [valgrind], [
        AC_SUBST([VALGRIND_CFLAGS])
        AC_SUBST([VALGRIND_LIBS])
        AC_DEFINE([HAVE_VALGRIND],[1],[Valgrind headers found at compile time])
    ],[
        AC_MSG_ERROR([--enable-valgrind given, but Valgrind headers are not available])
    ])
])

dnl Build the special libFuzzer version of nbdkit.  DO NOT USE THIS for
dnl normal builds.  See fuzzing/README.
AC_ARG_ENABLE([libfuzzer],
    [AS_HELP_STRING([--enable-libfuzzer],
                    [build libFuzzer test binary (developers only)])],
    [enable_libfuzzer=yes],
    [enable_libfuzzer=no])
AS_IF([test "x$enable_libfuzzer" = "xyes"],[
    AC_DEFINE([ENABLE_LIBFUZZER],[1],[Enable special libFuzzer binary])
])
AM_CONDITIONAL([ENABLE_LIBFUZZER],[test "x$enable_libfuzzer" = "xyes"])

dnl Bash completion.
PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0], [
    bash_completion=yes
    AC_MSG_CHECKING([for bash-completions directory])
    m4_ifdef([PKG_CHECK_VAR],[
        PKG_CHECK_VAR(bashcompdir, [bash-completion], [completionsdir])
    ])
    AS_IF([test -z "$bashcompdir"], [
        bashcompdir="${sysconfdir}/bash_completion.d"
    ])
    AC_MSG_RESULT([$bashcompdir])
    AC_SUBST([bashcompdir])
],[
    bash_completion=no
    AC_MSG_WARN([bash-completion not installed])
])
AM_CONDITIONAL([HAVE_BASH_COMPLETION],[test "x$bash_completion" = "xyes"])

dnl Check we have enough to run podwrapper.
AC_CHECK_PROG([PERL],[perl],[perl],[no])
AS_IF([test "x$PERL" != "xno"],[
    AC_MSG_CHECKING([if we have perl Pod::Man and Pod::Simple])
    AS_IF([$PERL -MPod::Man -MPod::Simple -e 1 >&AS_MESSAGE_LOG_FD 2>&1],[
        enable_pod=yes
    ],[
        enable_pod=no
    ])
    AC_MSG_RESULT([$enable_pod])
])
AM_CONDITIONAL([HAVE_POD],
               [test "x$PERL" != "xno" && test "x$enable_pod" = "xyes"])

dnl Define the path to the podwrapper program.
PODWRAPPER="$PERL $(pwd)/podwrapper.pl"
AC_SUBST([PODWRAPPER])

dnl Allow all plugins and filters to be disabled.
AC_ARG_ENABLE([plugins],
    [AS_HELP_STRING([--disable-plugins],
                    [disable all bundled plugins and filters])])
AM_CONDITIONAL([HAVE_PLUGINS], [test "x$enable_plugins" != "xno"])

dnl Check for Perl, for embedding in the perl plugin.
dnl Note that the perl binary is checked above.
AC_ARG_ENABLE([perl],
    [AS_HELP_STRING([--disable-perl], [disable Perl embed plugin])],
    [],
    [enable_perl=yes])
AS_IF([test "x$PERL" != "xno" && test "x$enable_perl" != "xno"],[
    dnl Check for Perl archlib.
    AC_MSG_CHECKING([for Perl embed archlib])
    PERL_ARCHLIB="$($PERL -MConfig -e 'print $Config{archlib}')"
    AS_IF([ test -n "$PERL_ARCHLIB" ],[
        AC_MSG_RESULT([$PERL_ARCHLIB])
    ],[
        AC_MSG_NOTICE([Perl embed module disabled])
        enable_perl=no
    ])

    dnl Check for Perl CFLAGS.
    AC_MSG_CHECKING([for Perl embed CFLAGS])
    PERL_CFLAGS="$($PERL -MExtUtils::Embed -e 'ccflags')"
    AS_IF([ test -n "$PERL_CFLAGS" ],[
        AC_MSG_RESULT([$PERL_CFLAGS])
    ],[
        AC_MSG_NOTICE([Perl embed module disabled])
        enable_perl=no
    ])

    dnl Check for Perl LDOPTS.
    AC_MSG_CHECKING([for Perl embed LDOPTS])
    PERL_LDOPTS="$($PERL -MExtUtils::Embed -e 'ldopts')"
    AC_MSG_RESULT([$PERL_LDOPTS])

    dnl XXX Could check these actually work.
])
AM_CONDITIONAL([HAVE_PERL],[test "x$enable_perl" != "xno" && test "x$PERL" != "xno"])
AC_SUBST([PERL_ARCHLIB])
AC_SUBST([PERL_CFLAGS])
AC_SUBST([PERL_LDOPTS])

dnl Check for Python 3, for embedding in the python plugin.
AC_PATH_PROGS([PYTHON],[python3 python],[no])
AC_ARG_ENABLE([python],
    [AS_HELP_STRING([--disable-python], [disable Python embed plugin])],
    [],
    [enable_python=yes])
AS_IF([test "x$PYTHON" != "xno" && test "x$enable_python" != "xno"],[
    AC_MSG_CHECKING([version of $PYTHON])
    PYTHON_VERSION_MAJOR=`$PYTHON -c "import sys; print (sys.version_info@<:@0@:>@)"`
    PYTHON_VERSION_MINOR=`$PYTHON -c "import sys; print (sys.version_info@<:@1@:>@)"`
    PYTHON_VERSION="$PYTHON_VERSION_MAJOR.$PYTHON_VERSION_MINOR"
    AS_IF([test -n "$PYTHON_VERSION"],[
        AC_MSG_RESULT([$PYTHON_VERSION])
    ],[
        AC_MSG_NOTICE([Python embed module disabled])
        enable_python=no
    ])

    AC_MSG_CHECKING([Python major version is 3])
    AS_IF([test "x$PYTHON_VERSION_MAJOR" = "x3"],[
        AC_MSG_RESULT([yes])
    ],[
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([Python $PYTHON_VERSION_MAJOR <> 3 is no longer supported.

Python 2 end of life is 2020-01-01 and nbdkit >= 1.16 no longer
supports it.

If you want to use Python 2, you will need to use nbdkit 1.14.])
    ])

    dnl Check for Python CFLAGS, libraries.
    dnl For Python >= 3.8 we have to use python-<VERSION>-embed.pc, see:
    dnl https://docs.python.org/3.8/whatsnew/3.8.html#debug-build-uses-the-same-abi-as-release-build
    dnl The python.pc is called python-<VERSION>.pc on Debian and
    dnl later versions of Fedora, and python.pc on older versions
    dnl of Fedora.
    PKG_CHECK_MODULES([PYTHON], [python-"$PYTHON_VERSION"-embed], [
        AC_SUBST([PYTHON_CFLAGS])
        AC_SUBST([PYTHON_LIBS])
        AC_SUBST([PYTHON_VERSION])
        AC_DEFINE([HAVE_PYTHON],[1],[Python library found at compile time])
    ],[
    PKG_CHECK_MODULES([PYTHON], [python-"$PYTHON_VERSION"], [
        AC_SUBST([PYTHON_CFLAGS])
        AC_SUBST([PYTHON_LIBS])
        AC_SUBST([PYTHON_VERSION])
        AC_DEFINE([HAVE_PYTHON],[1],[Python library found at compile time])
    ],[
    PKG_CHECK_MODULES([PYTHON], [python], [
        AC_SUBST([PYTHON_CFLAGS])
        AC_SUBST([PYTHON_LIBS])
        AC_SUBST([PYTHON_VERSION])
        AC_DEFINE([HAVE_PYTHON],[1],[Python library found at compile time])
    ],[
        AC_MSG_WARN([python $PYTHON_VERSION not found])
        enable_python=no
    ])])])
])
AM_CONDITIONAL([HAVE_PYTHON],[test "x$enable_python" != "xno" && test "x$PYTHON" != "xno"])
AC_SUBST([PYTHON_CFLAGS])
AC_SUBST([PYTHON_LIBS])
AC_SUBST([PYTHON_LDFLAGS])

dnl For the OCaml plugin, you can set OCAMLOPTFLAGS before running
dnl ./configure to specify any extra flags you want to pass to
dnl ocamlopt.
AC_SUBST([OCAMLOPTFLAGS])

dnl Check for OCaml, for embedding in the ocaml plugin.
AC_PROG_OCAML
AC_ARG_ENABLE([ocaml],
    [AS_HELP_STRING([--disable-ocaml], [disable OCaml embed plugin])],
    [],
    [enable_ocaml=yes])
AS_IF([test "x$OCAMLOPT" != "xno" && test "x$enable_ocaml" != "xno"],[
    dnl Check OCaml can create a shared library (see README for details).
    AC_MSG_CHECKING([if $OCAMLOPT can create a shared library])
    echo 'print_endline "test"' > conftest.ml
    AS_IF([$OCAMLOPT $OCAMLOPTFLAGS -output-obj -runtime-variant _pic -o conftest.so conftest.ml >&AS_MESSAGE_LOG_FD 2>&1],[
        AC_MSG_RESULT([yes])
        ocaml_link_shared=yes
    ],[
        AC_MSG_RESULT([no])
    ])
    rm -f conftest.ml conftest.cmi conftest.cmx conftest.so conftest.o
])
AM_CONDITIONAL([HAVE_OCAML],[test "x$OCAMLOPT" != "xno" &&
                             test "x$ocaml_link_shared" = "xyes"])

dnl Check if OCaml has caml_alloc_initialized_string (added 2017).
AS_IF([test "x$OCAMLC" != "xno" && test "x$OCAMLFIND" != "xno" && \
       test "x$enable_ocaml" = "xyes"],[
    AC_MSG_CHECKING([for caml_alloc_initialized_string])
    cat >conftest.c <<'EOF'
#include <caml/alloc.h>
int main () { char *p = (void *) caml_alloc_initialized_string; return 0; }
EOF
    AS_IF([$OCAMLC conftest.c >&AS_MESSAGE_LOG_FD 2>&1],[
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_CAML_ALLOC_INITIALIZED_STRING],[1],
                  [caml_alloc_initialized_string found at compile time.])
    ],[
        AC_MSG_RESULT([no])
    ])
    rm -f conftest.c conftest.o
])

dnl For developing plugins in Rust, optional.
AC_CHECK_PROG([CARGO],[cargo],[cargo],[no])
AC_ARG_ENABLE([rust],
    [AS_HELP_STRING([--disable-rust], [disable Rust plugin])],
    [],
    [enable_rust=yes])
AM_CONDITIONAL([HAVE_RUST],
               [test "x$CARGO" != "xno" && test "x$enable_rust" != "xno"])

dnl Check for Ruby, for embedding in the Ruby plugin.
AC_CHECK_PROG([RUBY],[ruby],[ruby],[no])
AC_ARG_ENABLE([ruby],
    [AS_HELP_STRING([--disable-ruby], [disable Ruby plugin])],
    [],
    [enable_ruby=yes])
AS_IF([test "x$RUBY" != "xno" && test "x$enable_ruby" != "xno"],[
    PKG_CHECK_MODULES([RUBY], [ruby], [
        AC_SUBST([RUBY_CFLAGS])
        AC_SUBST([RUBY_LIBS])
    ],[
        AC_MSG_WARN([ruby not found])
        enable_ruby=no
    ])
])
AM_CONDITIONAL([HAVE_RUBY],[test "x$RUBY" != "xno" &&
                            test "x$enable_ruby" = "xyes"])

dnl Check for Tcl, for embedding in the Tcl plugin.
AC_ARG_ENABLE([tcl],
    [AS_HELP_STRING([--disable-tcl], [disable Tcl plugin])],
    [],
    [enable_tcl=yes])
AS_IF([test "x$enable_tcl" != "xno"],[
    PKG_CHECK_MODULES([TCL], [tcl], [
        AC_SUBST([TCL_CFLAGS])
        AC_SUBST([TCL_LIBS])
    ],[
        AC_MSG_WARN([Tcl not found])
        enable_tcl=no
    ])
])
AM_CONDITIONAL([HAVE_TCL],[test "x$enable_tcl" = "xyes"])

dnl Check for Lua, for embedding in the Lua plugin.
AC_ARG_ENABLE([lua],
    [AS_HELP_STRING([--disable-lua], [disable Lua plugin])],
    [],
    [enable_lua=yes])
AS_IF([test "x$enable_lua" != "xno"],[
    PKG_CHECK_MODULES([LUA], [lua], [
        AC_SUBST([LUA_CFLAGS])
        AC_SUBST([LUA_LIBS])

	dnl Lua 5.1 used by RHEL 7 does not have lua_isinteger.
	old_LIBS="$LIBS"
	LIBS="$LUA_LIBS $LIBS"
	AC_CHECK_FUNCS([lua_isinteger])
	LIBS="$old_LIBS"
    ],[
        AC_MSG_WARN([Lua not found])
        enable_lua=no
    ])
])
AM_CONDITIONAL([HAVE_LUA],[test "x$enable_lua" = "xyes"])

dnl Check for golang.
AC_ARG_ENABLE([golang],
    AS_HELP_STRING([--disable-golang], [disable Go language plugin]),
        [],
        [enable_golang=yes])
AS_IF([test "x$enable_golang" != "xno"],[
    AC_CHECK_PROG([GOLANG],[go],[go],[no])
    AS_IF([test "x$GOLANG" != "xno"],[
        AC_MSG_CHECKING([if $GOLANG is usable])
        AS_IF([$GOLANG run $srcdir/plugins/golang/config-test.go 2>&AS_MESSAGE_LOG_FD],[
            AC_MSG_RESULT([yes])

            # Substitute some golang environment.
            GOOS=`$GOLANG env GOOS`
            GOARCH=`$GOLANG env GOARCH`
            GOROOT=`$GOLANG env GOROOT`
            AC_SUBST([GOOS])
            AC_SUBST([GOARCH])
            AC_SUBST([GOROOT])
        ],[
            AC_MSG_RESULT([no])
            AC_MSG_WARN([golang ($GOLANG) is installed but not usable])
            GOLANG=no
        ])
    ])
],[GOLANG=no])
AM_CONDITIONAL([HAVE_GOLANG],[test "x$GOLANG" != "xno"])

dnl Check for curl (only if you want to compile the curl plugin).
AC_ARG_WITH([curl],
    [AS_HELP_STRING([--without-curl],
                    [disable curl plugin @<:@default=check@:>@])],
    [],
    [with_curl=check])
AS_IF([test "$with_curl" != "no"],[
    PKG_CHECK_MODULES([CURL], [libcurl],[
        AC_SUBST([CURL_CFLAGS])
        AC_SUBST([CURL_LIBS])
        AC_DEFINE([HAVE_CURL],[1],[curl found at compile time.])
        AC_CHECK_DECL([CURLOPT_UNIX_SOCKET_PATH], [
            AC_DEFINE([HAVE_CURLOPT_UNIX_SOCKET_PATH],[1],
                      [CURLOPT_UNIX_SOCKET_PATH found at compile time.])
            ], [], [#include <curl/curl.h>])
    ],
    [AC_MSG_WARN([curl not found, curl plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_CURL],[test "x$CURL_LIBS" != "x"])

dnl Check for libssh (only if you want to compile the ssh plugin).
AC_ARG_WITH([ssh],
    [AS_HELP_STRING([--without-ssh],
                    [disable ssh plugin @<:@default=check@:>@])],
    [],
    [with_ssh=check])
AS_IF([test "$with_ssh" != "no"],[
    PKG_CHECK_MODULES([SSH], [libssh >= 0.8.0],[
        AC_SUBST([SSH_CFLAGS])
        AC_SUBST([SSH_LIBS])
    ],
    [AC_MSG_WARN([libssh not found, ssh plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_SSH],[test "x$SSH_LIBS" != "x"])

dnl Check for genisoimage or mkisofs
dnl (only if you want to compile the iso plugin).
ISOPROG="no"
AC_ARG_WITH([iso],
    [AS_HELP_STRING([--without-iso],
                    [disable iso plugin @<:@default=check@:>@])],
    [],
    [with_iso=check])
AS_IF([test "$with_iso" != "no"],[
    AC_CHECK_PROG([GENISOIMAGE],[genisoimage],[genisoimage],[no])
    AC_CHECK_PROG([MKISOFS],[mkisofs],[mkisofs],[no])
    AS_IF([test "x$GENISOIMAGE" != "xno"],[
        ISOPROG="$GENISOIMAGE"
    ],[
        AS_IF([test "x$MKISOFS" != "xno"],[
            ISOPROG="$MKISOFS"
        ])
    ])
    AS_IF([test "x$ISOPROG" != "xno"],[
        AC_DEFINE_UNQUOTED([ISOPROG],["$ISOPROG"],
                           [Program used by iso plugin to make ISOs.])
    ])
])
AC_SUBST([ISOPROG])
AM_CONDITIONAL([HAVE_ISO],[test "x$ISOPROG" != "xno"])

dnl Check for libvirt (only if you want to compile the libvirt plugin).
AC_ARG_WITH([libvirt],
    [AS_HELP_STRING([--without-libvirt],
                    [disable libvirt plugin @<:@default=check@:>@])],
    [],
    [with_libvirt=check])
AS_IF([test "$with_libvirt" != "no"],[
    PKG_CHECK_MODULES([LIBVIRT], [libvirt],[
        AC_SUBST([LIBVIRT_CFLAGS])
        AC_SUBST([LIBVIRT_LIBS])
        AC_DEFINE([HAVE_LIBVIRT],[1],[libvirt found at compile time.])
    ],
    [AC_MSG_WARN([libvirt not found, libvirt plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_LIBVIRT],[test "x$LIBVIRT_LIBS" != "x"])

dnl Check for zlib (only if you want to compile the gzip plugin).
AC_ARG_WITH([zlib],
    [AS_HELP_STRING([--without-zlib],
                    [disable gzip plugin @<:@default=check@:>@])],
    [],
    [with_zlib=check])
AS_IF([test "$with_zlib" != "no"],[
    PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.3.5],[
        AC_SUBST([ZLIB_CFLAGS])
        AC_SUBST([ZLIB_LIBS])
        AC_DEFINE([HAVE_ZLIB],[1],[zlib found at compile time.])
    ],
    [AC_MSG_WARN([zlib >= 1.2.3.5 not found, gzip plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_ZLIB],[test "x$ZLIB_LIBS" != "x"])

dnl Check for libnbd (only if you want to compile the nbd plugin).
AC_ARG_WITH([libnbd],
    [AS_HELP_STRING([--without-libnbd],
                    [disable nbd plugin @<:@default=check@:>@])],
    [],
    [with_libnbd=check])
AS_IF([test "$with_libnbd" != "no"],[
    PKG_CHECK_MODULES([LIBNBD], [libnbd >= 0.9.8],[
        AC_SUBST([LIBNBD_CFLAGS])
        AC_SUBST([LIBNBD_LIBS])
        AC_DEFINE([HAVE_LIBNBD],[1],[libnbd found at compile time.])
    ],
    [AC_MSG_WARN([libnbd >= 0.9.8 not found, nbd plugin will be crippled])])
])

dnl For backwards compatibilty, we have a second way to disable the nbd plugin.
AC_ARG_ENABLE([nbd-plugin],
    [AS_HELP_STRING([--disable-nbd-plugin], [disable nbd plugin (deprecated, use --without-libnbd)])],
    [],
    [enable_nbd_plugin=yes])
AM_CONDITIONAL([HAVE_LIBNBD],
  [test "x$LIBNBD_LIBS" != "x" && test "x$enable_nbd_plugin" = "xyes"])

dnl Check for liblzma (only if you want to compile the xz filter).
AC_ARG_WITH([liblzma],
    [AS_HELP_STRING([--without-liblzma],
                    [disable xz filter @<:@default=check@:>@])],
    [],
    [with_liblzma=check])
AS_IF([test "$with_liblzma" != "no"],[
    PKG_CHECK_MODULES([LIBLZMA], [liblzma],[
        AC_SUBST([LIBLZMA_CFLAGS])
        AC_SUBST([LIBLZMA_LIBS])
        AC_DEFINE([HAVE_LIBLZMA],[1],[liblzma found at compile time.])
    ],
    [AC_MSG_WARN([liblzma not found, xz filter will be disabled])])
])
AM_CONDITIONAL([HAVE_LIBLZMA],[test "x$LIBLZMA_LIBS" != "x"])

dnl Check for libguestfs (only for the guestfs plugin and the test suite).
AC_ARG_WITH([libguestfs],
    [AS_HELP_STRING([--without-libguestfs],
                    [disable guestfs plugin and tests @<:@default=check@:>@])],
    [],
    [with_libguestfs=check])
AS_IF([test "$with_libguestfs" != "no"],[
    PKG_CHECK_MODULES([LIBGUESTFS], [libguestfs],[
        # Although the library was found, we want to make sure it supports nbd
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <guestfs.h>
        ]], [[
#ifndef GUESTFS_ADD_DRIVE_OPTS_PROTOCOL
#error unsupported
#endif
        ]])], [
        AC_SUBST([LIBGUESTFS_CFLAGS])
        AC_SUBST([LIBGUESTFS_LIBS])
        AC_DEFINE([HAVE_LIBGUESTFS],[1],[libguestfs found at compile time.])
        ],[
        LIBGUESTFS_LIBS=
        AC_MSG_WARN([libguestfs too old, guestfs plugin and tests will be disabled])])
    ],
    [AC_MSG_WARN([libguestfs not found, guestfs plugin and tests will be disabled])])
])
AM_CONDITIONAL([HAVE_LIBGUESTFS],[test "x$LIBGUESTFS_LIBS" != "x"])

dnl Check for guestfish (only needed for some of the tests).
AC_CHECK_PROG([GUESTFISH], [guestfish], [guestfish], [no])
AM_CONDITIONAL([HAVE_GUESTFISH], [test "x$GUESTFISH" != "xno"])

dnl Check for ext2fs and com_err, for the ext2 filter and plugin.
AC_ARG_WITH([ext2],
    [AS_HELP_STRING([--without-ext2],
                    [disable ext2 filter @<:@default=check@:>@])],
    [],
    [with_ext2=check])
AS_IF([test "$with_ext2" != "no"], [
    PKG_CHECK_MODULES([EXT2FS], [ext2fs], [
        AC_SUBST([EXT2FS_CFLAGS])
        AC_SUBST([EXT2FS_LIBS])
        AC_DEFINE([HAVE_EXT2FS],[1],[ext2fs found at compile time.])
    ],
    [AC_MSG_WARN([ext2fs not found, ext2 plugin will be disabled])])
    PKG_CHECK_MODULES([COM_ERR], [com_err], [
        AC_SUBST([COM_ERR_CFLAGS])
        AC_SUBST([COM_ERR_LIBS])
        AC_DEFINE([HAVE_COM_ERR],[1],[com_err found at compile time.])
    ],
    [AC_MSG_WARN([com_err not found, ext2 plugin will be disabled])])

    AC_CHECK_MEMBERS([struct struct_io_manager.cache_readahead,
                      struct struct_io_manager.zeroout])
])
AM_CONDITIONAL([HAVE_EXT2],
               [test "x$EXT2FS_LIBS" != "x" && test "x$COM_ERR_LIBS" != "x"])

dnl Check if the user wants to disable VDDK support.
dnl See plugins/vddk/README.VDDK.
AC_ARG_ENABLE([vddk],
    [AS_HELP_STRING([--disable-vddk],
                    [disable VMware VDDK plugin])],
    [],
    [
     dnl While VDDK was available on i686 in 5.1.1, we only support 5.5.5
     dnl and newer, which is supported only on x86-64.  Don't compile on
     dnl other platforms.
     AC_MSG_CHECKING([if the host CPU is compatible with VDDK])
     AS_IF([test "$host_cpu" = "x86_64"],[
        AC_MSG_RESULT([yes ($host_cpu)])
        enable_vddk=yes
     ],[
        AC_MSG_RESULT([no ($host_cpu)])
        enable_vddk=no
     ])
    ])
AM_CONDITIONAL([HAVE_VDDK], [test "x$enable_vddk" = "xyes"])

dnl Expose version information to the public headers
[NBDKIT_]VERSION_MAJOR=NBDKIT_VERSION_MAJOR
[NBDKIT_]VERSION_MINOR=NBDKIT_VERSION_MINOR
[NBDKIT_]VERSION_MICRO=NBDKIT_VERSION_MICRO
AC_SUBST([NBDKIT_VERSION_MAJOR])
AC_SUBST([NBDKIT_VERSION_MINOR])
AC_SUBST([NBDKIT_VERSION_MICRO])

dnl Produce output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([podwrapper.pl],
                [chmod +x,-w podwrapper.pl])
AC_CONFIG_FILES([common/protocol/generate-protostrings.sh],
                [chmod +x,-w common/protocol/generate-protostrings.sh])
AC_CONFIG_FILES([Makefile
                 bash/Makefile
                 common/bitmap/Makefile
                 common/gpt/Makefile
                 common/include/Makefile
                 common/protocol/Makefile
                 common/regions/Makefile
                 common/sparse/Makefile
                 common/utils/Makefile
                 docs/Makefile
                 include/Makefile
                 include/nbdkit-version.h
                 plugins/Makefile
                 plugins/curl/Makefile
                 plugins/data/Makefile
                 plugins/eval/Makefile
                 plugins/example1/Makefile
                 plugins/example2/Makefile
                 plugins/example3/Makefile
                 plugins/example4/Makefile
                 plugins/ext2/Makefile
                 plugins/file/Makefile
                 plugins/floppy/Makefile
                 plugins/full/Makefile
                 plugins/golang/Makefile
                 plugins/guestfs/Makefile
                 plugins/gzip/Makefile
                 plugins/info/Makefile
                 plugins/iso/Makefile
                 plugins/libvirt/Makefile
                 plugins/linuxdisk/Makefile
                 plugins/lua/Makefile
                 plugins/memory/Makefile
                 plugins/nbd/Makefile
                 plugins/null/Makefile
                 plugins/ocaml/Makefile
                 plugins/partitioning/Makefile
                 plugins/pattern/Makefile
                 plugins/perl/Makefile
                 plugins/python/Makefile
                 plugins/random/Makefile
                 plugins/ruby/Makefile
                 plugins/rust/Cargo.toml
                 plugins/rust/Makefile
                 plugins/sh/Makefile
                 plugins/ssh/Makefile
                 plugins/split/Makefile
                 plugins/streaming/Makefile
                 plugins/tar/Makefile
                 plugins/tcl/Makefile
                 plugins/tmpdisk/Makefile
                 plugins/vddk/Makefile
                 plugins/zero/Makefile
                 filters/Makefile
                 filters/blocksize/Makefile
                 filters/cache/Makefile
                 filters/cacheextents/Makefile
                 filters/cow/Makefile
                 filters/ddrescue/Makefile
                 filters/delay/Makefile
                 filters/error/Makefile
                 filters/exitlast/Makefile
                 filters/ext2/Makefile
                 filters/extentlist/Makefile
                 filters/fua/Makefile
                 filters/ip/Makefile
                 filters/limit/Makefile
                 filters/log/Makefile
                 filters/nocache/Makefile
                 filters/noextents/Makefile
                 filters/nofilter/Makefile
                 filters/noparallel/Makefile
                 filters/nozero/Makefile
                 filters/offset/Makefile
                 filters/partition/Makefile
                 filters/rate/Makefile
                 filters/readahead/Makefile
                 filters/retry/Makefile
                 filters/stats/Makefile
                 filters/truncate/Makefile
                 filters/xz/Makefile
                 fuzzing/Makefile
                 server/local/nbdkit.pc
                 server/Makefile
                 server/nbdkit.pc
                 tests/functions.sh
                 tests/Makefile
                 valgrind/Makefile])

AC_OUTPUT

dnl Summary.
echo
echo
echo "----------------------------------------------------------------------"
echo "Thank you for downloading $PACKAGE_STRING"
echo
echo "This is how we have configured the optional components for you today:"
echo

feature ()
{
    printf %s "    $1"
    shift
    if $@; then echo "yes"; else echo "no"; fi
}

echo "Optional server features:"
echo
feature "bash-completion ........................ " \
        test "x$HAVE_BASH_COMPLETION_TRUE" = "x"
feature "manual pages ........................... " \
        test "x$HAVE_POD_TRUE" = "x"
feature "SELinux ................................ " \
        test "x$LIBSELINUX_LIBS" != "x"
feature "TLS .................................... " \
        test "x$GNUTLS_LIBS" != "x"

echo
echo "Optional plugins:"
echo
feature "curl ................................... " \
        test "x$HAVE_CURL_TRUE" = "x"
feature "example4 ............................... " \
        test "x$HAVE_PERL_TRUE" = "x"
feature "ext2 ................................... " \
        test "x$HAVE_EXT2_TRUE" = "x"
feature "floppy ................................. " \
        test "x$HAVE_ICONV_TRUE" = "x"
feature "guestfs ................................ " \
        test "x$HAVE_LIBGUESTFS_TRUE" = "x"
feature "gzip ................................... " \
        test "x$HAVE_ZLIB_TRUE" = "x"
feature "iso .................................... " \
        test "x$HAVE_ISO_TRUE" = "x"
feature "libvirt ................................ " \
        test "x$HAVE_LIBVIRT_TRUE" = "x"
feature "nbd .................................... " \
        test "x$HAVE_LIBNBD_TRUE" = "x"
feature "ssh .................................... " \
        test "x$HAVE_SSH_TRUE" = "x"
feature "tar .................................... " \
        test "x$HAVE_PERL_TRUE" = "x"
feature "vddk ................................... " \
        test "x$HAVE_VDDK_TRUE" = "x"


echo
echo "Languages:"
echo
feature "go ..................................... " \
        test "x$HAVE_GOLANG_TRUE" = "x"
feature "lua .................................... " \
        test "x$HAVE_LUA_TRUE" = "x"
feature "ocaml .................................. " \
        test "x$HAVE_OCAML_TRUE" = "x"
feature "perl ................................... " \
        test "x$HAVE_PERL_TRUE" = "x"
feature "python ................................. " \
        test "x$HAVE_PYTHON_TRUE" = "x"
feature "ruby ................................... " \
        test "x$HAVE_RUBY_TRUE" = "x"
feature "rust ................................... " \
        test "x$HAVE_RUST_TRUE" = "x"
feature "tcl .................................... " \
        test "x$HAVE_TCL_TRUE" = "x"


echo
echo "Optional filters:"
echo
feature "ext2 ................................... " \
        test "x$HAVE_EXT2_TRUE" = "x"
feature "xz ..................................... " \
        test "x$HAVE_LIBLZMA_TRUE" = "x"

echo
echo "If any optional component is configured ‘no’ when you expected ‘yes’"
echo "then you should check the preceding messages and README."
echo
echo "Please report bugs back to the mailing list:"
echo "http://www.redhat.com/mailman/listinfo/libguestfs"
echo
echo "Next you should type 'make' to build the package,"
echo "then 'make check' to run the tests."
