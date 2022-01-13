#! /bin/sh
# cargo test wrapper suitable for invocation from automake-generated makefiles

set -e

export RUSTFLAGS=-Dwarnings
export RUSTDOCFLAGS=-Dwarnings

cargo test --all-features
cargo test --all-features --release

if type cargo-doc >/dev/null 2>/dev/null
then
   cargo doc --no-deps
fi

if type cargo-clippy >/dev/null 2>/dev/null
then
    cargo clippy --all-features --all-targets -- -D warnings
fi
