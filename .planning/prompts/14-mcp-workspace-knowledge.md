# 14 — MCP workspace-backed knowledge tool

**Queue status:** Blocked by Prompt 13; second in the RC-to-final queue.

Run after 13. Read `AGENTS.md`, `.planning/LABIOS-2.1.md`, the coordination
contract, public Python API, MCP core ingress, and MCP documentation/tests.

## Objective

Add the multi-agent knowledge workflow as a thin MCP frontend over the proven
workspace API, without creating another storage or coordination implementation.

## Ownership

MCP knowledge tool/schema, Python-side adapter, tests, example, and MCP docs. Do not
change C++ coordination semantics, access DragonflyDB directly, add a region
primitive, or turn MCP into a generic tool broker.

## Work

1. Define create/open, put/get/list/delete or the smaller operation set justified by
   the workspace contract. Map MCP caller identity to documented workspace ACLs.
2. Use the public Python workspace handle for every operation. Surface version/CAS
   conflicts, authorization, expiry, timeout, and unavailable-runtime errors with
   stable MCP categories.
3. Provide a two-process/two-agent example where one process publishes knowledge
   and another independently opens the named workspace and consumes it.
4. Prove unauthorized access fails and no test reaches internal catalog keys.

## Exit

Unit and live MCP tests verify cross-process data, ACL enforcement, restart/reopen,
and conflict behavior. Documentation distinguishes workspace coordination from
ordinary Label I/O store/retrieve tools.
