# Design Decisions

Each section records one decision as Context / Decision / Consequences. Cross-references point at the file where the decision is visible in code.

---

## ADR-1: Opaque public types, with value-semantics structs as a named exception

**Context.** The library wants ABI stability, so callers must not be able to compute `sizeof` or a member offset of a library-owned object. At the same time a few types exist purely so that users can build them on the stack and hand them over — the allocator callback table is the only one that survives today.

**Decision.** Container types are incomplete in the public header. `ql_vector_t` is declared as `typedef struct ql_vector ql_vector_t;` in [`ql_vector.h`](../../include/qlib/ql_vector.h) and defined only in `src/ql_vector.c`. Types that users must construct are complete structs, and every such struct carries trailing `void* reserved[]` fields. The rule and the exception are written down in [CONTRIBUTING.md](../../CONTRIBUTING.md) rule 2.

**Consequences.** A container can gain, lose or reorder private fields without an ABI break. The cost is one heap allocation for the control block (`ql_vector_create()` allocates `struct ql_vector`, and the element buffer only on first growth), and accessors instead of direct field reads. Adding a new opaque container is cheap; adding a new *exposed* struct requires justifying the exception.

---

## ADR-2: `reserved[]` inside exposed structs

**Context.** `ql_allocator_t` is passed by value across the ABI, so its layout is frozen the moment it ships. Future needs (a `calloc` slot, a size field for versioning) would otherwise force a new type or a breaking change.

**Decision.** `ql_allocator_t` ends with `void* reserved[4]`, documented as "Reserved for future growth. Must be zeroed; do not use." New members are only ever taken from this array. `ql_default_allocator()` leaves them zero because `ql_default_alloc` in `src/ql_mem.c` is initialised with designated initialisers, so all unlisted members are zero-initialised. Note that aligned allocation, the case originally named here, did **not** end up needing a slot: see ADR-13 below.

**Consequences.** Four pointers of headroom (32 bytes on LP64) cost nothing at call time and buy a compatible growth path. Two obligations come with it: the header states the struct's layout is fixed, and callers that build an allocator by hand must zero the array — a partially filled struct is a caller bug, not something the library detects. The array is finite: once it is exhausted, any further growth needs a versioned struct or an accessor-based interface.

---

## ADR-3: Allocators are passed by value and copied into containers

**Context.** A container needs an allocator for its whole lifetime, not just for the call that created it. The two options are storing a pointer to the caller's `ql_allocator_t` or copying the struct.

**Decision.** `ql_vector_create()` copies: `vec->alloc = alloc != NULL ? *alloc : ql_default_allocator();`. The header states "The allocator is copied into the vector, so the caller's struct does not need to outlive the call." `ql_vector_destroy()` copies the allocator out into a local before freeing the control block, so the final free does not read from memory it is about to release.

**Consequences.** A stack-allocated allocator works as long as it lives through the create call, which removes a common class of dangling-pointer bug. The flip side is that later mutations of the caller's struct — including changing `userdata` — have no effect on an existing container; the copy is made once and never re-read. Copying also means the library never dereferences caller storage during normal operation, which is friendlier to embedders that keep their allocator in a shared library that may unload.

---

## ADR-4: A `NULL` allocator means "use the default"

**Context.** Requiring every call site to fetch and pass a `ql_allocator_t` makes the common case verbose and pushes embedders toward a global. Conversely, the library cannot invent an allocator implicitly.

**Decision.** Every helper in [`ql_mem.h`](../../include/qlib/ql_mem.h) accepts `const ql_allocator_t*`, and `NULL` resolves to the built-in libc allocator via `ql_alloc_resolve()`. All three function pointers in a *non-NULL* struct must be non-NULL; that requirement is documented but not re-checked, because these are the allocation hot path.

**Consequences.** `ql_alloc(NULL, 64)` and `ql_vector_create(NULL, sizeof(int), &v)` read well and keep the fast path branch-light. The trade-off is that a partially filled struct is undefined behaviour rather than a diagnosed error — `ql_alloc_resolve()` only tests the pointer for `NULL`. Callers that want a partial allocator rejected should validate it themselves.

---

## ADR-5: Overflow-checked multiplication instead of wrapping arithmetic

**Context.** `count * size` is the standard way to compute an allocation size and the standard way to introduce a heap overflow. `size_t` arithmetic wraps silently, so a wrapped product looks like a small, satisfiable request.

**Decision.** `ql_size_mul_overflow(count, size, result)` exists as a public function and is the only sanctioned way to compute an element-block size. It returns `QL_ERR_INVALID_ARG` for a NULL `result`, `QL_ERR_OVERFLOW` when `count != 0 && size > SIZE_MAX / count`, and `QL_OK` otherwise; on failure `*result` is **left unmodified**. `ql_alloc_array()`, `ql_realloc_array()` and `ql_calloc()` are built on it, and [CONTRIBUTING.md](../../CONTRIBUTING.md) rule 10 forbids computing the product by hand.

**Consequences.** The check is division-based, so it costs one division per array allocation — negligible against the allocator call that follows. Because the output is untouched on failure, a caller can initialise its variable once and rely on the value being either correct or untouched, never a wrapped half-result. The zero-count case is exempted explicitly (`count != 0`), since `0 * anything` is legitimately `0` and `SIZE_MAX` as an element size with a count of zero must not be reported as overflow.

---

## ADR-6: Helpers return `NULL` and also raise an error

**Context.** An allocation helper must return something when it fails. The library also offers an error handler, and the two mechanisms overlap.

**Decision.** The helpers return `NULL` — the only sane sentinel for "no block" — and independently call `ql_err_raise()` before returning. `ql_alloc()` and `ql_realloc()` raise `QL_ERR_NOMEM`; the array helpers and `ql_calloc()` raise `QL_ERR_OVERFLOW`. `ql_realloc()` documents libc semantics: on failure the original block is untouched and still owned by the caller. `ql_err_raise()` is a no-op when no handler is installed.

**Consequences.** Callers choose their style: check the pointer, or centralise logging in the handler, or both. Two consequences need stating. First, the failure is reported twice if the caller acts on both, so the handler should log and the caller should branch, not the reverse. Second, all array helpers raise the same generic message (for example `"ql_calloc: element count overflows size_t"`), so the message identifies the operation but not the caller's variable — allocation-time context has to come from the handler's own captured state.

---

## ADR-7: `ql_strerror()` is total, and its switch has no `default:`

**Context.** `ql_strerror()` takes an enum, but the value can arrive from outside the enum's range — a corrupted struct, a cast from an `int` returned by another API, a value written by a newer library version than the caller's header.

**Decision.** The switch lists all fourteen enumerators and has **no `default:` clause**; a `return "unknown error";` sits after it, and the function is documented as never returning NULL. The absence of `default:` is deliberate and commented in `src/ql_common.c`: it makes `-Wswitch` (part of `-Wall`) fail the build when a new enumerator is added and forgotten here.

**Consequences.** The function is total — no caller needs a NULL check — and adding a new error code produces a compile error instead of a silent "unknown error". The cost is that the string-builder's switch must be kept exhaustive by hand, and that `-Wswitch` only protects builds where warnings are on; `QLIB_WERROR=ON` in CI is what turns the reminder into a hard failure.

---

## ADR-8: `EAGAIN` and `ENOTSUP` are used directly, not through their aliases

**Context.** POSIX defines aliases that are equal in value to other constants: `EWOULDBLOCK == EAGAIN` and `EOPNOTSUPP == ENOTSUP` on glibc. `ql_ret_from_errno()` could reasonably want to accept either spelling.

**Decision.** Only the primary names are used; `EWOULDBLOCK` and `EOPNOTSUPP` are deliberately omitted from the switch in `src/ql_common.c`, and the reason is recorded in a comment there.

**Consequences.** Writing both spellings of an alias in the same `switch` gives two `case` labels with the same value, which is a compile error, not a warning — so the alias form could not have been added "for completeness" on any platform where the two are equal. The behaviour is nevertheless complete, since an alias is the same integer and matches the primary `case`. The subtlety that remains is cosmetic: a reader looking for `EWOULDBLOCK` in the function will not find it, hence the comment.

---

## ADR-9: `push_back` and `insert` pin an element that aliases the vector

**Context.** Nothing stops a caller writing `ql_vector_push_back(vec, ql_vector_at(vec, 0))`. Two operations destroy the source pointer in that case: growth calls `realloc`, which may move and free the buffer, and `insert` calls `memmove`, which shifts the very bytes being read.

**Decision.** `ql_vector_pin_elem()` tests the argument with `ql_vector_contains()` — a range test against `[data, data + size * elem_size)`. When the pointer is inside the live buffer it allocates `elem_size` bytes of scratch, copies the element there, and returns the scratch pointer in place of the caller's. `push_back()` and `insert()` free the scratch on every exit path. `ql_vector_assign()` has the same hazard in a cheaper form and uses `memmove` rather than `memcpy`, since overlapping regions within one buffer are the only case `assign` can create.

**Consequences.** Self-referential calls are well-defined instead of a use-after-free, and the guard is exercised by dedicated tests that run on an allocator whose `realloc` always moves the block, so the bug cannot hide behind a benign in-place `realloc`. The cost is a range comparison on every `push_back` and `insert`, plus a temporary allocation in the aliasing case only. That allocation is itself fallible, so an aliasing `push_back` can fail with `QL_ERR_NOMEM` even when the vector had spare capacity.

---

## ADR-10: Geometric growth with an explicit doubling-overflow branch

**Context.** Growing by one element per `push_back` makes appending quadratic. Doubling is the usual answer, but `capacity * 2` itself overflows for capacity above `SIZE_MAX / 2`.

**Decision.** `ql_vector_grow()` starts from `QL_VECTOR_MIN_CAPACITY` (8) when the vector has no capacity, doubles while `new_capacity < min_capacity`, and stops doubling once `new_capacity > SIZE_MAX / 2`, at which point it requests exactly `min_capacity`. The byte count is then validated with `ql_size_mul_overflow()`.

**Consequences.** Amortised appending is O(1). Observed behaviour on this checkout: the first `push_back` takes capacity from 0 to 8; capacity stays 8 through the eighth element and becomes 16 on the ninth; `reserve(8)` yields capacity 8 and a later `reserve(4)` leaves it at 8. The overflow branch means the loop terminates even for a `min_capacity` near `SIZE_MAX`, and the failure surfaces as `QL_ERR_OVERFLOW` rather than as a small allocation. On failure `vec->data` and `vec->capacity` are untouched, so a rejected grow never loses existing elements. Doubling does mean up to 2× memory overhead versus a size-exact allocation, and capacity is never returned to the allocator — there is no `shrink_to_fit`.

---

## ADR-11: `resize()` zero-fills, `clear()` retains capacity

**Context.** A caller that grows a vector and reads the new elements needs a defined initial value. At the same time, a caller that resets a vector to reuse it does not want to pay for re-allocation.

**Decision.** `ql_vector_resize()` zero-fills the newly exposed region with `memset` after reserving; shrinking only lowers `size`, leaving the bytes in place until overwritten. `ql_vector_clear()` sets `size = 0` and touches nothing else. Both are documented in [`ql_vector.h`](../../include/qlib/ql_vector.h).

**Consequences.** Reading an element obtained from `resize()` is always defined, which removes a whole class of uninitialised-read bugs and is asserted by the tests. `clear()` is O(1) and makes a fill/drain loop allocation-free after the first pass. The cost is that `resize()` writes memory that a caller who is about to fill every element did not need written, and that shrinking does not reduce capacity, so a vector that once held a million elements keeps that buffer until it is destroyed.

---

## ADR-12: `ql_lock_t` was removed rather than half-implemented

**Context.** An earlier revision of [`ql_common.h`](../../include/qlib/ql_common.h) declared a lock adapter (`ql_lock_t` plus `ql_lock_fn`/`ql_unlock_fn`) as a forward-looking hook for thread safety. Nothing constructed one and nothing consumed one: the types had no setter and no implementation, so no compiler or test would have noticed if the semantics were wrong.

**Decision.** The types were deleted. No concurrent container exists yet, and a correct implementation needs a POSIX path and a Win32 path, each with a thread-driven test — macOS provides no C11 `<threads.h>`, so a single portable test is not available. `ql_allocator_t.reserved[]` (ADR-2) exists precisely so the capability can return without an ABI break.

**Consequences.** The public API contains no types that cannot be used, and the README no longer advertises a lock adapter. The cost is a genuine loss of a planned feature, which must be reintroduced with its implementation and tests together when the first concurrent module lands. The alternative — shipping the declarations now so the header "looks complete" — was rejected because untested cross-platform threading code is worse than an absent feature.

---

## ADR-13: Aligned allocation aligns by hand instead of using the platform primitive

**Context.** Over-aligned memory is needed for SIMD buffers, cache-line separation, DMA and page-aligned I/O. Three ways to get it were on the table: the C11 `aligned_alloc`, the POSIX `posix_memalign`, and the MSVC `_aligned_malloc`/`_aligned_free` pair. All three are platform-specific, and — more importantly — all three bypass `ql_allocator_t` entirely, so memory obtained that way would not come from the caller's allocator and could not be released through it. The other candidate, consuming `ql_allocator_t.reserved[]` for a pair of aligned function pointers, was rejected: it splits freeing into two paths, so `ql_aligned_free()` would have to be handed an allocator whose aligned slots match whatever allocated the block, and getting that wrong corrupts memory rather than failing.

**Decision.** `ql_aligned_alloc()` and `ql_aligned_free()` are implemented as a single path over the allocator's own `malloc`/`free`. The request is padded to `size + alignment + sizeof(void*)` via `ql_size_add_overflow()` twice, the address is rounded up, and the pointer the allocator actually returned is stored in the `sizeof(void*)` bytes immediately before the aligned address. Freeing reads that header back and returns it to the allocator's `free`. No platform primitive is used, and `ql_allocator_t` is left exactly as it was.

**Consequences.** Every byte still goes through the caller's allocator, so rule 7 holds with no exception and no platform branch; the code compiles and behaves identically on all three supported platforms. There is one path, so there is never a question of which allocator may free a given block. The costs are specific and accepted: `alignment + sizeof(void*)` bytes are wasted per block, which is negligible for the 16–64 byte alignments this exists for and wasteful for megabyte alignments; the returned pointer is not the allocator's pointer, so it must be freed with `ql_aligned_free()` and can never be passed to `ql_dealloc()`, `ql_realloc()` or libc `free()`; and there is no aligned realloc, because the header makes the size unknowable to `realloc`. The platform primitives are never exercised, so whatever efficiency `posix_memalign` or `_aligned_malloc` would have offered is forgone. `reserved[]` stays untouched and available for additions that genuinely cannot be layered this way, such as a `calloc` slot or a size field.

---

## ADR-14: The network module starts with address parsing, and no sockets

**Context.** `QLIB_BUILD_NET` was an option that built nothing. A network module is a large undertaking, and the awkward parts arrive immediately: sockets need per-process initialisation on Windows (`WSAStartup`, which is global, must precede every other socket call and has no natural place to be called from inside a library), the socket library has to be linked on Windows but not on POSIX, and nearly every operation has a different spelling on each platform. A first slice therefore has to be chosen for what it does *not* need, or it will not land.

**Decision.** The module begins with textual IPv4 address handling: `ql_net_addr_parse()`, `ql_net_addr_format()` and `ql_net_addr_equal()`. No sockets, no platform initialisation, no external library to link, and no `#if defined(_WIN32)` anywhere. IPv6 text is recognised and reported as `QL_ERR_NOT_SUPPORTED` rather than mis-parsed as IPv4, and `ql_net_addr_t` stores sixteen bytes so adding IPv6 is not an ABI change. The family enum uses qlib's own values rather than the platform's `AF_INET`, which differs between Windows and POSIX.

**Consequences.** Every code path is pure computation over a string, so it is testable without a network, a peer, a privileged port or a timeout, and it cannot be flaky in CI. There is no link dependency, so `QLIB_BUILD_NET` stays a source-level toggle and the module behaves identically on all three supported platforms. Parsing also turns out to be worth fuzzing on its own merits: it is the one place in qlib that converts untrusted text into a value, and `fuzz/fuzz_net.c` checks the round-trip invariant. The cost is a module called "network" that currently opens no sockets, which has to be said plainly in the documentation rather than implied away. When sockets do arrive they will bring `WSAStartup` and its process-global lifetime, a conditional link dependency, and the module's first genuinely platform-specific code — a design question this slice deliberately leaves open.

---

## ADR-15: Three containers, each answering one question `ql_vector` cannot

**Context.** `ql_vector` is a contiguous array of fixed size elements, and it covers most needs. Adding a string, a hash table and a linked list risks three more ways to do the same thing badly. Each was therefore admitted only for a question `ql_vector` answers poorly, and each has one design decision that determines whether it earns its place.

**Decision.**

*`ql_str` — the buffer is always NUL terminated.* A growable buffer could track only a length, and would then be a `ql_vector` of `char`. Keeping a terminator in place means `ql_str_cstr()` can be handed to any C API without a copy, and every mutating function restores it before returning — **including on the failure path**, which is why the `vsnprintf` retry loop rewrites the terminator after a truncated attempt. Embedded NUL bytes are still accepted, because rejecting data a caller already holds is worse than a length that exceeds `strlen()`; the length is authoritative and the header says so.

*`ql_hash` — generic fixed size byte keys, not string keys.* A string-keyed map is easier to use and would have covered the common case. It was rejected because it forecloses integer keys, composite keys and keys the caller already has as bytes, and because it silently allocates a copy of every key. Comparing with `memcmp` over a fixed block has real costs, and they are documented rather than hidden: a struct key with padding bytes cannot be used, and a `char*` key hashes the pointer.

*`ql_list` — node handles are part of the API.* Without stable handles a linked list is a slow deque. With them it answers the question a vector cannot: remove this specific element in constant time, no matter what else has happened. Elements are copied in and out, never referenced, so no pointer into the list's storage is ever handed out — which is also why the element type's alignment does not matter.

**Consequences.** The three modules share `ql_mem`'s allocator discipline and `ql_common`'s error codes, so they feel like the rest of the library rather than three additions. Each one's cost is measurable: the benchmark suite compares them directly against `ql_vector` (push 15 ns contiguous versus 29 ns allocated per node; removing the first element 39 ns shifting versus 15 ns to unlink a node by handle), which keeps the "use a vector unless you need this" guidance honest instead of assumed. The price is surface area: three more opaque types, three more error-code tables, and three more things to keep documented in two languages.

Nothing here adds a dependency. `ql_hash` implements FNV-1a inline rather than linking a hash library, and `ql_list` allocates one block per node rather than pooling, because a pool is a second allocator policy to test and the allocator abstraction already lets a caller supply one.

---

## ADR-16: Sockets are blocking, IPv4 only, and bring the first link dependency

**Context.** ADR-14 left sockets out of the network module because they arrive with three awkward things at once: `WSAStartup`, which is process-global and has no natural place to be called from inside a library; a link dependency that exists on Windows and not on POSIX; and a different spelling for nearly every operation. Deferring them was the right call for a first slice, but a network module that cannot open a connection is not finished either.

**Decision.** *TCP only, blocking, IPv4 only, no DNS.* Every socket call has a single obvious POSIX and Win32 spelling, so the platform code stays next to the calls it adjusts instead of spreading through the logic. `WSAStartup` is handled by a one-shot `ql_net_init()` that every socket call invokes itself, with an optional `ql_net_shutdown()`; there is no reference count, because a count that can get out of step with the sockets actually open is worse than a flag that cannot. The Winsock dependency is `PUBLIC` on the target, so a consumer of the static library links it too, and the whole module remains behind `QLIB_BUILD_NET` so a build that does not want a link dependency does not get one.

*Blocking rather than asynchronous.* Non-blocking sockets need either a thread or an event loop, and qlib has neither. Adding one is the decision that ADR-14 deliberately postponed, and it is bigger than this module: the testing story for an async API is a different testing story. That is also why there is no DNS: `getaddrinfo` itself is easy now, but resolution belongs with a policy about timeouts and cancellation that this library cannot yet honour.

**Consequences.** The address half still needs no network, no initialisation and no library, so `QLIB_BUILD_NET` remains a source-level toggle and the address tests remain hermetic. The socket half is tested on loopback without a peer thread: the kernel completes a connection into the listener's backlog before `accept()` is called, so connect-then-accept-then-send-then-recv is deterministic in a single thread as long as the order is respected and a timeout is set. That is a real test, not a mock, and it caught two contract violations while being written.

Two costs are documented rather than hidden. First, an expired receive timeout is reported as `QL_ERR_AGAIN`, not `QL_ERR_TIMEOUT`, because that is what Linux and macOS return for an expired `SO_RCVTIMEO` and it cannot be told apart from a full buffer without a `poll()` layer. Second, a caller that wants IPv6, DNS or concurrency has to bring those itself; the module says so instead of pretending they are coming for free.

---

## ADR-17: A threading layer of our own, rather than C11 `<threads.h>` or nothing

**Context.** Two earlier decisions kept hitting the same wall. ADR-16 made sockets blocking because non-blocking I/O needs a thread or an event loop, and qlib had neither. The same gap blocks an asynchronous log sink. Three ways out were on the table: require C11 `<threads.h>`, tell callers to write their own `#ifdef`s, or build a small portability layer.

**Decision.** A portability layer: `ql_thread`, `ql_mutex_t` and `ql_cond_t` over `pthread` and the Win32 equivalents, behind the optional `QLIB_BUILD_THREADS` because it is the library's second link dependency. `<threads.h>` was rejected because **macOS does not ship it** -- the platform is one of the three this library claims, and a C11 feature it omits is not available to us however standard it is. Pushing the `#ifdef`s onto callers was rejected because the same twenty lines of Windows-versus-POSIX boilerplate would then exist once per caller, each version tested only by whoever wrote it.

All three types are opaque handles rather than embeddable values. An embedded `ql_mutex_t` would have to reserve space for whichever platform type is largest: `pthread_mutex_t` and `CRITICAL_SECTION` have no portable size, so the struct would either waste memory everywhere or silently corrupt memory on one platform. A handle moves that risk into the library, where one implementation covers every caller. The cost is one allocation per synchronisation object.

**Consequences.** The Windows implementation is the first in the library that could not be compiled locally at all -- `_beginthreadex`, `CRITICAL_SECTION` and `CONDITION_VARIABLE` are verified only by CI. That is the honest position: the alternative was not having a threading layer. It is also why the Windows jobs now build with `QLIB_BUILD_THREADS=ON`, and why the local check runs the module under ThreadSanitizer when clang is present.

Building the module found two bugs that a plain run would not have. The first was a real use-after-free: the thread's start context lived in the same allocation as the handle, so `ql_thread_detach()` freed memory the trampoline had not read yet -- ASAN caught it. It is now two allocations with one owner each. The second was in the test: the leak check read a shared counter while a thread was still running, which ThreadSanitizer correctly called a data race.

An expired condition-variable wait is `QL_ERR_TIMEOUT` on both platforms, but the POSIX side had to use `timespec_get` rather than `clock_gettime`: the latter is POSIX.1b and stays hidden under `-std=c11`, which is the dialect qlib builds in.

---

## ADR-18: The logger is a singleton with fixed buffers, and that is deliberate

**Context.** A capable logging library (`xlog`) was evaluated for merging. It brings levels, structured key-value fields rendered as `k=v` or JSON, three sinks including syslog, rotation, and a synchronous and asynchronous mode. Three of its properties conflict with how the rest of qlib is built: it is a singleton rather than a handle, its record buffers are compile-time macros declared as part of the ABI, and its asynchronous mode needs a thread. Holding it back until all three were resolved would have meant shipping none of it.

**Decision.** Merge the synchronous core as `ql_log`, and take the three departures deliberately rather than by accident.

*The singleton stays.* A logger is genuinely per-process, and threading a handle through every call site would be felt in every function that ever logs. It is the second piece of global state in the library, after the error-handler registry, and this record is where that is admitted. `ql_log_init()` is idempotent so that "who initialises it" never becomes a question.

*The buffers stay fixed, and are not tunable.* `QL_LOG_LINE_MAX` is part of the format: a record that needs an arbitrary amount of memory turns a diagnostic into an allocation failure, which is the opposite of what a diagnostic is for. A message that does not fit is truncated rather than dropped. The important part is that the size is **not** a per-consumer macro like `XLOG_MSG_MAX` was — a struct whose layout differs between the library and the application is not an ABI, it is a crash waiting for two differently-configured builds.

*It requires the threading module.* `QLIB_BUILD_LOG` without `QLIB_BUILD_THREADS` is a configure error, because the logger takes a mutex. A logger that interleaves half-lines between threads is worse than no logger, and making the mutex optional would mean the safe configuration is the one nobody tests.

*What was left out, and why.* Asynchronous logging, syslog, and thread-local structured fields are all deferred. The first two need work that this slice does not: async needs a ring queue and a writer thread, syslog is POSIX-only. Structured fields need thread-local storage, which the threading module does not yet provide. JSON output was kept, because once formatting exists it is self-contained.

**Consequences.** `src/ql_log.c` contains **no `#ifdef` at all** — everything it needs is C11. That is not an accident either: colour is explicit rather than sniffing for a terminal (which needs `isatty`, a POSIX call), and the timestamp uses `localtime()`/`gmtime()` under the logger's own mutex rather than the POSIX reentrant variants. A module with no platform code cannot fail on a platform it was never compiled for, which is worth more than automatic colour.

That portability has one price, paid in the build system rather than in the source: MSVC deprecates `fopen` and `getenv` in favour of Annex K replacements that glibc and macOS do not provide. `_CRT_SECURE_NO_WARNINGS` is therefore set on the internal warnings target, so one compiler's policy is answered once instead of forking every call.

The cost is a second link dependency chain (`ql_log` → `ql_thread` → `Threads::Threads`), and a log format that is now a compatibility surface: `PLAIN` lines and `JSON` keys are what log shippers will parse, so changing them is a breaking change from here on. Both are recorded rather than discovered later.
