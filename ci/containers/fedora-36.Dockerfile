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
               clang \
               clippy \
               e2fsprogs \
               expect \
               gcc \
               gcc-c++ \
               genisoimage \
               git \
               glibc-langpack-en \
               gnutls-devel \
               golang \
               gzip \
               iproute \
               jq \
               libcurl-devel \
               libguestfs-devel \
               libnbd-devel \
               libselinux-devel \
               libssh-devel \
               libtool \
               libtorrent-devel \
               libvirt-devel \
               libzstd-devel \
               lua-devel \
               make \
               ocaml \
               perl-ExtUtils-Embed \
               perl-Pod-Simple \
               perl-base \
               perl-devel \
               perl-podlators \
               pkgconfig \
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
               xz \
               xz-devel \
               zlib-devel && \
    nosync dnf autoremove -y && \
    nosync dnf clean all -y && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV PYTHON "/usr/bin/python3"
