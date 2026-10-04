{
  inputs.nixbox.url = "github:mbrock/nixbox";
  outputs =
    { self, nixbox }:
    let
      xbox = nixbox.lib.x86_64-linux;
    in
    {
      packages.x86_64-linux.default = import ./game.nix xbox;
      # nix run .#deploy: sign, install, launch, and take a screenshot.
      apps.x86_64-linux.deploy = xbox.mkDeploy self.packages.x86_64-linux.default;
    };
}
