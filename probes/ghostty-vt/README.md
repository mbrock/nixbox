# Ghostty VT on Xbox

`nix/ghostty-vt.nix` pins Ghostty at
`35a81a980bb9fce09a1ea762a68b55f8eb3477ed` with Zig 0.16.0. It installs
the static archive, public `ghostty/vt.h` headers, and `libghostty-vt.pc`.
Consumers use `pkg-config --cflags --libs --static libghostty-vt`; this
includes `GHOSTTY_STATIC` and, on Xbox, `WindowsApp.lib`.
This is a pinned preview API, not a stable ABI or Ghostty's graphical app.

The library-only build disables SIMD and Kitty graphics. It does not build
the GUI, C++ dependencies, image decoder, or themes. Three pinned dependency
archives (uucode, translate-c, and aro) satisfy the build graph; `zig build
--system` forbids fetching during compilation. LLVM is selected explicitly.
The Windows archive bundles compiler-rt; native C consumers use their own
compiler runtime and libm, avoiding Zig 0.16's invalid native ELF compiler-rt.

The UWP patch uses CRT heap allocation and `VirtualAllocFromApp` for zeroed,
page-aligned terminal memory, rather than Zig's `NtAllocateVirtualMemory`.
It also removes the retained desktop NT TinyIo filesystem backend and raw
stderr writes. Terminal logging still uses the host's C API callback. There
is no Windows filesystem backend in this recipe; Kitty graphics is disabled
accordingly. Panics trap without attempting to write to a console handle.

`consumer.c` checks independently expected cells, cursor positions, and grid
dimensions for split UTF-8 (including a wide CJK character), split CSI, CUP,
CUB, EL, ED, scroll/history, shrinking/growing reflow, and 3,000 lines of
page-pool stress. `nix/check-ghostty-vt.nix` links only installed headers and
pkg-config metadata. The Windows check audits every undefined archive symbol
against an explicit reviewed list, records PE imports, and optionally runs
the same assertions under Wine. Wine success is not Xbox hardware validation.

From the nixbox root:

```sh
./build ghostty-vt-xbox
./build checks.x86_64-linux.ghostty-vt-native     # GCC/system linker + assertions
./build checks.x86_64-linux.ghostty-vt-consumer   # installed UWP consumer + Wine
./build ghostty-vt-probe
nix run .#deploy-ghostty-vt-probe
```

The Windows check output contains `bin/consumer.exe`, `imports.txt`, and
`undefined.txt`, plus `behavior.txt` when Wine runs. The check on Linux requires
Wine; on build hosts without Wine it remains a compile/link/import check only.

The Xbox diagnostic compiles the same C consumer in a tiny SDL3 status host.
No network, credentials or runtime arguments are needed. It writes `vt.txt`
under `SDL_GetPrefPath("nixbox", "ghostty-vt-probe")` in package LocalState.
All five groups and the final all-checks-passed marker passed on real Xbox
Series X on 2026-10-05. This exercises terminal state and the UWP allocator,
not a terminal renderer, SSH/PTY integration, long-term memory soak or
suspend/resume behavior.
