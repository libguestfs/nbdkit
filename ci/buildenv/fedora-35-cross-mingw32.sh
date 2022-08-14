# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    dnf update -y
    dnf install -y \
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
        xz
    dnf install -y \
        mingw32-curl \
        mingw32-gcc \
        mingw32-gcc-c++ \
        mingw32-gnutls \
        mingw32-libvirt \
        mingw32-pkg-config
    rpm -qa | sort > /packages.txt
    mkdir -p /usr/libexec/ccache-wrappers
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-c++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-cc
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-g++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-gcc
}

export CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
export LANG="en_US.UTF-8"
export MAKE="/usr/bin/make"
export PYTHON="/usr/bin/python3"

export ABI="i686-w64-mingw32"
export CONFIGURE_OPTS="--hosti686-w64-mingw32"
