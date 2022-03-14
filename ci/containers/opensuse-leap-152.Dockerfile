# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM registry.opensuse.org/opensuse/leap:15.2

RUN zypper update -y && \
    zypper install -y \
           autoconf \
           automake \
           bash \
           bash-completion \
           ca-certificates \
           cargo \
           ccache \
           clang \
           e2fsprogs \
           expect \
           gcc \
           gcc-c++ \
           git \
           glibc \
           glibc-devel \
           glibc-locale \
           go \
           gzip \
           iproute2 \
           jq \
           kernel-default \
           kernel-headers \
           libcom_err-devel \
           libcurl-devel \
           libgnutls-devel \
           libguestfs-devel \
           libselinux-devel \
           libssh-devel \
           libtool \
           libtorrent-devel \
           libvirt-devel \
           libzstd-devel \
           lua-devel \
           make \
           mkisofs \
           ocaml \
           openssh \
           perl \
           perl-Pod-Simple \
           perl-base \
           pkgconfig \
           podman \
           python3 \
           python3-boto3 \
           python3-devel \
           python3-flake8 \
           qemu-tools \
           rust \
           socat \
           tcl-devel \
           util-linux \
           xorriso \
           xz \
           xz-devel \
           zlib-devel && \
    zypper clean --all && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc

ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV PYTHON "/usr/bin/python3"
ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
