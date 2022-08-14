# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    dnf distro-sync -y
    dnf install 'dnf-command(config-manager)' -y
    dnf config-manager --set-enabled -y powertools
    dnf install -y centos-release-advanced-virtualization
    dnf install -y epel-release
    dnf install -y epel-next-release
    dnf install -y \
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
        perl \
        perl-ExtUtils-Embed \
        perl-Pod-Simple \
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
        zlib-devel
    rpm -qa | sort > /packages.txt
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
