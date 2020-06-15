#! /bin/sh
# cargo test wrapper suitable for invocation from automake-generated makefiles
cargo test && cargo test --release
