{
  description = "Linux tools for building and deploying Xbox UWP examples";
  nixConfig = {
    extra-substituters = [ "https://nix.swa.sh/xbox-cache" ];
    extra-trusted-public-keys = [ "xbox-uwp-1:YMEqS8rfsKIOyXplusBbWtNUuKTNikZqw7X/BtW3ijQ=" ];
  };
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.uwp-crossbuild = {
    url = "github:gianlucamazza/uwp-crossbuild/d40e8de6b16d1edc78276c14157bf735d2744d5b";
    flake = false;
  };
  inputs.openappx = {
    url = "github:gianlucamazza/openappx/da05f7f66e2ac9071253aea954088d3159b734fd";
    flake = false;
  };
  inputs.cppwinrt = {
    url = "github:microsoft/cppwinrt/2.0.250303.1";
    flake = false;
  };
  inputs.winmd = {
    url = "github:microsoft/winmd/0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a";
    flake = false;
  };
  inputs.nxtui = {
    url = "github:mbrock/nxtui/54e888fecee1478590ecef159c5b272642d0282c";
    flake = false;
  };
  outputs =
    inputs:
    let
      lib = inputs.nixpkgs.lib;
      # Linux is the reference host. macOS builds the same packages natively;
      # only the steps that run Windows tools under Wine need Linux.
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
      ];
      forSystem =
        system:
        let
          pkgs = import inputs.nixpkgs {
            inherit system;
            config.allowUnfreePredicate =
              package:
              builtins.elem (inputs.nixpkgs.lib.getName package) [
                "uwp-xwin"
                "uwp-sdk"
                "uwp-msxml6"
              ];
          };
          llvmPackageSet = "llvmPackages_23";
          llvmPackages = pkgs.${llvmPackageSet};
          toolchain = import ./nix/toolchain.nix {
            inherit
              pkgs
              inputs
              pkgsXbox
              llvmPackages
              ;
          };
          xbox = import ./nix/xbox-pkgs.nix {
            inherit
              pkgs
              inputs
              toolchain
              llvmPackageSet
              llvmPackages
              ;
          };
          pkgsXbox = xbox.pkgsXbox;
          app = import ./nix/app {
            inherit
              pkgs
              pkgsXbox
              llvmPackages
              inputs
              ;
            inherit (xbox) xboxCC;
            inherit (toolchain) python tools;
          };
          # The interface a game's flake uses: see templates/game.
          xboxLib = app // {
            inherit pkgs pkgsXbox;
            inherit (xbox) mkPkgsXbox;
          };
          game = import ./templates/game/game.nix xboxLib;
          hello = import ./example/hello.nix xboxLib;
          sdlgame = import ./templates/sdl/game.nix xboxLib;
          ricochet = import ./games/ricochet/game.nix xboxLib;
          ricochet-native = import ./games/ricochet/native.nix pkgs;
          supertux = import ./ports/supertux/game.nix xboxLib;
          d3d12caps = import ./probes/d3d12-caps/probe.nix xboxLib;
          nxt-network = import ./probes/nxt-network/probe.nix {
            xbox = xboxLib;
            nxtuiSource = inputs.nxtui;
          };
          nxt-chat = import ./apps/nxt-chat/app.nix {
            xbox = xboxLib;
            nxtuiSource = inputs.nxtui;
          };
          # Wine runs midlrt for the XAML sample and MSBuild for the .vcxproj
          # route. Nixpkgs has no Wine for Apple silicon.
          hasWine = pkgs.stdenv.hostPlatform.isLinux;
          withWine = lib.optionalAttrs hasWine;
          wineTools = [
            "toolchain"
            "uwp-crossbuild"
            "cache-roots"
          ];
        in
        {
          packages =
            (if hasWine then toolchain.packages else removeAttrs toolchain.packages wineTools)
            // withWine {
              inherit hello;
              hello-vcxproj = toolchain.hello;
            }
            // {
              default = if hasWine then hello else game;
              inherit
                game
                sdlgame
                ricochet
                ricochet-native
                supertux
                d3d12caps
                nxt-network
                nxt-chat
                ;
              zlib-xbox = pkgsXbox.zlib;
              hello-xbox = pkgsXbox.hello;
              luau-xbox = pkgsXbox.luau;
              sdl3-xbox = pkgsXbox.SDL3;
              box2d-xbox = pkgsXbox.box2d;
              imgui-xbox = pkgsXbox.imgui;
              sdl3-image-xbox = pkgsXbox.SDL3_image;
              sdl3-ttf-xbox = pkgsXbox.SDL3_ttf;
              harfbuzz-xbox = pkgsXbox.harfbuzz;
              nxtrt-iocp-xbox = pkgsXbox.nxtrt-iocp;
              nxtui-sdl-xbox = pkgsXbox.nxtui-sdl;
              xbox-cxx-headers = xbox.cxxHeaders;
              xbox-cc = xbox.xboxCC;
            };
          legacyPackages = { inherit pkgsXbox; };
          lib = xboxLib;
          apps = {
            deploy = {
              type = "app";
              program = pkgs.lib.getExe app.deployTool;
            };
            deploy-game = app.mkDeploy game;
            deploy-sdlgame = app.mkDeploy sdlgame;
            deploy-ricochet = app.mkDeploy ricochet;
            deploy-supertux = app.mkDeploy supertux;
            deploy-d3d12caps = app.mkDeploy d3d12caps;
            deploy-nxt-network = app.mkDeploy nxt-network;
            deploy-nxt-chat = app.mkDeploy nxt-chat;
          }
          // withWine { deploy-hello = app.mkDeploy hello; };
          checks = {
            xbox-compiler = import ./nix/check-compiler.nix {
              inherit pkgs llvmPackages;
              compiler = xbox.xboxCC;
            };
            inherit
              game
              sdlgame
              ricochet
              ricochet-native
              supertux
              nxt-network
              nxt-chat
              ;
            zlib-xbox = pkgsXbox.zlib;
            hello-xbox = pkgsXbox.hello;
            luau-xbox = pkgsXbox.luau;
            nxtrt-iocp-xbox = pkgsXbox.nxtrt-iocp;
            nxtui-sdl-consumer = import ./nix/check-nxtui-sdl.nix { xbox = xboxLib; };
          }
          // withWine {
            inherit hello;
            hello-vcxproj = toolchain.hello;
          };
          devShells = {
            game = game.devShell;
            sdlgame = sdlgame.devShell;
            ricochet = ricochet.devShell;
            supertux = supertux.devShell;
            d3d12caps = d3d12caps.devShell;
            nxt-network = nxt-network.devShell;
            nxt-chat = nxt-chat.devShell;
          }
          // withWine {
            hello = hello.devShell;
            default = pkgs.mkShell {
              packages = [
                toolchain.tools
                pkgs.git
                pkgs.openssl
              ];
              inherit (toolchain) UWP_XWIN_ROOT UWP_SDK_ROOT UWP_CPPWINRT_EXE;
              WINEARCH = "wow64";
              WINEDEBUG = "-all";
              WINEDLLOVERRIDES = "mscoree,mshtml=;msxml6=n,b";
              shellHook = ''
                export WINEPREFIX="''${WINEPREFIX:-''${XDG_CACHE_HOME:-$HOME/.cache}/xbox-uwp/wine}"
              '';
            };
          };
        };
      outputs = lib.genAttrs systems forSystem;
      # The flake's output names, each keyed by system.
      perSystem = name: lib.mapAttrs (_: outputs: outputs.${name}) outputs;
    in
    {
      packages = perSystem "packages";
      legacyPackages = perSystem "legacyPackages";
      lib = perSystem "lib";
      apps = perSystem "apps";
      checks = perSystem "checks";
      devShells = perSystem "devShells";
      templates.game = {
        path = ./templates/game;
        description = "A Direct3D 12 game for Xbox, built with Meson";
      };
      templates.sdl = {
        path = ./templates/sdl;
        description = "An SDL3 game for Xbox, built with Meson";
      };
    };
}
