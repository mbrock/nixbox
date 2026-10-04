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
  outputs =
    inputs:
    let
      system = "x86_64-linux";
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
      toolchain = import ./nix/toolchain.nix { inherit pkgs inputs pkgsXbox llvmPackages; };
      xbox = import ./nix/xbox-pkgs.nix { inherit pkgs inputs toolchain llvmPackageSet llvmPackages; };
      pkgsXbox = xbox.pkgsXbox;
      app = import ./nix/app {
        inherit pkgs pkgsXbox llvmPackages inputs;
        inherit (xbox) xboxCC;
        inherit (toolchain) python;
      };
      # The interface a game's flake uses: see templates/game.
      xboxLib = app // {
        inherit pkgs pkgsXbox;
        inherit (xbox) mkPkgsXbox;
      };
      game = import ./templates/game/game.nix xboxLib;
    in
    {
      packages.${system} = toolchain.packages // {
        default = toolchain.hello;
        hello-uwp = toolchain.hello;
        inherit game;
        zlib-xbox = pkgsXbox.zlib;
        hello-xbox = pkgsXbox.hello;
        luau-xbox = pkgsXbox.luau;
        xbox-cxx-headers = xbox.cxxHeaders;
        xbox-cc = xbox.xboxCC;
      };
      legacyPackages.${system} = { inherit pkgsXbox; };
      lib.${system} = xboxLib;
      apps.${system} = {
        deploy = {
          type = "app";
          program = pkgs.lib.getExe app.deployTool;
        };
        deploy-game = app.mkDeploy game;
      };
      templates.game = {
        path = ./templates/game;
        description = "A Direct3D 12 game for Xbox, built with CMake";
      };
      checks.${system} = {
        xbox-compiler = import ./nix/check-compiler.nix {
          inherit pkgs llvmPackages;
          compiler = xbox.xboxCC;
        };
        hello-uwp = toolchain.hello;
        inherit game;
        zlib-xbox = pkgsXbox.zlib;
        hello-xbox = pkgsXbox.hello;
        luau-xbox = pkgsXbox.luau;
      };
      devShells.${system}.default = pkgs.mkShell {
        packages = [
          toolchain.tools
          pkgs.git
          pkgs.openssl
        ];
        inherit (toolchain) UWP_XWIN_ROOT UWP_SDK_ROOT UWP_CPPWINRT_EXE;
        UWP_MAKEPRI_X86 = "1";
        XBOX_ZLIB_INCLUDE_DIR = "${pkgs.lib.getDev pkgsXbox.zlib}/include";
        XBOX_ZLIB_LIBRARY = "${pkgs.lib.getLib pkgsXbox.zlib}/lib/zs.lib";
        XBOX_LUAU_ROOT = pkgsXbox.luau;
        XBOX_SHADER_INCLUDE_DIR = "${toolchain.shaders}/include";
        XBOX_DIRECTX_HEADERS_INCLUDE_DIR = "${pkgs.directx-headers}/include/directx";
        XBOX_CXX_HEADERS = xbox.cxxHeaders;
        WINEARCH = "wow64";
        WINEDEBUG = "-all";
        WINEDLLOVERRIDES = "mscoree,mshtml=;msxml6=n,b";
        shellHook = ''
          export WINEPREFIX="''${WINEPREFIX:-''${XDG_CACHE_HOME:-$HOME/.cache}/xbox-uwp/wine}"
        '';
      };
    };
}
