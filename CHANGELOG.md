# Changelog

All notable changes to qlib are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-10-17

The first release. Version 0.1.0 was developed in-tree and never published, so
everything below is what 0.2.0 contains.

The major version is 0: that is a promise the major version will not change under
you, not a promise that nothing else will. A minor bump is not yet an ABI
boundary -- see [ABI and versioning](docs/technical/abi-and-versioning.md).

### Added

#### Modules

- `ql_common` — `ql_ret_t` and `ql_strerror()`, `ql_ret_from_errno()`, an
  installable error handler (`ql_set_error_handler()` / `ql_get_error_handler()`),
  version macros and `ql_version()`, and `QLIB_NODISCARD`.
- `ql_mem` — the replaceable `ql_allocator_t`, `ql_default_allocator()`, the
  allocation helpers (`ql_alloc`, `ql_calloc`, `ql_realloc`, `ql_dealloc`), the
  overflow-checked array helpers (`ql_alloc_array`, `ql_realloc_array`), the
  overflow predicates (`ql_size_mul_overflow`, `ql_size_add_overflow`), and
  `ql_aligned_alloc()` / `ql_aligned_free()`.
- `ql_str` — a growable, always NUL terminated byte string, with an append
  path that accepts embedded NUL bytes and supports appending a string to
  itself.
- `ql_hash` — a hash table of fixed size keys to fixed size values: open
  addressing with linear probing, tombstones for deletion, `ql_hash_reserve()`
  to size it up front so that known workloads never rehash, and an iterator that
  allocates nothing.
- `ql_list` — a doubly linked list whose node handles stay valid while other
  nodes come and go, giving constant time removal from anywhere.
- `ql_vector` — an opaque, growable, contiguous array, with an aliasing guard
  that makes `ql_vector_push_back(v, ql_vector_at(v, 0))` defined behaviour.
- `ql_log` *(optional)* — levels with runtime filtering and compile-time
  stripping, plain/raw/JSON output with JSON escaping, thread-local structured
  fields, console and rotating-file sinks, and an asynchronous mode with a
  bounded queue. A singleton with fixed-size records, both deliberate; it
  requires the threading module so that a shared logger cannot interleave
  half-lines. Asynchronous logging, syslog and structured fields are not in it.
- `ql_thread` *(optional)* — `ql_thread_t`, `ql_mutex_t` and `ql_cond_t` over
  `pthread` and the Win32 equivalents. The first part of the library that is not
  plain C11, and the second link dependency (`Threads::Threads`).
- `ql_net` *(optional)* — `ql_net_addr_t` with textual IPv4 **and IPv6**
  parsing, formatting (RFC 5952, IPv4-mapped addresses in mixed notation) and
  comparison; name resolution via `getaddrinfo`; plus `ql_net_socket_t` and
  blocking IPv4 TCP sockets (`listen`, `connect`, `accept`, `send`, `recv`,
  timeouts). No non-blocking I/O and no IPv6 sockets. The only link dependency in the library:
  `ws2_32` on Windows, and only when the module is built.

#### Build

- CMake package with `GNUInstallDirs`, an exported target set
  (`find_package(qlib)` provides `qlib::qlib`), a generated
  `qlibConfigVersion.cmake`, and a `qlib.pc` for pkg-config.
- Options: `QLIB_BUILD_TESTS`, `QLIB_BUILD_FUZZ`, `QLIB_BUILD_BENCH`,
  `QLIB_BUILD_NET`, `QLIB_BUILD_DOCS`, `QLIB_SHARED`, `QLIB_WERROR`,
  `QLIB_SANITIZE`, `QLIB_INSTALL`.
- Pinned to C11 exactly: `-std=c11` with compiler extensions disabled.
- An `uninstall` target.

#### Testing

- A minunit harness with a self-check (`tests/selftest/`) that fails when the
  harness itself stops reporting failures.
- 147 unit tests with the threading and logging modules, 127 with threads but no logger, 118 on the default build, and 95 with the network module disabled.
- Six libFuzzer targets: `fuzz_str`, `fuzz_hash`, `fuzz_list`, `fuzz_vector`, `fuzz_mem`, `fuzz_net`.
- Sixteen micro-benchmark cases across the containers, memory and address handling.
- `ci/check.sh`, running the whole local matrix in eleven steps and
  distinguishing a skipped check from a passing one.

#### Documentation

- An English user guide and technical documentation, with a Simplified Chinese
  translation maintained file for file, both included in the generated Doxygen
  reference.
- `docs/zh/development-workflow.md`, an operational manual for extending the
  library.
- MIT licence.

#### Continuous integration

Eleven jobs: two Linux compilers, macOS, two Windows configurations (static and
shared, real MSVC), ASan+UBSan, libFuzzer, the Doxygen build with warnings
treated as errors, the install plus downstream consumer, formatting, and static
analysis.

### Known limitations

- `ql_net` parses and formats IPv6 but cannot connect over it: sockets are IPv4
  only, and an IPv6 address given to one is `QL_ERR_NOT_SUPPORTED`.
- Concurrency is opt-in (`QLIB_BUILD_THREADS`) and minimal: threads, a mutex and
  a condition variable. No thread pool, no read-write lock, no atomics. Sockets
  are still blocking. The logger does have an asynchronous mode, which is what
  the threading layer was for.
- C11 `<threads.h>` is not used, because macOS does not ship it.
- `ql_allocator_t`'s `realloc` takes only `(ptr, size, userdata)`, so it cannot
  use the old size. Callers needing a realloc that knows the old size should
  allocate-copy-free.
- Two things are deliberately absent rather than half-present: **non-blocking
  sockets**, which need an event-loop abstraction the library does not have
  (a thread per blocking socket is the supported answer), and **syslog**, which
  would put the first `#ifdef` into `ql_log`, whose portability claim is that it
  has none.
