# D3D12 capability probe

Reports what Direct3D 12 exposes to a Developer Mode UWP package, on screen
and in `LocalState/nixbox/d3d12-caps/d3d12-caps.txt`.

```sh
nix build .#d3d12caps
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-d3d12caps
```

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
