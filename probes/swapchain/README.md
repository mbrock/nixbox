# Swapchain probe

Which swapchains reach the screen. A bare `CoreApplication` app (so the
display APIs run on the view thread) cycles every eight seconds through
swapchain sizes, formats, and colour spaces, drawing a pattern in swapchain
pixels: a band in the phase's tag colour, then one-pixel vertical stripes on
the left and horizontal ones on the right. A console screenshot (always
3840x2160 here) shows whether a stripe stayed one output pixel wide. The
report goes to `LocalState/swapchain.txt`.

```sh
nix build .#swapchain
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-swapchain
curl "$UWP_DEVICE_URL/ext/screenshot?hdr=false" -o shot.png   # during a phase
curl "$UWP_DEVICE_URL/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=Nixbox.swapchain_0.1.0.0_x64__qscf1nrdzxz2p&path=%5C%5CLocalState&filename=swapchain.txt"
```

## Xbox Series X, installed as a Game (2026-10-06)

The CoreWindow is 1920x1080 view pixels at 1.0 raw pixels per view pixel, and
the console drives the TV at 3840x2160, 59.94 Hz, 8-bit RGB limited. This TV
offers only 4K and 1080p modes at up to 60 Hz, none with HDR10.

| Phase (tag) | Swapchain | Colour space | In the screenshot |
| --- | --- | --- | --- |
| red | 3840x2160 BGRA8 | sRGB | 1-px stripes, 0/255: **native 4K** |
| green | 1920x1080 BGRA8 | sRGB | 2-px stripes, 64/191: filtered 2x upscale |
| blue | 3840x2160 RGB10A2 | HDR10 (PQ, 200 nit white) | native 4K, white shown at 148: tone-mapped to the SDR output |
| yellow | 3840x2160 RGBA16F | scRGB (2.5 = 200 nits) | native 4K, white clipped to 255 |
| magenta | 2560x1440 BGRA8 | sRGB | not captured |

Every swapchain was created and resized with `FLIP_DISCARD` and
`DXGI_SCALING_STRETCH` for the 1080p window, and every colour space reported
present support and accepted `SetColorSpace1` (HDR10 metadata too), even on
an SDR output. So a UWP game renders native 4K just by sizing its swapchain,
and can author HDR10 or scRGB and let the system map it to the display.

`Present(1, 0)` paced every phase at 59.94 Hz (median 16.68 ms). Device
Portal screenshots stall presentation for up to about 60 ms; the one phase
without screenshots stayed between 16.3 and 17.1 ms.
