# nbdkit
# Copyright (C) 2013-2018 Red Hat Inc.
# All rights reserved.
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

AC_INIT([nbdkit], [1.3.4])
AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AC_USE_SYSTEM_EXTENSIONS],[],
         [m4_define([AC_USE_SYSTEM_EXTENSIONS],[])])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

dnl NB: Do not [quote] this parameter.
AM_INIT_AUTOMAKE(foreign)
AC_PROG_LIBTOOL
LT_INIT

AC_PROG_SED

dnl Check for basic C environment.
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_CPP

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
    WARNINGS_CFLAGS="-Wall -Werror"
    AC_SUBST([WARNINGS_CFLAGS])
fi

dnl Check if libc has program_invocation_short_name.
AC_CHECK_DECLS([program_invocation_short_name], [], [], [#include <errno.h>])

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
dnl restore CFLAGS
CFLAGS="${acx_nbdkit_save_CFLAGS}"

dnl Check for other headers, all optional.
AC_CHECK_HEADERS([selinux/selinux.h sys/prctl.h])

dnl Check support for setsockcreatecon_raw (part of SELinux).
AC_CHECK_LIB([selinux], [setsockcreatecon_raw], [], [:])

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
    AC_ARG_WITH([tls-priority],[
        AS_HELP_STRING([--with-tls-priority],
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
	gnutls_certificate_set_known_dh_params \
	gnutls_session_set_verify_cert])
    LIBS="$old_LIBS"
])

dnl Check for valgrind.
AC_CHECK_PROG([VALGRIND],[valgrind],[valgrind],[no])

dnl If valgrind headers are available (optional).
dnl Since this is only useful for developers, you have to enable
dnl it explicitly using --enable-valgrind.
AC_ARG_ENABLE([valgrind],
    AS_HELP_STRING([--enable-valgrind], [enable Valgrind extensions, for developers]),
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

dnl Bash completion.
PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0], [
    bash_completion=yes
    AC_MSG_CHECKING([for bash-completions directory])
    PKG_CHECK_VAR(bashcompdir, [bash-completion], [completionsdir], ,
        bashcompdir="${sysconfdir}/bash_completion.d")
    AC_MSG_RESULT([$bashcompdir])
    AC_SUBST([bashcompdir])
],[
    bash_completion=no
    AC_MSG_WARN([bash-completion not installed])
])
AM_CONDITIONAL([HAVE_BASH_COMPLETION],[test "x$bash_completion" = "xyes"])

dnl Check for Perl POD.
AC_CHECK_PROG([POD2MAN], [pod2man], [pod2man], [no])
AS_IF([test "x$POD2MAN" != "xno"],[
    POD2MAN_ARGS="--center=nbdkit --release=nbdkit"
    # Check whether pod2man supports --utf8, and if so add it to $POD2MAN_ARGS.
    AC_MSG_CHECKING([if $POD2MAN supports --utf8])
    AS_IF([$POD2MAN --help 2>&1 | grep -s -q -- --utf8],[
        AC_MSG_RESULT([yes])
	POD2MAN_ARGS="$POD2MAN_ARGS --utf8"
    ],[
        AC_MSG_RESULT([no])
    ])
    AC_SUBST([POD2MAN_ARGS])
])
AM_CONDITIONAL([HAVE_POD2MAN], [test "x$POD2MAN" != "xno"])

AC_ARG_ENABLE([plugins],
    [AS_HELP_STRING([--disable-plugins], [disable all bundled plugins and filters])])
AM_CONDITIONAL([HAVE_PLUGINS], [test "x$enable_plugins" != "xno"])

dnl Check for Perl, for embedding in the perl plugin.
AC_CHECK_PROG([PERL],[perl],[perl],[no])
AC_ARG_ENABLE([perl],
    AS_HELP_STRING([--disable-perl], [disable Perl embed plugin]),
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

dnl Check for Python, for embedding in the python plugin.
AC_CHECK_PROG([PYTHON],[python],[python],[no])
AC_ARG_ENABLE([python],
    AS_HELP_STRING([--disable-python], [disable Python embed plugin]),
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

    dnl Check for Python CFLAGS, libraries.
    dnl On Debian: python-X.Y.pc
    PKG_CHECK_MODULES([PYTHON], [python-"$PYTHON_VERSION"], [
        AC_SUBST([PYTHON_CFLAGS])
        AC_SUBST([PYTHON_LIBS])
        AC_SUBST([PYTHON_VERSION])
        AC_DEFINE([HAVE_PYTHON],[1],[Python library found at compile time])
    ],[
        dnl On Fedora: python.pc
        PKG_CHECK_MODULES([PYTHON], [python], [
            AC_SUBST([PYTHON_CFLAGS])
            AC_SUBST([PYTHON_LIBS])
            AC_SUBST([PYTHON_VERSION])
            AC_DEFINE([HAVE_PYTHON],[1],[Python library found at compile time])
        ],[
            AC_MSG_WARN([python $PYTHON_VERSION not found])
            enable_python=no
        ])
    ])

    dnl Check for various functions needed by the bindings.
    old_LIBS="$LIBS"

    PYTHON_BLDLIBRARY=`$PYTHON -c "import distutils.sysconfig; \
        print (distutils.sysconfig.get_config_var('BLDLIBRARY'))"`
    AC_CHECK_LIB([c],[PyString_FromString],
                 [AC_DEFINE([HAVE_PYSTRING_FROMSTRING],1,
                            [Found PyString_FromString in libpython.])],
                 [],[$PYTHON_BLDLIBRARY])
    AC_CHECK_LIB([c],[PyString_AsString],
                 [AC_DEFINE([HAVE_PYSTRING_ASSTRING],1,
                            [Found PyString_AsString in libpython.])],
                 [],[$PYTHON_BLDLIBRARY])
    AC_CHECK_LIB([c],[PyUnicode_AsUTF8],
                 [AC_DEFINE([HAVE_PYUNICODE_ASUTF8],1,
                            [Found PyUnicode_AsUTF8 in libpython.])],
                 [],[$PYTHON_BLDLIBRARY])

    LIBS="$old_LIBS"

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
    AS_HELP_STRING([--disable-ocaml], [disable OCaml embed plugin]),
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

dnl Check for Ruby, for embedding in the Ruby plugin.
AC_CHECK_PROG([RUBY],[ruby],[ruby],[no])
AC_ARG_ENABLE([ruby],
    AS_HELP_STRING([--disable-ruby], [disable Ruby plugin]),
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

dnl Check for curl (only if you want to compile the curl plugin).
AC_ARG_WITH([curl],[
    AS_HELP_STRING([--without-curl],
                   [disable curl plugin @<:@default=check@:>@])],
    [],
    [with_curl=check])
AS_IF([test "$with_curl" != "no"],[
    PKG_CHECK_MODULES([CURL], [libcurl],[
        AC_SUBST([CURL_CFLAGS])
        AC_SUBST([CURL_LIBS])
        AC_DEFINE([HAVE_CURL],[1],[curl found at compile time.])
    ],
    [AC_MSG_WARN([curl not found, curl plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_CURL],[test "x$CURL_LIBS" != "x"])

dnl Check for libvirt (only if you want to compile the libvirt plugin).
AC_ARG_WITH([libvirt],[
    AS_HELP_STRING([--without-libvirt],
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
AC_ARG_WITH([zlib],[
    AS_HELP_STRING([--without-zlib],
                   [disable gzip plugin @<:@default=check@:>@])],
    [],
    [with_zlib=check])
AS_IF([test "$with_zlib" != "no"],[
    PKG_CHECK_MODULES([ZLIB], [zlib],[
        AC_SUBST([ZLIB_CFLAGS])
        AC_SUBST([ZLIB_LIBS])
        AC_DEFINE([HAVE_ZLIB],[1],[zlib found at compile time.])
    ],
    [AC_MSG_WARN([zlib not found, gzip plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_ZLIB],[test "x$ZLIB_LIBS" != "x"])

dnl Check for liblzma (only if you want to compile the xz plugin).
AC_ARG_WITH([liblzma],[
    AS_HELP_STRING([--without-liblzma],
                   [disable xz plugin @<:@default=check@:>@])],
    [],
    [with_liblzma=check])
AS_IF([test "$with_liblzma" != "no"],[
    PKG_CHECK_MODULES([LIBLZMA], [liblzma],[
        AC_SUBST([LIBLZMA_CFLAGS])
        AC_SUBST([LIBLZMA_LIBS])
        AC_DEFINE([HAVE_LIBLZMA],[1],[liblzma found at compile time.])
    ],
    [AC_MSG_WARN([liblzma not found, xz plugin will be disabled])])
])
AM_CONDITIONAL([HAVE_LIBLZMA],[test "x$LIBLZMA_LIBS" != "x"])

dnl Check for libguestfs (only for the guestfs plugin and the test suite).
AC_ARG_WITH([libguestfs],[
    AS_HELP_STRING([--without-libguestfs],
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

dnl Check for ext2fs and com_err, for the ext2 plugin.
AC_ARG_WITH([ext2],[
    AS_HELP_STRING([--without-ext2],
                   [disable ext2 plugin @<:@default=check@:>@])],
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
])
AM_CONDITIONAL([HAVE_EXT2],
               [test "x$EXT2FS_LIBS" != "x" && test "x$COM_ERR_LIBS" != "x"])

dnl See plugins/vddk/README.VDDK.
AC_CHECK_SIZEOF([size_t])
AS_IF([test "x$ac_cv_sizeof_size_t" = "x4"],[bits=32],[bits=64])
AC_ARG_WITH([vddk],[
    AS_HELP_STRING([--with-vddk],
                   [enable VMware VDDK plugin @<:@default=no@:>@])],
    [],
    [with_vddk=no])
AS_IF([test "$with_vddk" = "yes"],[
    VDDK_CFLAGS=
    VDDK_LIBS="-lvixDiskLib"
    # XXX Warning: stupid VMware API.
    VDDK_LIBDIR="$libdir/vmware-vix-disklib"
    AC_MSG_NOTICE([VDDK plugin enabled from $VDDK_LIBDIR])
    ],[
    AS_IF([test "$with_vddk" != "no"], [
        VDDK_CFLAGS="-I$with_vddk/include"
        VDDK_LIBS="-L$with_vddk/lib$bits -lvixDiskLib"
        VDDK_LIBDIR="$with_vddk"
        AC_MSG_NOTICE([VDDK plugin enabled from $with_vddk])
        ],
        [AC_MSG_NOTICE([VDDK plugin disabled])
    ])
])

dnl If the VDDK plugin was enabled, compile and link a test program to make
dnl sure the library really works.
AS_IF([test "x$VDDK_LIBS" != "x"],[
    # Save CFLAGS etc while we do this test.
    acx_nbdkit_save_CFLAGS="${CFLAGS}"
    acx_nbdkit_save_LIBS="${LIBS}"
    CFLAGS="$CFLAGS $VDDK_CFLAGS"
    LIBS="$VDDK_LIBS $LIBS"

    AC_MSG_CHECKING([if we can link to VDDK])
    AC_LINK_IFELSE([
        AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdint.h>
#include <vixDiskLib.h>

int
main ()
{
    VixDiskLib_Exit ();
}
]])
    ],[
        AC_MSG_RESULT([yes])
    ],[
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([could not link to VDDK, see ‘config.log’ for more information])
    ])

    dnl Check for optional fields in VixDiskLibConnectParams struct.
    AC_CHECK_MEMBERS([VixDiskLibConnectParams.nfcHostPort, VixDiskLibConnectParams.vimApiVer],
                    [], [], [[
#include <stdio.h>
#include <stdint.h>
#include <vixDiskLib.h>
]])

    dnl Restore CFLAGS, etc.
    CFLAGS="${acx_nbdkit_save_CFLAGS}"
    LIBS="${acx_nbdkit_save_LIBS}"
])

AC_SUBST([VDDK_CFLAGS])
AC_SUBST([VDDK_LIBS])
AC_DEFINE_UNQUOTED([VDDK_LIBDIR],["$VDDK_LIBDIR"],[VDDK 'libDir'.])
AM_CONDITIONAL([HAVE_VDDK],[test "x$VDDK_LIBS" != "x"])

dnl Produce output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([nbdkit],
                [chmod +x,-w nbdkit])
AC_CONFIG_FILES([Makefile
                 bash/Makefile
                 docs/Makefile
                 include/Makefile
                 plugins/Makefile
                 plugins/curl/Makefile
                 plugins/example1/Makefile
                 plugins/example2/Makefile
                 plugins/example3/Makefile
                 plugins/example4/Makefile
                 plugins/ext2/Makefile
                 plugins/file/Makefile
                 plugins/guestfs/Makefile
                 plugins/gzip/Makefile
                 plugins/libvirt/Makefile
                 plugins/memory/Makefile
                 plugins/nbd/Makefile
                 plugins/null/Makefile
                 plugins/ocaml/Makefile
                 plugins/perl/Makefile
                 plugins/python/Makefile
                 plugins/random/Makefile
                 plugins/ruby/Makefile
                 plugins/split/Makefile
                 plugins/streaming/Makefile
                 plugins/tar/Makefile
                 plugins/vddk/Makefile
                 plugins/xz/Makefile
                 plugins/zero/Makefile
                 filters/Makefile
                 filters/blocksize/Makefile
                 filters/cache/Makefile
                 filters/cow/Makefile
                 filters/delay/Makefile
                 filters/fua/Makefile
                 filters/log/Makefile
                 filters/nozero/Makefile
                 filters/offset/Makefile
                 filters/partition/Makefile
                 src/Makefile
                 src/nbdkit.pc
                 tests/Makefile])

AC_OUTPUT
