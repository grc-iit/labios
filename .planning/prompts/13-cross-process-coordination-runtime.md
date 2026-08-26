# 13 — Cross-process channel and workspace runtime

**Queue status:** Next; first in the RC-to-final queue.

Run after 12 and implement its accepted contract exactly. Read `AGENTS.md`,
`.planning/LABIOS-2.1.md`, the new contract, catalog transport, public native and
Python APIs, and existing coordination tests.

## Objective

Replace process-local channel/workspace truth with catalog-backed state shared by
independently started processes.

## Ownership

Channel/workspace/catalog implementation, public C++/Python bindings needed by the
contract, focused unit/multiprocess tests, observability, and coordination docs. Do
not implement MCP tools, shadow metadata, new transient primitives, or arbitrary
distributed transactions.

## Work

1. Implement the contracted versioned key schema and atomic create/open/update/CAS/
   sequence/ACL operations. Process memory may cache but never become authority.
2. Make handles ownership-safe across client destruction and reconnects. Enforce
   owner/reader/writer/admin rights on every operation and make TTL cleanup visible.
3. Implement channel ordering/cursors/delivery and workspace version conflicts
   exactly as contracted, including duplicate, crash, reconnect, and expiry paths.
4. Expose public lifecycle, membership/ACL, version/sequence, retention, and error
   diagnostics without requiring direct DragonflyDB inspection.
5. Keep existing single-process compatibility where semantics permit; document any
   deliberate break with migration guidance.

## Required evidence

Use independently launched producer, consumer, owner, authorized peer, unauthorized
peer, and restart processes. Test concurrent sequence allocation, CAS conflict,
ACL denial, client destruction, reconnect, manager/Dragonfly restart within the
declared guarantee, TTL expiry, and cleanup. Run under sanitizers plus live Compose.

## Exit

No authoritative coordination state is process-local, all multiprocess acceptance
cases pass twice from clean state, and claims are bounded to the tested topology.
