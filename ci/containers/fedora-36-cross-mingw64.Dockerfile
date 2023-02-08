# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM registry.fedoraproject.org/fedora:36

RUN dnf install -y nosync && \
    echo -e '#!/bin/sh\n\
if test -d /usr/lib64\n\
then\n\
    export LD_PRELOAD=/usr/lib64/nosync/nosync.so\n\
else\n\
    export LD_PRELOAD=/usr/lib/nosync/nosync.so\n\
fi\n\
exec "$@"' > /usr/bin/nosync && \
    chmod +x /usr/bin/nosync && \
    nosync dnf update -y && \
    nosync dnf install -y \
               autoconf \
               automake \
               bash \
               bash-completion \
               ca-certificates \
               cargo \
               ccache \
               clippy \
               e2fsprogs \
               expect \
               genisoimage \
               git \
               glibc-langpack-en \
               golang \
               gzip \
               iproute \
               jq \
               libnbd-devel \
               libtool \
               libtorrent-devel \
               libzstd-devel \
               lua-devel \
               make \
               ocaml \
               perl-ExtUtils-Embed \
               perl-Pod-Simple \
               perl-base \
               perl-devel \
               perl-podlators \
               python3 \
               python3-boto3 \
               python3-devel \
               python3-flake8 \
               python3-libnbd \
               qemu-img \
               rust \
               socat \
               tcl-devel \
               util-linux \
               xorriso \
               xz && \
    nosync dnf autoremove -y && \
    nosync dnf clean all -y

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV PYTHON "/usr/bin/python3"

RUN nosync dnf install -y \
               mingw64-curl \
               mingw64-gcc \
               mingw64-gcc-c++ \
               mingw64-gnutls \
               mingw64-libvirt \
               mingw64-pkg-config && \
    nosync dnf clean all -y && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/x86_64-w64-mingw32-c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/x86_64-w64-mingw32-cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/x86_64-w64-mingw32-g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/x86_64-w64-mingw32-gcc

ENV ABI "x86_64-w64-mingw32"
ENV CONFIGURE_OPTS "--host=x86_64-w64-mingw32"
