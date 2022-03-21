# Change Log

All notable changes to this project will be documented in this file.
This project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased] - ReleaseDate
### Changed

- The `peername` function is now generic, taking any argument that implements
  `nix::sys::socket::SockaddrLike`.
  (#[4](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/4)

- Raised the MSRV to 1.46.0 due to bitflags bug #255.
  (#[1](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/1)

