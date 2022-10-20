# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    apk update
    apk upgrade
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
        py3-boto3 \
        py3-flake8 \
        python3 \
        python3-dev \
        qemu-img \
        rust \
        rust-clippy \
        sfdisk \
        socat \
        tcl \
        xz \
        xz-dev \
        zlib-dev \
        zstd
    apk list | sort > /packages.txt
    mkdir -p /usr/libexec/ccache-wrappers
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc
}

export CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
export LANG="en_US.UTF-8"
export MAKE="/usr/bin/make"
export PYTHON="/usr/bin/python3"
