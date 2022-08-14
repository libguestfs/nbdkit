# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    zypper update -y
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
           glibc-locale \
           go \
           gzip \
           iproute2 \
           jq \
           libcurl-devel \
           libgnutls-devel \
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
           mkisofs \
           ocaml \
           perl \
           perl-Pod-Simple \
           perl-base \
           pkgconfig \
           python3-base \
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
