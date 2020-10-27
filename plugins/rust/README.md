# NBDKit

Rust bindings to the Network Block Device Kit library.

[![Crates.io](https://img.shields.io/crates/v/nbdkit.svg)](https://crates.io/crates/nbdkit)
[![Documentation](https://docs.rs/nbdkit/badge.svg)](https://docs.rs/nbdkit)
[![Build Status](https://api.cirrus-ci.com/github/libguestfs/nbdkit.svg)](https://cirrus-ci.com/github/libguestfs/nbdkit)

## Overview

NBDKit is a framework for developing Network Block Device servers.  Most of
the logic lives in the C code, but there are plugins to implement a server
in another language, such as Rust.

## Usage

To create an NBD server in Rust, you must implement the `nbdkit::Server`
trait, and register it with `nbdkit::plugin!`, like this:

```toml
[dependencies]
nbdkit = "0.1.0"
```

```rust
use nbdkit::*;

#[derive(Default)]
struct MyPlugin {
    // ...
}

impl Server for MyPlugin {
    // ...
}

plugin!(MyPlugin {write_at, trim, ...});
```

# Minimum Supported Rust Version (MSRV)

`nbdkit` is supported on Rust 1.42.0 and higher.  The MSRV will not be
changed in the future without raising the major or minor version.

# License

`nbdkit` is primarily distributed under the 2-clause BSD liense.  See
LICENSE for details.
