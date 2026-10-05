# D3D12 capability probe

Reports what Direct3D 12 exposes to a Developer Mode UWP package, on screen
and in `LocalState/nixbox/d3d12-caps/d3d12-caps.txt`: the capability queries,
format support, the display, live tests of the binding and render-target
patterns a portable renderer would build on, and measured throughput.

```sh
nix build .#d3d12caps
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-d3d12caps
# The GPU tests take a few seconds; then fetch the report:
curl "$UWP_DEVICE_URL/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=Nixbox.d3d12caps_0.1.0.0_x64__qscf1nrdzxz2p&path=%5C%5CLocalState%5Cnixbox%5Cd3d12-caps&filename=d3d12-caps.txt"
```

The shaders in `probe.hlsl` compile with DXC into one header per entry point;
`gpu.cpp` runs each test and checks the result against the CPU's answer.

Xbox Series X, retail console in Developer Mode, OS 10.0.26100.9438
(2026-10-05). Installed as an *App*, the process sees 832 MB of shared GPU
memory; installed as a *Game* (see [App or game](../../docs/apps.md#app-or-game)),
it sees 5120 MB of physical memory and 4096 MB shared. The feature set is
identical in both:

| Capability | Value |
| --- | --- |
| Adapter | `SraKmd_arden` (1414:d000), 512 MB dedicated |
| Max feature level | 11_0 |
| Highest shader model | 6.4 |
| Memory | cache-coherent UMA |
| Resource binding tier | 3 (tiled resources tier 1) |
| Wave ops | yes, 64 lanes; int64 shader ops yes |
| Native 16-bit shader ops | no |
| Mesh shaders | tier 0 (none) |
| Ray tracing | tier 0 (none) |
| VRS, sampler feedback, view instancing, barycentrics | none |
| Conservative raster, ROVs | none |
| Enhanced barriers, work graphs | none |

This matches Microsoft's statement that UWP apps and games on Series X|S get
DirectX 12 at hardware feature level 11.0; DirectX 12 Ultimate features are
reserved for GDK titles on development kits.

### Measured on the console (installed as a Game, 2026-10-06)

| Test | Result |
| --- | --- |
| Root UAV/SRV by GPU virtual address | works |
| Unbounded SRV tables (buffers and textures), `NonUniformResourceIndex` | works |
| Shader-visible heap of 1,000,000 descriptors | created |
| Wave intrinsics in compute | works, 64 lanes |
| 64-bit integer arithmetic | works |
| `ExecuteIndirect` changing root constants per dispatch, with and without a GPU count | works |
| 4x MSAA MRT (RGBA16F, RG16F, R8) over D32F S8, reversed-Z, resolve | works |
| `dot4add_u8packed` (cs_6_4) | **returns 0** on every lane; the same shader's plain stores land |
| cs_6_5 pipeline | rejected (`E_INVALIDARG`) |
| Heap-indexed root signature / cs_6_6 `ResourceDescriptorHeap` pipeline | created / rejected |

Bindless therefore means shader-model-5.1 style unbounded descriptor tables,
not shader model 6.6 heap indexing. Every format a deferred or temporal HDR
renderer uses renders, blends, multisamples (up to 8x), and resolves; only
R32F, R32 uint, and RGBA32 uint report typed UAV loads.

| Throughput | Result |
| --- | --- |
| fp32 FMA | 11.8 TFLOPS (theoretical peak 12.1) |
| 256 MB buffer copy, compute or `CopyBufferRegion` | ~330 GB/s read+write |
| Fill, RGBA8 opaque at 4K | ~112 Gpixel/s (64 ROPs at 1.825 GHz: 117) |
| Fill, RGBA16F additive at 4K | ~56 Gpixel/s |
| Instanced, vertex-generated tubes, 5.1 M tiny triangles at 1080p | ~5.8 Gtri/s (mostly sub-pixel: a setup rate) |

A Game install gets essentially the whole GPU. The copy bandwidth matches the
336 GB/s of the console's slower memory pool rather than the 560 GB/s of the
fast one, which suggests where these allocations land.

The display: the console drives 3840x2160 at 59.94 Hz in 8-bit RGB, while the
UWP view and SDL's window are 1920x1080 (SDL picks its Direct3D 11 renderer).
This TV reported no HDR10 modes, so HDR output is still untested.
