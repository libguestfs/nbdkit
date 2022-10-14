# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM docker.io/library/alpine:3.15

RUN apk update && \
    apk upgrade && \
    apk add \
        autoconf \
        automake \
        bash \
        bash-completion \
        busybox \
        ca-certificates \
        cargo \
        ccache \
        cdrkit \
        clang \
        curl-dev \
        e2fsprogs \
        expect \
        g++ \
        gcc \
        git \
        gnutls-dev \
        go \
        gzip \
        hexdump \
        iproute2 \
        jq \
        libselinux-dev \
        libssh-dev \
        libtool \
        libtorrent \
        libvirt-dev \
        lua5.4 \
        make \
        ocaml \
        perl \
        perl-dev \
        pkgconf \
        py3-flake8 \
        py3-pip \
        py3-setuptools \
        py3-wheel \
        python3 \
        python3-dev \
        qemu-img \
        rust \
        sfdisk \
        socat \
        tcl \
        xz \
        xz-dev \
        zlib-dev \
        zstd && \
    apk list | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc

RUN /usr/bin/pip3 install boto3

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV PYTHON "/usr/bin/python3"
