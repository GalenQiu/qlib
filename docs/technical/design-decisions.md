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
