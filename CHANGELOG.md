# Changelog

All notable changes to qlib are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing is released yet. Everything below is what exists in the tree at version
`0.1.0`, which is still marked unreleased because the public API has not been
frozen.

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
- `ql_thread` *(optional)* — `ql_thread_t`, `ql_mutex_t` and `ql_cond_t` over
  `pthread` and the Win32 equivalents. The first part of the library that is not
  plain C11, and the second link dependency (`Threads::Threads`).
- `ql_net` *(optional)* — `ql_net_addr_t` and textual IPv4 parsing, formatting
  and comparison; plus `ql_net_socket_t` and blocking IPv4 TCP sockets
  (`listen`, `connect`, `accept`, `send`, `recv`, timeouts). No DNS, no
  non-blocking I/O, no IPv6 sockets. The only link dependency in the library:
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
- 116 unit tests on the default build, 125 with the threading module enabled, and 95 with the network module disabled.
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

- `ql_net` handles IPv4 only. IPv6 text is reported as `QL_ERR_NOT_SUPPORTED`
  rather than mis-parsed, and `ql_net_addr_t` already has room for it.
- Concurrency is opt-in (`QLIB_BUILD_THREADS`) and minimal: threads, a mutex and
  a condition variable. No thread pool, no read-write lock, no atomics. Sockets
  are still blocking, and there is still no asynchronous API -- the threading
  layer is what would make one possible, not the async API itself.
- C11 `<threads.h>` is not used, because macOS does not ship it.
- `ql_allocator_t`'s `realloc` takes only `(ptr, size, userdata)`, so it cannot
  use the old size. Callers needing a realloc that knows the old size should
  allocate-copy-free.
