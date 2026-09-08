# vyto/net/server

An HTTP/1.1 server: a reactor per worker, one worker per core, no threads.

```vyto
import { server } from "vyto/net/server";

fn main() {
    let s = server(8080);
    s.get("/health", (req, res) => { res.text(200, "ok"); });
    s.get("/users/:id", (req, res) => { res.text(200, req.param("id")); });
    s.post("/echo", (req, res) => { res.text(200, req.bodyText()); });
    s.run();
}
```

`run()` pre-forks one worker per core. `runSingle()` runs one reactor in this
process — the right mode for tests and for a debugger.

## Two axes, both needed

**Reactor** — one `PollSet` (epoll, `poll(2)` elsewhere) holding thousands of
connections per process. Without it a slow client stalls every other client,
which is an availability problem before it is a throughput one.

**Pre-fork** — `SO_REUSEPORT`, one worker per core, kernel-balanced accepts.
Workers share nothing, so refcounts stay non-atomic; threads would force atomic
RC on every object in the program to benefit this one module. Given Vyto has no
thread model, pre-fork is not a preference, it is the only route to multi-core.

A supervisor re-forks any worker that dies, so a handler `panic` costs one
worker rather than the service.

## Request and response

`ServerRequest` is a **view** over the connection's receive buffer. Headers are
recorded as `(offset, len)` pairs and nothing is allocated per header; an
accessor materialises a string only when a handler asks for one.

```vyto
req.method()          req.path()            req.query()
req.header("Accept")  req.hasHeader(...)    req.headerCount()
req.param("id")       req.queryParam("n")
req.body()            req.bodyText()
```

`ServerResponse` builds into the connection's reusable buffer.

```vyto
res.text(200, "hi")   res.html(200, doc)    res.json(200, encoded)
res.header(k, v)      res.redirect(302, "/elsewhere")
```

`Date` is emitted automatically on every response from a per-second cache
(RFC 7231 requires it); a handler that sets its own wins and no duplicate is
sent. A `header()` value containing CR or LF is dropped rather than escaped —
that is the response-splitting defence, and it means a handler cannot introduce
one by echoing user input.

## Limits

Every hostile-input cap lives in one auditable block, enforced **before**
allocation:

```vyto
let L = new Limits();
L.max_header_count  = 100;
L.max_header_bytes  = 32 * 1024;
L.max_body_bytes    = 1024 * 1024;
L.recv_timeout_ms   = 15000;      // the Slowloris bound
L.max_keepalive_reqs = 1000;
s.withLimits(L);
```

Also tunable on the server: `withWorkers(n)`, `withMaxConns(n)`,
`withBacklog(n)`, `bindHost(h)`.

A connection's buffer starts at 2 KB and doubles toward
`max_request_line + max_header_bytes` on demand. The cap is the security bound;
it is deliberately not the allocation, because allocating it up front cost
~15x the memory at ten thousand connections.

## Status

**The protocol gaps this section used to list are fixed.** `HEAD` sends no body,
duplicate and conflicting `Content-Length` is refused, `Host` is required on
1.1, `Expect: 100-continue` gets its interim response, absolute-form targets
match, header names are tchar-validated, and percent-decoding plus dot-segment
normalisation happen before routing. `Transfer-Encoding` is still refused with
501 rather than mis-framed — deliberately, since guessing at framing is the
request-smuggling class of bug.

What is genuinely still missing, and each is core:

- **Signal handling.** `stop()` drains — closes the listener, finishes what is
  in flight within `withDrainMs()` — but nothing triggers it from outside the
  process, so `SIGTERM` still kills workers where they stand. The self-pipe
  shim it needs exists (`vyto/os/reactor`'s `onSignal`); it is not wired in.
  This is the biggest gap.
- **Chunked request bodies** (501, above).
- **TLS.** Terminate upstream.
- **Cross-worker aggregation.** Pre-fork workers share nothing, so `ServerStats`
  is per worker and any cache is too.

Access logging and metrics are **seams, not gaps**: `onRequest(req, status,
bytes, us)` and `ServerStats` ship, and the format, sink and `/metrics`
endpoint are user-space by design — see "what belongs in this server" in
`local/docs/HTTP.md`, which also carries the measurements and roadmap.

Tested by `examples/91_server.vt` — 32 assertions over routing, parsing, limits,
buffer growth, and the framing rules, with the security-critical subset promoted
from `local/bench/http/srv_proto.vt`. The concurrency cases (many simultaneous
connections, a stalled client not blocking a healthy one, worker resurrection)
are not deterministic enough for the golden suite and live beside it in
`local/bench/http/`.
