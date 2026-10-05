# SBCL platform feasibility on Xbox

This is a small x64 UWP diagnostic, **not an SBCL port**. It tests the runtime
primitives needed before attempting to adapt SBCL's existing Windows backend
to nixbox's Clang/MSVC-CRT toolchain and Xbox app container.

```sh
./build sbcl-platform-probe
nix run .#deploy-sbcl-platform-probe -- --screenshot build-output/sbcl-platform.png

# Separate package identity, same tests, without the codeGeneration capability.
./build sbcl-platform-control
nix run .#deploy-sbcl-platform-control -- --screenshot build-output/sbcl-control.png
```

No network capability, secrets or runtime configuration are required. The SDL
host displays the results in two columns and writes `platform.txt` below
`SDL_GetPrefPath("nixbox", "sbcl-platform-probe")` in each package's LocalState.
Every line is flushed, including markers before deliberate faults. A missing
`COMPLETE` marker means the probe did not finish; installation or process
liveness alone does not establish success.

## What it exercises

- Allocate RW pages using `VirtualAllocFromApp`, write a small Win64 function,
  change to RX with `VirtualProtectFromApp`, flush the instruction cache and
  call it with positive and negative inputs. Change RX back to RW, patch its
  immediate operand, return to RX and verify different exact results.
- Observe FromApp RWX allocation/protection independently. Rejection is an
  observation, not a failed requirement: Microsoft's documented APIs reject
  those protections. The tests also deliberately import the **real desktop**
  `VirtualAlloc`/`VirtualProtect` exports from KERNELBASE, testing SBCL's RWX
  allocation, execution, protection changes and patching if allocation works.
  The SDK's UWP inline wrappers must not silently turn this into a FromApp test.
- Register a vectored exception handler, arm a page with `PAGE_NOACCESS`,
  recover a read fault by restoring RW and retry the original instruction.
  Re-arm and repeat; independently verify the stored value and handler count.
- Register a generated non-leaf function with `RtlAddFunctionTable`, verify
  its lookup entry, then fault inside its body and unwind its 40-byte stack
  frame to a C SEH handler. Check the exception's instruction address, read
  access and fault address, rather than accepting an arbitrary access violation.
- Create a suspended CRT thread with `_beginthreadex`, resume and join it,
  verifying a new TLS slot is initially empty and thread-local values remain
  independent. Remove handlers/tables, free pages/TLS and close the thread.

The probe's explicit desktop/VEH imports are **experiments**, not a claim of
supported UWP APIs or Store compliance. They are confined to `probe.def` and
`probe.c`; the shared SDK family/compiler settings and import audit are unchanged.
The main manifest declares `<Capability Name="codeGeneration" />`; the control
omits it. Both run the same checks even when a memory operation is rejected,
so the control may legitimately report failed executable-code checks.

## Xbox result (2026-10-05)

Both exact MSIX builds were hash-verified, signed and run on the real Xbox
through the tailnet-connected Linux runner `swa`. Full LocalState reports and
inspected screenshots agree:

| Primitive | With `codeGeneration` | Control without it |
| --- | --- | --- |
| FromApp RW → RX, execution and patching | Passed, including both inputs before/after patch | RX denied, error 5 |
| FromApp RWX protection | Accepted | Denied, error 5 |
| FromApp direct RWX allocation | Rejected, error 87 | Rejected, error 87 |
| Deliberate desktop RWX allocation, execution and patching | Passed | Passed |
| Two protected-page reads recovered through VEH | Passed | Passed |
| Registered generated-frame SEH unwind | Passed; intended read fault at code + 4 | Not exercised: RX step denied |
| Suspended CRT thread and independent TLS | Passed | Passed |
| Final report | `COMPLETE: 0 failed checks` | `COMPLETE: 2 failed checks` |

After repeat launches, each app retained the same running process at 5 and
65 seconds, and no new crash dump or WER report appeared. Earlier process
disappearances coincided with another app being foreground; their cause was
not established. The main probe was left visible on the console. This is a
short runtime check, not a long-term reliability or suspend/resume test.

The most useful finding for SBCL is that its RWX memory pattern and vectored
exception handling **worked through these deliberate desktop imports** on this
Developer Mode console. That removes the immediate need to assume a W^X runtime
rewrite. The capability also enabled the FromApp path. The accepted FromApp
RWX protection differs from Microsoft's documented contract, so treat it as
an observed device behavior, not a portable promise.

## Interpretation and limits

Microsoft documents an RW-to-RX JIT path with `codeGeneration`:
[VirtualAllocFromApp](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualallocfromapp)
and [VirtualProtectFromApp](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectfromapp).
Device results, rather than desktop Windows or Wine behavior, determine which
paths are usable on Xbox Developer Mode.

These successes remove important platform unknowns, but do not prove SBCL
works. SBCL's bootstrap/core loading, target compiler/runtime build,
symbol resolution and desktop filesystem/console adaptations remain. The
probe does not exercise a Lisp heap, GC, simultaneous code execution/patching,
multithreaded safepoints, SBCL's register/TLS layout, large address-space
reservations, suspend/resume or a REPL. If only RW-to-RX works, an SBCL adaptation
must account for *all* code mutations and concurrency; Apple's per-thread JIT
write-protection mechanism is not equivalent to Windows process-wide page
permission changes.

The same C checks have passed as a console consumer under Wine 11.0. That run
validates the probe's generated instructions, SEH/unwind metadata and assertions;
it is not Xbox policy validation. In particular Wine accepted FromApp RWX
protection even though Microsoft's documented contract rejects it.
