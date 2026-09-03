# learning-backend

A hands-on roadmap for learning backend systems engineering from the ground up, in C++, without frameworks. No `malloc`-in-the-hot-path, no borrowed libraries for the core pieces — each folder is a self-contained lesson that builds a real primitive (an allocator, a socket server, an HTTP parser, a database, a rate limiter) and the next folder reuses what the previous one built.

Read it top to bottom — it's ordered the way the commits actually happened, which is also the order a backend system gets built in: memory → concurrency → networking → protocol → storage → product feature.

## The path

### 1. [cpp_fundamentals/](cpp_fundamentals/) — memory-safe containers from scratch
[`FixedVector.h`](cpp_fundamentals/FixedVector.h) is a fixed-capacity, stack-allocated vector: raw `std::byte` buffer, objects constructed in place with placement `new`, destructors called manually, move-only (copy disabled).

**Why:** before touching sockets or databases, understand what `std::vector` is hiding from you — object lifetime, alignment, and the cost of an allocation.

### 2. [memory_laboratory/](memory_laboratory/) — custom allocators
- [`arena_allocator/ArenaAllocator.h`](memory_laboratory/arena_allocator/ArenaAllocator.h) — a bump allocator: hands out pointers by advancing an offset (with alignment padding), `reset()` wipes everything in O(1). No per-object free.
- [`pool_allocator/PoolAllocator.h`](memory_laboratory/pool_allocator/PoolAllocator.h) — a fixed-block-size allocator backed by an intrusive free list, giving O(1) allocate/deallocate for uniformly-sized objects.

Benchmarked **37x faster than `malloc`** for fixed-size allocations. This is the allocator later reused by `FixedMiniDB` to allocate hash table nodes without ever calling `malloc` on the hot path.

**Why:** generic allocators pay for flexibility you don't need when every object is the same size — this is the building block every high-throughput data structure in this repo stands on.

### 3. [sockets_laboratory/](sockets_laboratory/) & [multi_thread_programming/](multi_thread_programming/) — concurrency & raw networking
- `sockets_laboratory/` — a minimal blocking BSD-socket server/client pair (`socket`/`bind`/`listen`/`accept`/`read`/`write`), one connection at a time.
- `multi_thread_programming/` — a producer/consumer queue built on `std::mutex` + `std::condition_variable`, worked through deadlock and spurious-wakeup bugs by hand.

**Why:** learn the synchronization primitives (mutex, condvar, lock ordering) and feel why a blocking, one-thread-per-connection model doesn't scale — motivating both the thread pool and the event loop that come next.

### 4. [http_from_zero/](http_from_zero/) — protocol parsing & a handmade HTTP server
- [`HttpParser.h`](http_from_zero/HttpParser.h) — an incremental state-machine parser (`RequestLine → Headers → Body → Complete`) that consumes bytes as they arrive over TCP and correctly handles a request split across multiple `read()`s, using `Content-Length` for body framing.
- [`ThreadPool.h`](http_from_zero/ThreadPool.h) — a fixed-size worker pool with a task queue guarded by a mutex/condvar.
- [`server.cpp`](http_from_zero/server.cpp) — a thread-pool-backed HTTP/1.1 server built on the two headers above.
- [`client.cpp`](http_from_zero/client.cpp) — a mock client that deliberately sends one request across two separate `send()` calls, to prove the parser survives partial reads.

**Why:** HTTP is just a framing convention over a byte stream — writing the parser makes that concrete, and it's the last stop before building a real service.

### 5. [mini_db/](mini_db/) — FixedMiniDB, a handmade in-memory key-value store
- [`FixedMiniDB.h`](mini_db/FixedMiniDB.h) — a sharded hash table (16 shards, each behind its own `std::shared_mutex` so reads don't block each other), fixed-size keys/values stored in a `HashNode`, nodes allocated from the `PoolAllocator` built in step 2 (no `malloc` in `put`/`get`/`incr`). TTL-based expiry plus a background eviction thread that wakes on a `condition_variable` once occupancy passes 50%.
- [`main.cpp`](mini_db/main.cpp) — a single-threaded `kqueue`-based event-loop TCP server (macOS/BSD) speaking a Redis-like line protocol: `SET`, `GET`, `INCR`, `DEL`.
- [`benchmark.cpp`](mini_db/benchmark.cpp) — a multi-threaded, pipelining load generator that benchmarks FixedMiniDB head-to-head against real Redis (RESP protocol) at batch sizes 1/64/128, reporting RPS and p50/p95/p99/p99.9 latency.

Along the way, a real concurrency bug got fixed: `get()` originally handed back a raw `char*` into memory that could be reused by another shard's writer, causing dangling-pointer/`nullptr` reads under load. Fixed by returning `std::string` **by value** instead of a pointer.

**Why:** sharded locking beats one global mutex; an event loop beats a thread per connection for I/O-bound work; and returning internal pointers from a concurrent structure is a bug waiting to happen — better to copy out.

### 6. [rate_limiter/](rate_limiter/) — a sliding-window rate limiter on top of FixedMiniDB
Reuses `FixedMiniDB` (scaled up to 128 shards / 524,288 buckets), adds `get_exp()` and a `LIMIT <key> <max> <window_sec>` command that layers a counting rate limiter directly on top of the existing `incr()` + TTL machinery — no new storage engine needed.

[`client.cpp`](rate_limiter/client.cpp) is a heavy stress test: 100 threads × 20,000 requests hammering `LIMIT`, tallying `ACCEPT`/`REJECT`/error counts and measuring throughput. Sustained **~81k requests/sec** under load.

**Why:** once you own a fast enough KV store, rate limiting is "just" a TTL'd counter — the interesting part is proving it holds up under real concurrent load, not the algorithm.

## Recurring architecture lessons
- **Avoid the general-purpose allocator on the hot path.** Every performance-sensitive structure here (`FixedMiniDB`) allocates from a pool built in step 2, not from `malloc`/`new`.
- **Shard your locks.** One `std::shared_mutex` per shard scales better than one mutex guarding the whole table.
- **Event loops over thread-per-connection** for I/O-bound servers — `kqueue` shows up as soon as connection counts matter.
- **Never leak internal pointers from a concurrent structure** — the `get()` dangling-pointer bug in `mini_db` is the concrete example of why.
- **Benchmark against the real thing.** Claims here are backed by numbers against `malloc` and against Redis, not vibes — see `memory_laboratory`'s and `mini_db/benchmark.cpp`'s results.

## Building & running
No build system — everything is a header plus a single `.cpp` per binary, compiled directly:

```sh
clang++ -std=c++17 -O2 mini_db/main.cpp -o mini_db/main
./mini_db/main
```

Each `main.cpp` / `benchmark.cpp` / `client.cpp` / `server.cpp` is its own standalone binary — build only the one you need. The `kqueue`-based servers (`mini_db`, `rate_limiter`) are macOS/BSD-only; everything else is portable C++17.

## Possible next steps
Natural continuations of this path, not yet started: persistence (WAL / snapshotting) for `FixedMiniDB`, a cross-platform event loop (`epoll`/`io_uring` alongside `kqueue`), wiring the HTTP parser directly into the DB server for an HTTP API instead of the line protocol, and a proper sliding-window (not fixed-window) implementation for the rate limiter.

## License
MIT — see [LICENSE](LICENSE).
