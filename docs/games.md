# Game playground

[Back to the README](../README.md)

Two games exercise the toolchain beyond the small SDL and Direct3D templates:
an original physics arcade game and an existing-game port. Both have been
deployed and exercised on a real Xbox through the `swa` runner on the Whale
Justice tailnet. These are smoke tests, not full compatibility or performance
certification; the coverage and remaining gaps are described below.

## Ricochet

[games/ricochet](../games/ricochet) is a C++20, SDL3, Box2D 3.1.1 and ImGui
cannon arena. Shoot bouncing balls into a floating pile of 15 boxes, and push
the boxes into the green goal lane on the right. A target earns 100 points
once it enters the lane moving slowly enough; hitting it alone does not score.
Each round has 12 shots. Balls expire after 15 seconds, and the round finishes
when all targets score or the last ball expires after ammunition runs out.

Console checks exercised charging/firing, collisions and scoring (700 points,
7/15 targets), aiming, and resetting to 12 shots and zero points. The HUD was
inspected in console screenshots. Input was sent through Device Portal's remote
keyboard; no physical controller was connected for these tests. Audio and
persistent saves are not implemented in this prototype.

| Action | Keyboard | Xbox controller |
| --- | --- | --- |
| Aim | Arrow keys | Left stick up/down |
| Charge and fire | Hold Space, release | Hold A, release |
| Restart | R | B |
| Show/hide controls | Click HOW TO PLAY | Menu |

```sh
./build ricochet
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-ricochet

# Run the same game on the build host; this also runs its physics tests.
nix run .#ricochet-native

# Configure a local incremental Xbox build.
nix develop .#ricochet
cmake -S games/ricochet -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install" -DBUILD_TESTING=OFF
cmake --build build
cmake --install build
xbox-deploy
```

`simulation.cpp` owns the physics, ammunition, scoring and reset behavior;
`main.cpp` owns input and drawing. Physics advances at a fixed 60 Hz independent
of rendering. This separation leaves room for multiple players or an
authoritative network simulation, but no multiplayer, synchronization, or
cross-platform determinism is implemented or promised yet.

The native Release tests check actual collisions/scoring, arena bounds, aim
and charge clamps, cooldown boundaries, ammunition exhaustion and reset. A
headless preview can exercise a shot or a completed round without a console:

```sh
nix build .#ricochet-native
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  result/bin/ricochet --demo --show-help --screenshot /tmp/ricochet.bmp
# --demo-round exhausts a whole round; screenshots are BMP files.
```

The first version uses SDL's 2D renderer and ImGui's SDLRenderer3 backend. The
graphics are procedural, with no borrowed game artwork or external asset pack.
ImGui supplies the current UI font; SDL3_ttf/HarfBuzz are separate reusable ports,
not dependencies of Ricochet.

## SuperTux

[ports/supertux/game.nix](../ports/supertux/game.nix) pins the SDL3-based upstream
source and all its submodules. It uses the SDL renderer rather than OpenGL,
static libraries, and the usual `mkXboxApp` packaging route:

```sh
./build supertux
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-supertux
nix develop .#supertux
```

Console checks reached the main menu, world map and "Welcome to Antarctica"
level, then exercised movement, jumping/landing, collecting a coin, pausing and
relaunching. Config, profile and world-save files are written to LocalState;
the config file was unchanged after relaunch. Restoring completed-level progress
and non-default user options has not been tested. These checks used remote
keyboard input, not a physical controller. Audio output remains unverified;
no frame-rate claim is made from Device Portal screenshot timings.

The dependency overlay in [nix/supertux-libraries.nix](../nix/supertux-libraries.nix)
adds SDL3_image, SDL3_ttf, FreeType, HarfBuzz, PhysicsFS, OpenAL Soft, Vorbis,
GLM and fmt to `pkgsXbox`. Important UWP differences:

- SDL3_image uses stb and libpng without dynamically loaded codec plugins.
- SDL3_ttf uses FreeType and HarfBuzz, without the optional SVG font backend.
- PhysicsFS uses C++/WinRT for the package directory and writable LocalFolder.
- Packaged game data lives alongside the executable, not in a desktop
  `share/supertux` path. Mount that absolute path directly: desktop filesystem
  canonicalization fails with access denied in the app container. Saves belong
  in the app's writable storage.
- UWP starts in desktop fullscreen at the display dimensions, rather than a
  desktop 1280x800 window or an exclusive display-mode request.
- FreeType uses its portable stdio backend. OpenAL uses its UWP WASAPI backend.
- Box2D, ImGui, OpenAL, SDL3_image, SDL3_ttf, libpng and Vorbis use scalar paths
  where the current SDK/Clang intrinsic headers otherwise leave unresolved SSE
  helpers. SIMD optimization is a follow-up, not a prerequisite for these ports.
- Networking/add-on downloads, Discord, external text editors and desktop
  DbgHelp crash reporting are disabled. Device Portal supplies crash dumps.
- The embedded Squirrel interpreter is static; shell execution is unavailable.

The console installer also exposed an openappx content-types omission for
extensionless files, such as `levels/world1/info`. The patched packer emits
per-file overrides and runs format regression tests. `mkXboxApp` inspects the
finished MSIX's block map and content types and saves `inspection.txt` alongside
the package; an inspection failure now fails the build before deployment.

Upstream code, artwork, music and fonts retain their respective licenses and
credits. The package includes SuperTux's license and credits; this is a port,
not a new game using those assets under a different name. Keep the pinned
source and the local patches available when redistributing GPL binaries.

## Next experiments

SDL3 GPU is **not enabled for UWP** in the pinned SDL fork. Its desktop D3D12
swapchain path needs a WinRT-compatible implementation before either game can
use that API. The existing Direct3D 12 template remains the working shader/3D
route; a successful SDL renderer build is not evidence of SDL3 GPU support.

OpenLara is a follow-up port, not an integrated target yet. An original-asset
experiment should use newly made levels, models, animation and audio rather
than distribute Tomb Raider data. Box3D is also a separate future dependency
experiment; the present physics game uses Box2D.
