{
  inputs.nixbox.url = "github:mbrock/nixbox";
  outputs =
    { self, nixbox }:
    let
      # Each system nixbox builds on: x86_64-linux and aarch64-darwin.
      forEachSystem = f: builtins.mapAttrs f nixbox.lib;
    in
    {
      packages = forEachSystem (_: xbox: { default = import ./game.nix xbox; });
      # nix run .#deploy: sign, install, launch, and take a screenshot.
      apps = forEachSystem (system: xbox: { deploy = xbox.mkDeploy self.packages.${system}.default; });
      # nix develop, then make / make deploy: the same build, incrementally.
      devShells = forEachSystem (system: _: { default = self.packages.${system}.default.devShell; });
    };
}
